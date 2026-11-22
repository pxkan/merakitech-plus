# oem-tests — make the test suites correct for the v7.0 OEM design

Branch `oem-tests` (from `oem-ui` 2588b20). Push to origin (pxkan fork) only.

## Verification
- `zig cc -DMERAKIPLUS_PREVIEW -O2 -o build/merakiplus_preview src/merakiplus.c -lm`
- `./build/merakiplus_preview --selftest sim/fixtures/config.json` (+ `--lang da`): ALL OK, >= 545 checks
- `./build.sh host`; `python3.12 sim/selftest.py`; `python3.12 sim/run_e2e.py --all --quick --parallel 4`
- full E2E EN + DA (`--run-name v7-full-en`, `--ui-lang da --run-name v7-full-da`), baseline 45/45 + 45/45

## Start state
- selftest EN: 545 checks, 43 failed (compat block in selftest.c maps v6 names)

## Done
- hit_test: STOP was dead during the 280 ms cross-fade into the live view (tr.type -> T_NONE). Real bug: fixed (19 checks).

## Next
- selftest.c rewrite (me); sim/ (subagent)
