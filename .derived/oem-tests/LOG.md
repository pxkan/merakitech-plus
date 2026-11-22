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

- 1fc0c9b: 5 more real UI bugs (offline hint dead after a brew: stale cst.active; offline hint under the chevron's hit area;
  TITLE_W wider than the chord -> ink under the bezel; exit-reminder title 331 px in F_L; °F hero clipped by its widget;
  system-row subline cut to 200 for no reason).
- 13441e8: selftest rewritten for v7 (compat block gone; KB budgets from draw code; ink_out() circle checks; contrast on bgpure).
- 3ed58c6: st_v7 (roller drag, pull-down, static layer, ring heat, arc labels, glyph engine). EN 556 / DA 556 ALL OK.
- ./build.sh host OK.

## Next
- sim/ (subagent): quick + full E2E EN/DA; then final report
