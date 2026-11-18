#!/usr/bin/env python3
"""End-to-end tests of tools/merakiplus.py (install / re-install / revert / dry-run) against
tests/mock_device.py. No hardware, no network beyond 127.0.0.1.

    python3 tests/test_installer.py
"""
import hashlib
import io
import contextlib
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(HERE))
import merakiplus as mp          # noqa: E402
import mock_device as md         # noqa: E402

FAILS = []


def check(name, cond, detail=""):
    print(("  [PASS] " if cond else "  [FAIL] ") + name + ("" if cond else f"  -- {detail}"))
    if not cond:
        FAILS.append(name)


def md5f(p):
    return hashlib.md5(pathlib.Path(p).read_bytes()).hexdigest()


def tree_digest(root):
    h = hashlib.md5()
    for p in sorted(pathlib.Path(root).rglob("*")):
        if p.is_file() and not p.is_symlink() and p.name not in ("events", ".mockfuncs.sh"):
            h.update(str(p.relative_to(root)).encode() + p.read_bytes())
    return h.hexdigest()


def run(srv, root, *args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = mp.main([args[0], "--ip", "127.0.0.1", "--port", str(srv.port), "--test-prefix", str(root),
                      "--yes", "--log", str(root.parent / "test.log")] +
                     (["--bin-dir", str(ROOT / "bin")] if args[0] == "install" else []) + list(args[1:]))
    out = buf.getvalue()
    if rc != 0:
        print(out)
    return rc, out


def ui(root):
    res = []
    for d in sorted((root / "proc").iterdir()):
        ep = d / "exe_path"
        if ep.exists() and ep.read_text().endswith("/demo_v9"):
            res.append((d.name, ep.read_text()[len(str(root)):]))
    return res


def scenario(name, kind, steps):
    print(f"== {name} ==")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-test-")).resolve()
    root = md.make_root(tmp / "dev", kind)
    srv = md.MockServer(root)
    try:
        steps(srv, root)
    finally:
        srv.close()


OURUI = mp.BIN_MD5["ourui"]


def stock_a(srv, root):
    orig_sh = md5f(root / "customer/demo.sh")
    orig_a = md5f(root / "customer/demo_v9")
    orig_b = md5f(root / "new_customer/demo_v9")

    before = tree_digest(root)
    rc, out = run(srv, root, "install", "--dry-run")
    check("dry-run install exits 0", rc == 0)
    check("dry-run changes nothing on the device", tree_digest(root) == before)

    rc, out = run(srv, root, "install")
    check("install exits 0", rc == 0)
    check("slot B now holds Meraki+", md5f(root / "new_customer/demo_v9") == OURUI)
    check("vendor slot-B backup is byte-identical", md5f(root / "new_customer/ourui/boot/demo_v9.vendor") == orig_b)
    check("demo.sh backup is byte-identical", md5f(root / "new_customer/ourui/boot/demo.sh.bak") == orig_sh)
    check("demo.sh untouched without --gesture", md5f(root / "customer/demo.sh") == orig_sh)
    check("slot A untouched", md5f(root / "customer/demo_v9") == orig_a)
    check("boot flag NOT written without --persist", not (root / "customer/new").exists())
    check("exactly one UI runs, from slot B", [e for _, e in ui(root)] == ["/new_customer/demo_v9"], ui(root))
    check("old stock UI was stopped (SIGSTOP first, then killed)",
          "kill -STOP 600" in (root / "events").read_text() and not (root / "proc/600").exists())
    check("state records original flag=0", "orig_flag=0" in (root / "new_customer/ourui/boot/merakiplus.state").read_text())

    pid_before = ui(root)
    rc, out = run(srv, root, "install")
    check("re-install is idempotent (exit 0)", rc == 0)
    check("re-install: nothing re-uploaded, same UI process", "already" in out and ui(root) == pid_before, out[-400:])

    rc, out = run(srv, root, "install", "--gesture", "--persist")
    check("install --gesture --persist exits 0", rc == 0)
    sh = (root / "customer/demo.sh").read_text()
    check("demo.sh has exactly one guard line", sum(mp.GUARD_KEY in l for l in sh.splitlines()) == 1)
    lines = sh.splitlines()
    gi = next(i for i, l in enumerate(lines) if mp.GUARD_KEY in l)
    check("guard line sits right before the boot slot branch", lines[gi + 1] == mp.ANCHOR and "starting from B" in lines[gi + 2])
    check("only that one line was added", [l for l in lines if mp.GUARD_KEY not in l] ==
          (root / "new_customer/ourui/boot/demo.sh.bak").read_text().splitlines())
    check("switcher + shim installed", md5f(root / "new_customer/ourui/boot/switcher") == mp.BIN_MD5["switcher"]
          and md5f(root / "new_customer/ourui/boot/grab_shim.so") == mp.BIN_MD5["grab_shim.so"])
    check("boot flag set by --persist", (root / "customer/new").exists())
    check("backups were not overwritten", md5f(root / "new_customer/ourui/boot/demo.sh.bak") == orig_sh)

    before = tree_digest(root)
    rc, out = run(srv, root, "revert", "--dry-run")
    check("dry-run revert changes nothing", rc == 0 and tree_digest(root) == before)

    rc, out = run(srv, root, "revert")
    check("revert exits 0", rc == 0)
    check("demo.sh byte-identical to the original", md5f(root / "customer/demo.sh") == orig_sh)
    check("slot B vendor binary restored", md5f(root / "new_customer/demo_v9") == orig_b)
    check("boot flag removed (it was not set originally)", not (root / "customer/new").exists())
    check("switcher removed", not (root / "new_customer/ourui/boot/switcher").exists())
    check("stock UI running from slot A, nothing else", [e for _, e in ui(root)] == ["/customer/demo_v9"], ui(root))
    check("slot A still untouched", md5f(root / "customer/demo_v9") == orig_a)

    rc, out = run(srv, root, "revert")
    check("second revert is a no-op", rc == 0 and "nothing to revert" in out, out[-300:])


def vendor_b(srv, root):
    orig_b = md5f(root / "new_customer/demo_v9")
    rc, out = run(srv, root, "install")
    check("install on a B-booted device exits 0", rc == 0)
    check("activated through the watchdog exec-reload", "watchdog exec-reload" in out)
    check("one UI, Meraki+ from slot B", [e for _, e in ui(root)] == ["/new_customer/demo_v9"]
          and md5f(root / "new_customer/demo_v9") == OURUI)
    check("flag (set by the vendor) left alone", (root / "customer/new").exists())
    rc, out = run(srv, root, "revert")
    check("revert exits 0", rc == 0)
    check("vendor slot B restored", md5f(root / "new_customer/demo_v9") == orig_b)
    check("original flag restored (vendor B boots again)", (root / "customer/new").exists())


def empty_b(srv, root):
    rc, out = run(srv, root, "install")
    check("install with an empty slot B exits 0", rc == 0)
    check("empty slot B recorded", (root / "new_customer/ourui/boot/demo_v9.vendor.absent").exists())
    rc, out = run(srv, root, "revert", "--purge")
    check("revert --purge exits 0", rc == 0)
    check("slot B empty again", not (root / "new_customer/demo_v9").exists())
    check("/new_customer/ourui purged", not (root / "new_customer/ourui").exists())
    check("stock UI running", [e for _, e in ui(root)] == ["/customer/demo_v9"])


def not_a_display(srv, root):
    (root / "customer/test.sh").unlink()
    before = tree_digest(root)
    rc, out = run(srv, root, "install")
    check("unrecognised device: install refuses (exit 1)", rc == 1 and "does not look like" in out)
    check("unrecognised device: nothing changed", tree_digest(root) == before)


if __name__ == "__main__":
    scenario("fresh machine running stock from slot A", "stock_a", stock_a)
    scenario("machine running a vendor update from slot B", "vendor_b", vendor_b)
    scenario("slot B empty", "empty_b", empty_b)
    scenario("not a MerakiTech display", "stock_a", not_a_display)
    print(f"\nINSTALLER TESTS: {'ALL PASS' if not FAILS else str(len(FAILS)) + ' FAIL: ' + ', '.join(FAILS)}")
    sys.exit(1 if FAILS else 0)
