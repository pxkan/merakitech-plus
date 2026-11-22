#!/usr/bin/env python3
"""mock_device.py — a fake MerakiTech display for testing tools/merakiplus.py without hardware.

It serves a "telnet" shell (plain TCP, no login) on 127.0.0.1 backed by bash. Device paths are
relocated under a sandbox root via merakiplus' hidden --test-prefix option, and the few things a
sandbox cannot do for real are emulated with shell functions:

  * processes: <root>/proc/<pid>/{exe,cmdline,exe_path,image}. `readlink .../exe` reports the
    launched path, or "<path> (deleted)" once that file was replaced (inode semantics).
  * kill -STOP/-CONT/-TERM/-9 act on fake pids; when no UI is left the vendor watchdog is
    emulated (it restarts the slot recorded in <root>/watchdog_slot). A stopped process keeps
    its pid (so the watchdog does not restart it), gets a <root>/proc/<pid>/stopped marker and
    reports "State:\tT (stopped)" in /proc/<pid>/status, as Linux does.
  * nohup ./demo_v9 spawns a fake UI process; nc -l -p PORT is a tiny Python listener.
  * uname -m says armv7l.

Failure injection (for the installer's error paths): create <root>/inject/<name> with
inject(root, name[, value]) and remove it with uninject(root, name). The shell functions
check for the file on every call, so a test can switch a failure on and off between runs:

  * gzip_fail        gzip exits 1 without output (busybox gzip OOM); base64 still exits 0.
  * ui_start=die     `nohup ./demo_v9` kills the shell: the telnet connection drops mid-switch.
  * ui_start=never   `nohup ./demo_v9` starts nothing (the binary fails to come up).

When the shell exits, the server closes the connection (like telnetd), so a client sees a
transport drop at once instead of waiting for its timeout.

Everything here is synthetic — no vendor files are used. demo.sh below only reproduces the
lines the installer anchors on.
"""
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import threading

DEMO_SH = """#!/bin/sh
# SYNTHETIC test fixture — not the vendor script. Only the structure the installer relies on.
MOUNT_SUCCESS=0
ubiattach -m 6 && mount -t ubifs ubi0:new_customer /new_customer && MOUNT_SUCCESS=1
if [ -f /customer/temp.tar.gz ]; then
    if [ "$MOUNT_SUCCESS" -eq 1 ]; then
        if [ ! -f /customer/new ]; then
            touch /customer/new
        fi
    fi
fi
if [ "$MOUNT_SUCCESS" -eq 1 ] && [ -f /customer/new ]; then
\techo "ota bookkeeping"
fi
insmod /config/modules/4.9.84/fake_touch.ko
# kernel_mod_list

if [ "$MOUNT_SUCCESS" -eq 1 ] && [ -f /customer/new ]; then
    echo "===> starting from B"
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/new_customer/libs:/config/wifi
    cd /new_customer
    ./demo_v9 &
    sleep 3
    /customer/test.sh new_customer &
else
    echo "===> starting from A"
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/customer/libs:/config/wifi
    cd /customer
    ./demo_v9 &
    sleep 3
    ./test.sh customer &
fi
"""

