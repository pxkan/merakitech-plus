## What / Hvad

<!-- Short description of the change and why. -->

## Tested / Testet

- [ ] `./build.sh host` (0 warnings in new code)
- [ ] `python3 sim/selftest.py`
- [ ] `python3 sim/run_e2e.py --all --quick --parallel 4`
- [ ] `python3 tests/test_installer.py` (if tools/, install.sh or revert.sh changed)
- [ ] On a real machine (model + what was verified): ...

## Safety checklist / Sikkerhed

- [ ] No vendor firmware, binaries, libraries or scripts added
- [ ] No credentials, IP/MAC addresses, serial numbers or personal paths added
- [ ] Device writes (if any) are backed up, atomic, idempotent and undone by `./revert.sh`
- [ ] The UI only sends frames the stock UI also sends, through the frame policy
- [ ] `bin/MD5SUMS` and `BIN_MD5` in `tools/merakiplus.py` updated (if bin/ changed)
- [ ] CHANGELOG.md / docs updated
