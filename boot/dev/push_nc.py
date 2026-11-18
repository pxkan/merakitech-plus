#!/usr/bin/env python3
"""
push_nc.py — upload stor fil til displayet via busybox nc-lytter (rå binær over TCP, Mac→device).
Undgår telnet-heredoc/echo-deadlock. Device starter 'nc -l -p PORT > tmp'; Mac connecter og sender
rå bytes; derefter mv+chmod+md5 via telnet. Verificerer md5 round-trip.

Brug: python3 push_nc.py <lokal_fil> <remote_dest> [port]
"""
import hashlib, os, pathlib, socket, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
PY = sys.executable
HOST = os.environ["TELNET_HOST"]  # set TELNET_HOST=<display-ip>

def telnet(cmd, timeout=30):
    r = subprocess.run([PY, str(HERE / "telnet_run.py"), cmd, str(timeout)],
                       capture_output=True, text=True, env=dict(os.environ))
    return r.returncode, r.stdout.strip(), r.stderr

def main():
    local, dest = sys.argv[1], sys.argv[2]
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 4444
    raw = pathlib.Path(local).read_bytes()
    md5_local = hashlib.md5(raw).hexdigest()
    tmp = "/tmp/.push_nc_tmp"
    print(f"[push_nc] {local} ({len(raw)} B, md5 {md5_local}) -> {dest} via nc {HOST}:{port}", file=sys.stderr)

    # 1. start nc-lytter på enheden (nohup så den overlever telnet-session-luk)
    rc, out, err = telnet(f"rm -f {tmp}; nohup nc -l -p {port} > {tmp} 2>/tmp/nc.err & echo NC_START=$!; sleep 1; cat /tmp/nc.err 2>/dev/null", 15)
    print(f"[push_nc] listener start: {out}", file=sys.stderr)
    time.sleep(1.0)

    # 2. Mac connecter og sender rå bytes (retry indtil lytter er klar)
    sent = False
    for attempt in range(8):
        try:
            s = socket.create_connection((HOST, port), timeout=10)
            s.settimeout(30)
            s.sendall(raw)
            s.shutdown(socket.SHUT_WR)
            # dræn evt. svar til EOF
            try:
                while s.recv(4096): pass
            except Exception: pass
            s.close()
            sent = True
            break
        except Exception as e:
            print(f"[push_nc] connect attempt {attempt+1} fejlede: {e}", file=sys.stderr)
            time.sleep(1.5)
    if not sent:
        print("[push_nc] kunne ikke connecte til nc-lytter", file=sys.stderr)
        sys.exit(1)
    time.sleep(1.0)  # lad nc skrive færdig + exit

    # 3. mv + chmod + md5
    rc, out, err = telnet(f"mv {tmp} {dest} && chmod 755 {dest} && md5sum {dest}; ls -la {dest}", 20)
    print(out)
    if md5_local in out:
        print(f"[push_nc] MD5 ROUND-TRIP OK ✓ ({md5_local})", file=sys.stderr)
        sys.exit(0)
    print(f"[push_nc] MD5 MISMATCH (forventet {md5_local})", file=sys.stderr)
    sys.exit(1)

if __name__ == "__main__":
    main()