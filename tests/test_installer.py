#!/usr/bin/env python3
"""End-to-end tests of tools/merakiplus.py (install / re-install / revert / dry-run / migration of
a pre-rename install) against
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


MERAKIPLUS = mp.BIN_MD5["merakiplus"]


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
    check("slot B now holds Meraki+", md5f(root / "new_customer/demo_v9") == MERAKIPLUS)
    check("vendor slot-B backup is byte-identical", md5f(root / "new_customer/merakiplus/boot/demo_v9.vendor") == orig_b)
    check("demo.sh backup is byte-identical", md5f(root / "new_customer/merakiplus/boot/demo.sh.bak") == orig_sh)
    check("demo.sh untouched without --gesture", md5f(root / "customer/demo.sh") == orig_sh)
    check("slot A untouched", md5f(root / "customer/demo_v9") == orig_a)
    check("boot flag NOT written without --persist", not (root / "customer/new").exists())
    check("exactly one UI runs, from slot B", [e for _, e in ui(root)] == ["/new_customer/demo_v9"], ui(root))
    check("old stock UI was stopped (SIGSTOP first, then killed)",
          "kill -STOP 600" in (root / "events").read_text() and not (root / "proc/600").exists())
    check("state records original flag=0", "orig_flag=0" in (root / "new_customer/merakiplus/boot/merakiplus.state").read_text())

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
          (root / "new_customer/merakiplus/boot/demo.sh.bak").read_text().splitlines())
    check("switcher + shim installed", md5f(root / "new_customer/merakiplus/boot/switcher") == mp.BIN_MD5["switcher"]
          and md5f(root / "new_customer/merakiplus/boot/grab_shim.so") == mp.BIN_MD5["grab_shim.so"])
    check("boot flag set by --persist", (root / "customer/new").exists())
    check("backups were not overwritten", md5f(root / "new_customer/merakiplus/boot/demo.sh.bak") == orig_sh)

    before = tree_digest(root)
    rc, out = run(srv, root, "revert", "--dry-run")
    check("dry-run revert changes nothing", rc == 0 and tree_digest(root) == before)

    rc, out = run(srv, root, "revert")
    check("revert exits 0", rc == 0)
    check("demo.sh byte-identical to the original", md5f(root / "customer/demo.sh") == orig_sh)
    check("slot B vendor binary restored", md5f(root / "new_customer/demo_v9") == orig_b)
    check("boot flag removed (it was not set originally)", not (root / "customer/new").exists())
    check("switcher removed", not (root / "new_customer/merakiplus/boot/switcher").exists())
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
          and md5f(root / "new_customer/demo_v9") == MERAKIPLUS)
    check("flag (set by the vendor) left alone", (root / "customer/new").exists())
    rc, out = run(srv, root, "revert")
    check("revert exits 0", rc == 0)
    check("vendor slot B restored", md5f(root / "new_customer/demo_v9") == orig_b)
    check("original flag restored (vendor B boots again)", (root / "customer/new").exists())


def empty_b(srv, root):
    rc, out = run(srv, root, "install")
    check("install with an empty slot B exits 0", rc == 0)
    check("empty slot B recorded", (root / "new_customer/merakiplus/boot/demo_v9.vendor.absent").exists())
    rc, out = run(srv, root, "revert", "--purge")
    check("revert --purge exits 0", rc == 0)
    check("slot B empty again", not (root / "new_customer/demo_v9").exists())
    check("/new_customer/merakiplus purged", not (root / "new_customer/merakiplus").exists())
    check("stock UI running", [e for _, e in ui(root)] == ["/customer/demo_v9"])


def make_legacy(root):
    """Turn a current install (made by the test before) into the pre-rename layout an older
    release left behind: old directory, old staged binary name, old guard line, old binary marker,
    a dev run.sh with the old env prefix, plus language / schedule / settings-backup files."""
    new, old = root / mp.OURDIR.lstrip("/"), root / mp.LEGACY_DIR.lstrip("/")
    new.rename(old)
    (old / pathlib.PurePosixPath(mp.STAGE).name).rename(old / mp.LEGACY_ID)
    fake = b"\x7fELF FAKE PRE-RENAME UI " + mp.LEGACY_MARKS[0].encode() + b"V6.16\n" * 40
    (root / "new_customer/demo_v9").write_bytes(fake)
    for d in (root / "proc").iterdir():           # the running UI is that old build
        if (d / "exe_path").exists() and (d / "exe_path").read_text().endswith("/new_customer/demo_v9"):
            (d / "image").write_bytes(fake)
    sh = root / "customer/demo.sh"
    sh.write_text(sh.read_text().replace(mp.GUARD_LINE, mp.GUARD_LINE.replace(mp.OURDIR, mp.LEGACY_DIR)))
    (old / "lang").write_text("da\n")
    (old / "schedule").write_text("preheat=1\nat=390\n")
    (old / "cfgbak").mkdir(exist_ok=True)
    (old / "cfgbak/config_writes.log").write_text("synthetic\n")
    (old / "run.sh").write_text(f"#!/bin/sh\n{mp.LEGACY_ENV_PREFIX}NOBOOTPUSH=1 ./{mp.LEGACY_ID}\n")


def no_legacy_left(root):
    old_id = mp.LEGACY_ID.encode()
    hits = [str(p) for p in (root / "new_customer").rglob("*") if old_id in p.name.encode()]
    hits += [str(p) for p in (root / "new_customer").rglob("*")
             if p.is_file() and p.name not in ("demo_v9.vendor",) and old_id in p.read_bytes()]
    return hits + (["demo.sh"] if old_id in (root / "customer/demo.sh").read_bytes() else [])


def legacy_upgrade(srv, root):
    orig_sh = md5f(root / "customer/demo.sh")
    orig_b = md5f(root / "new_customer/demo_v9")
    rc, _ = run(srv, root, "install", "--gesture", "--persist")
    check("setup: current install", rc == 0)
    make_legacy(root)
    new = root / mp.OURDIR.lstrip("/")
    check("setup: pre-rename layout in place", not new.exists() and (root / mp.LEGACY_DIR.lstrip("/")).is_dir())

    before = tree_digest(root)
    rc, out = run(srv, root, "install", "--dry-run")
    check("dry-run on a pre-rename install changes nothing", rc == 0 and tree_digest(root) == before, out[-400:])
    check("dry-run reports the migration", "Migrating an install from before the rename" in out)
    check("dry-run finds the old backups (no new vendor backup planned)", "slot-B backup already exists" in out, out[-600:])

    rc, out = run(srv, root, "install")
    check("install over a pre-rename install exits 0", rc == 0)
    check("old directory gone", not (root / mp.LEGACY_DIR.lstrip("/")).exists())
    check("backups moved, byte-identical", md5f(new / "boot/demo_v9.vendor") == orig_b
          and md5f(new / "boot/demo.sh.bak") == orig_sh)
    check("state moved (original flag=0 kept)", "orig_flag=0" in (new / "boot/merakiplus.state").read_text())
    check("language, schedule and settings backups moved",
          (new / "lang").read_text() == "da\n" and (new / "schedule").read_text().startswith("preheat=1")
          and (new / "cfgbak/config_writes.log").exists())
    check("switcher + shim moved", (new / "boot/switcher").exists() and (new / "boot/grab_shim.so").exists())
    check("old staged binary removed, new one staged", not (new / mp.LEGACY_ID).exists()
          and md5f(root / mp.STAGE.lstrip("/")) == MERAKIPLUS)
    check("run.sh rewritten (new env prefix + binary name)",
          (new / "run.sh").read_text() == f"#!/bin/sh\n{mp.ENV_PREFIX}NOBOOTPUSH=1 ./merakiplus\n")
    sh = (root / "customer/demo.sh").read_text().splitlines()
    check("demo.sh: exactly one guard line, the current one", [l for l in sh if "/boot/switcher" in l] == [mp.GUARD_LINE])
    check("demo.sh otherwise unchanged", [l for l in sh if l != mp.GUARD_LINE] ==
          (new / "boot/demo.sh.bak").read_text().splitlines())
    check("slot B upgraded to the current build, running", md5f(root / "new_customer/demo_v9") == MERAKIPLUS
          and [e for _, e in ui(root)] == ["/new_customer/demo_v9"])
    check("no pre-rename identifier left on the device", not no_legacy_left(root), no_legacy_left(root))

    rc, out = run(srv, root, "install")
    check("second install after the migration is idempotent", rc == 0 and "Migrating" not in out)
    rc, out = run(srv, root, "revert")
    check("revert after the migration exits 0", rc == 0)
    check("demo.sh byte-identical to the original", md5f(root / "customer/demo.sh") == orig_sh)
    check("vendor slot B restored", md5f(root / "new_customer/demo_v9") == orig_b)


def legacy_revert(srv, root):
    orig_sh = md5f(root / "customer/demo.sh")
    orig_b = md5f(root / "new_customer/demo_v9")
    rc, _ = run(srv, root, "install", "--gesture", "--persist")
    check("setup: current install", rc == 0)
    make_legacy(root)

    before = tree_digest(root)
    rc, out = run(srv, root, "revert", "--dry-run")
    check("dry-run revert on a pre-rename install changes nothing", rc == 0 and tree_digest(root) == before)

    rc, out = run(srv, root, "revert")
    check("revert of a pre-rename install exits 0", rc == 0)
    check("old build recognised by its legacy marker and stopped", "stock UI running again" in out, out[-600:])
    check("demo.sh byte-identical to the original (old guard line removed)", md5f(root / "customer/demo.sh") == orig_sh)
    check("vendor slot B restored from the moved backup", md5f(root / "new_customer/demo_v9") == orig_b)
    check("boot flag removed", not (root / "customer/new").exists())
    check("stock UI running from slot A", [e for _, e in ui(root)] == ["/customer/demo_v9"], ui(root))
    check("old directory migrated", not (root / mp.LEGACY_DIR.lstrip("/")).exists()
          and (root / mp.OURDIR.lstrip("/") / "lang").exists())


def legacy_purge(srv, root):
    rc, _ = run(srv, root, "install", "--gesture")
    check("setup: current install", rc == 0)
    make_legacy(root)
    (root / mp.OURDIR.lstrip("/")).mkdir()          # both layouts present (interrupted migration)
    (root / mp.OURDIR.lstrip("/") / "lang").write_text("en\n")
    rc, out = run(srv, root, "revert", "--purge")
    check("revert --purge exits 0", rc == 0)
    check("merge kept the newer file", "merged" in out)
    check("both directories purged", not (root / mp.OURDIR.lstrip("/")).exists()
          and not (root / mp.LEGACY_DIR.lstrip("/")).exists())
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
    scenario("upgrade of a pre-rename install (legacy migration)", "stock_a", legacy_upgrade)
    scenario("revert of a pre-rename install (legacy migration)", "stock_a", legacy_revert)
    scenario("revert --purge with both layouts present", "stock_a", legacy_purge)
    print(f"\nINSTALLER TESTS: {'ALL PASS' if not FAILS else str(len(FAILS)) + ' FAIL: ' + ', '.join(FAILS)}")
    sys.exit(1 if FAILS else 0)