FUNCS = r'''
R="$MOCK_ROOT"
uname() { [ "$1" = "-m" ] && echo armv7l || command uname "$@"; }
mock_inject() { [ -f "$R/inject/$1" ]; }
gzip() { if mock_inject gzip_fail; then echo "gzip: out of memory" >&2; return 1; fi; command gzip "$@"; }
command -v md5sum >/dev/null 2>&1 || md5sum() {
  rc=0; for f in "$@"; do if [ -e "$f" ]; then echo "$(md5 -q "$f")  $f"; else echo "md5sum: $f: No such file" >&2; rc=1; fi; done; return $rc; }
mock_next_pid() { n=$(cat "$R/pidcounter"); n=$((n+1)); echo $n > "$R/pidcounter"; echo $n; }
mock_spawn() {  # $1 = absolute exe path, $2 = argv0
  p=$(mock_next_pid); mkdir -p "$R/proc/$p"; cp "$1" "$R/proc/$p/image"
  printf '%s' "$1" > "$R/proc/$p/exe_path"; ln -s "$R/proc/$p/image" "$R/proc/$p/exe"
  printf '%s\000' "$2" > "$R/proc/$p/cmdline"; mock_state $p S; echo "spawn $p $1" >> "$R/events"; }
mock_state() {  # $1 = pid, $2 = S (running) / T (stopped)
  if [ "$2" = T ]; then touch "$R/proc/$1/stopped"; printf 'Name:\tdemo_v9\nState:\tT (stopped)\n' > "$R/proc/$1/status"
  else rm -f "$R/proc/$1/stopped"; printf 'Name:\tdemo_v9\nState:\tS (sleeping)\n' > "$R/proc/$1/status"; fi; }
mock_ui_left() { for d in "$R"/proc/[0-9]*; do [ -f "$d/exe_path" ] && case "$(cat $d/exe_path)" in */demo_v9) return 0;; esac; done; return 1; }
readlink() {
  case "$1" in
    "$R"/proc/*/exe)
      d="${1%/exe}"
      if [ -f "$d/exe_path" ]; then p=$(cat "$d/exe_path")
        if [ -f "$p" ] && cmp -s "$p" "$d/image"; then echo "$p"; else echo "$p (deleted)"; fi; return 0; fi;;
  esac
  command readlink "$@"; }
kill() {
  sig=TERM; case "$1" in -*) sig="${1#-}"; shift;; esac
  for p in "$@"; do
    if [ -d "$R/proc/$p" ]; then
      echo "kill -$sig $p" >> "$R/events"
      case "$sig" in
        STOP) mock_state $p T;; CONT) mock_state $p S;;
        *) rm -rf "$R/proc/$p"
           if ! mock_ui_left; then ws=$(cat "$R/watchdog_slot" 2>/dev/null)
             [ "$ws" = B ] && [ -f "$R/new_customer/demo_v9" ] && mock_spawn "$R/new_customer/demo_v9" ./demo_v9
             [ "$ws" = A ] && mock_spawn "$R/customer/demo_v9" ./demo_v9
           fi;;
      esac
    else command kill -s "$sig" "$p" 2>/dev/null || command kill "$p" 2>/dev/null; fi
  done; return 0; }
nc() {  # only "nc -l -p PORT" is used by the installer
  python3 -c '
import socket,sys
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(("127.0.0.1",int(sys.argv[1]))); s.listen(1)
c,_=s.accept()
while True:
    b=c.recv(65536)
    if not b: break
    sys.stdout.buffer.write(b)
sys.stdout.flush(); c.close()' "$3"; }
nohup() {
  case "$1" in
    ./demo_v9) case "$(cat "$R/inject/ui_start" 2>/dev/null)" in
                 die) command kill -9 $$;;
                 never) :;;
                 *) mock_spawn "$PWD/demo_v9" ./demo_v9;;
               esac;;
    nc) shift; nc "$@";;
    *) command nohup "$@";;
  esac; }
'''


def make_root(root, scenario="stock_a"):
    """scenario: stock_a  (stock running from A, B holds a vendor copy, no flag)
                 vendor_b (vendor UI running from B after a vendor update, flag set)
                 empty_b  (stock A, slot B has no binary)"""
    root = pathlib.Path(root)
    if root.exists():
        shutil.rmtree(root)
    for d in ("customer/libs", "new_customer/libs", "tmp", "proc", "config/wifi"):
        (root / d).mkdir(parents=True)
    (root / "customer/demo.sh").write_text(DEMO_SH)
    (root / "customer/demo_v9").write_bytes(b"\x7fELF MOCK VENDOR UI slot A\n" * 50)
    (root / "customer/test.sh").write_text("#!/bin/sh\n# synthetic watchdog stand-in\n")
    (root / "customer/config.json").write_text('{\n\t"brewTemper":\t870\n}\n')
    if scenario in ("stock_a", "vendor_b"):
        tag = b"slot A" if scenario == "stock_a" else b"slot B (newer vendor build)"
        (root / "new_customer/demo_v9").write_bytes(b"\x7fELF MOCK VENDOR UI " + tag + b"\n" * 50)
    if scenario == "vendor_b":
        (root / "customer/new").touch()
    (root / "pidcounter").write_text("700\n")
    (root / "events").write_text("")
    slot = "B" if scenario == "vendor_b" else "A"
    (root / "watchdog_slot").write_text(slot + "\n")
    # watchdog process (test.sh <slot>) and the running UI
    wd = root / "proc/650"
    wd.mkdir()
    os.symlink("/bin/sh", wd / "exe")
    arg = b"/customer/test.sh\0new_customer\0" if slot == "B" else b"./test.sh\0customer\0"
    (wd / "cmdline").write_bytes(b"/bin/sh\0" + arg)
    ui = root / ("new_customer/demo_v9" if slot == "B" else "customer/demo_v9")
    p = root / "proc/600"
    p.mkdir()
    shutil.copy(ui, p / "image")
    (p / "exe_path").write_text(str(ui))
    os.symlink(p / "image", p / "exe")
    (p / "cmdline").write_bytes(b"./demo_v9\0")
    set_stopped(root, 600, False)
    return root


def set_stopped(root, pid, stopped=True):
    """Put a fake process into the state `kill -STOP` (True) / `kill -CONT` (False) leaves it in."""
    d = pathlib.Path(root) / "proc" / str(pid)
    (d / "status").write_text("Name:\tdemo_v9\nState:\t" + ("T (stopped)" if stopped else "S (sleeping)") + "\n")
    if stopped:
        (d / "stopped").touch()
    elif (d / "stopped").exists():
        (d / "stopped").unlink()


def is_stopped(root, pid):
    return (pathlib.Path(root) / "proc" / str(pid) / "stopped").exists()


def inject(root, name, value=""):
    """Switch on a failure injection (see the module docstring)."""
    d = pathlib.Path(root) / "inject"
    d.mkdir(exist_ok=True)
    (d / name).write_text(value)


def uninject(root, name):
    f = pathlib.Path(root) / "inject" / name
    if f.exists():
        f.unlink()


class MockServer:
    def __init__(self, root):
        self.root = str(pathlib.Path(root).resolve())
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(4)
        self.port = self.srv.getsockname()[1]
        self.funcs = pathlib.Path(self.root) / ".mockfuncs.sh"
        self.funcs.write_text(FUNCS)
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            threading.Thread(target=self._session, args=(c,), daemon=True).start()

    def _session(self, c):
        env = dict(os.environ, MOCK_ROOT=self.root)
        sh = subprocess.Popen(["bash", "--noprofile", "--norc"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, cwd=self.root)
        sh.stdin.write(f". '{self.funcs}'\n".encode())
        sh.stdin.flush()
        c.sendall(b"\r\nMock display (synthetic)\r\n# ")

        def pump_out():
            for chunk in iter(lambda: sh.stdout.read1(65536), b""):
                try:
                    c.sendall(chunk.replace(b"\n", b"\r\n"))
                except OSError:
                    break
            try:                        # the shell is gone: hang up, as telnetd does
                c.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        threading.Thread(target=pump_out, daemon=True).start()
        try:
            while True:
                b = c.recv(65536)
                if not b:
                    break
                sh.stdin.write(b.replace(b"\r\n", b"\n").replace(b"\r", b"\n"))
                sh.stdin.flush()
        except OSError:
            pass
        finally:
            try:
                sh.stdin.close()
            except OSError:
                pass
            sh.terminate()
            c.close()

    def close(self):
        self.srv.close()


if __name__ == "__main__":
    r = make_root(sys.argv[1] if len(sys.argv) > 1 else "/tmp/merakiplus-mock",
                  sys.argv[2] if len(sys.argv) > 2 else "stock_a")
    s = MockServer(r)
    print(f"mock display on 127.0.0.1:{s.port}  root={r}  (use --port {s.port} --test-prefix {r})")
    threading.Event().wait()
