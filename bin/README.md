# bin/ — prebuilt Meraki+ binaries (our code only)

| File | What | Built from |
|---|---|---|
| `merakiplus` | **Meraki+ v6.17** device binary (static ARMv7, musl). Installed as `/new_customer/demo_v9`. | `src/merakiplus.c` |
| `switcher` | gesture switcher daemon (center-hold 5 s → stock to Meraki+), only with `--gesture` | `boot/switcher.c` |
| `grab_shim.so` | tiny LD_PRELOAD shim so the switcher can see touches while the stock UI runs (only with `--gesture`) | `boot/grab_shim.c` |
| `tinj_hold` | touch injector used for on-device testing of the gesture | `boot/tinj_hold.c` |
| `keyinj` | key injector used for on-device testing of the power button | `src/keyinj.c` |
| `merakiplus_sim_arm` | SIM build (no hardware) as static ARMv7 | `src/merakiplus.c -DMERAKIPLUS_SIM` |
| `merakiplus_sim-linux-x86_64`, `merakiplus_sim-macos-arm64` | SIM builds for running `sim/` without zig | `src/merakiplus.c -DMERAKIPLUS_SIM` |

`MD5SUMS` lists every file; `tools/merakiplus.py` refuses to install anything whose md5 differs.
**No vendor firmware, libraries or scripts are included here or anywhere in this repository.**

## Provenance of the ARM binaries

**v6.17 (2026-11-20, UX audit + IA consolidation):** `merakiplus`, `merakiplus_sim_arm` and the two host SIM
builds were rebuilt from the v6.17 source. The change is display and navigation only — one page per function
behind its HOME icon, SYSTEM in sections, DIAGNOSTICS behind SYSTEM, shorter texts, a larger STOP button (see
CHANGELOG / docs/ux-audit-v617.md). No UART frame, guard, limit or timing changed: the self-test proves that
HOLD TO START on a page queues byte for byte the same frames as the icon hold, and the frame-vector, policy,
boot-push, STOP, power and scheduling sections of the self-test are unchanged and green. `switcher`,
`tinj_hold`, `keyinj` and `grab_shim.so` are byte-identical to the previous build. **Not yet verified on a
machine** (install deferred); covered by the UI self-test (EN + DA, also as a 32-bit ARM binary), the
simulator quick E2E suite and the new `ia_navigation`, `feature_page_start` and `activity_takes_home` scenarios.

**Rebrand (2026-11-19, still V6.16):** the files were renamed (`merakiplus*`, formerly named after the
internal code name) and `merakiplus`, `merakiplus_sim_arm`, `switcher` and the two host SIM builds were
rebuilt with `./build.sh` from the renamed sources. The only differences are renamed strings (install
directory `/new_customer/merakiplus/`, `MERAKIPLUS_*` variables, the `@(#)merakiplus ` marker, log names
and the displayed `merakiplus/lang` / `merakiplus/schedule` paths); control logic and frames are unchanged.
`grab_shim.so`, `tinj_hold` and `keyinj` are byte-identical (their sources compile to the same output).
The pre-rename V6.16 builds were `2d10173cffb99807d7144619099b49b7` (device UI), `34e64f3b9705e5556e6e09bcbafa3909`
(ARM SIM) and `de0d4920e4e80be32e3a70798886a3c0` (switcher). **Not yet verified on a machine.**

**v6.16 (2026-11-19, time & scheduling):** `merakiplus`, `merakiplus_sim_arm` and the two host SIM builds were
rebuilt from the v6.16 source: the clock on HOME/DIAGNOSTICS, the corrected TIME FORMAT (`timemode` 1 =
24 h, as the stock UI), the planned pre-heat (the stock cmd8 wake alarm, sent at power-off and pre-armed
while on) and the optional auto off (see CHANGELOG / docs/time-scheduling.md). The only new UART frame is
cmd8, which the stock UI and v6.14 already send. `switcher`, `tinj_hold`, `keyinj` and `grab_shim.so` are
byte-identical to the previous build. **Not yet verified on a machine** (install deferred); covered by the
UI self-test (EN + DA, also as a 32-bit ARM binary), the simulator quick E2E suite and the new
`time_display`, `timemode_stock_parity` and `schedule_prewarm` scenarios.

**v6.15 (2026-11-19, graph reset on activity start):** `merakiplus`, `merakiplus_sim_arm` and the two host SIM
builds were rebuilt from the v6.15 source. The only code change since the build below is display-only:
the HOME graph's ring buffer is cleared when an activity starts (see CHANGELOG / docs/calibration.md
§14.7); control logic, frames and thresholds are unchanged. `switcher`, `tinj_hold` and `keyinj` are
byte-identical to the previous build. **Not yet verified on a machine** (install deferred); covered by
the UI self-test (EN + DA, also as a 32-bit ARM binary), the simulator quick E2E suite and the new
`graph_reset` scenario.

**Since 2026-11-19 (English source text)** `merakiplus`, `merakiplus_sim_arm`, `switcher`, `tinj_hold` and
`keyinj` are plain `./build.sh` builds (zig 0.17.1, static, stripped with `-s`, so no build paths are
embedded) of the sources in this repository. The only source change since the on-device-verified v6.14
builds is that comments and developer log/console text were translated from Danish to English (the
EN/DA UI string table is unchanged); the code paths are the same, except that merakiplus's DIAGNOSTICS
switcher-log reader also recognises the switcher's new English log lines. These binaries therefore have
not been re-verified on a machine yet; behaviour is covered by the UI self-test (EN + DA), the switcher
self-test and the simulator E2E suite.

| File | md5 (this repo) | Built with |
|---|---|---|
| `merakiplus` (V6.17) | `532e4bd5aa06e625e3b2c80ace5921f4` | `./build.sh` → `build/merakiplus` |
| `merakiplus_sim_arm` (V6.17) | `dcb74a9407f2989b995527c75784ad7f` | `./build.sh` → `build/merakiplus_sim_arm` |
| `switcher` | `a5e0128bdd77ff5697df2100e889604a` | `./build.sh` → `build/switcher` |
| `tinj_hold` | `795e5d66bc22a724da89ccce6c9c95b1` | `./build.sh` → `build/tinj_hold` |
| `keyinj` | `645d223205a66b20d16cb408f03a73a4` | `./build.sh` → `build/keyinj` |
| `merakiplus_sim-linux-x86_64` (V6.17) | `5470e1289839db0fe1b83cec9765cd1f` | `zig cc -target x86_64-linux-musl -DMERAKIPLUS_SIM -O2 -static -s src/merakiplus.c -lm` |
| `merakiplus_sim-macos-arm64` (V6.17) | `ddbb0ef2a6443c45a1bddfec5b1cbb16` | `zig cc -DMERAKIPLUS_SIM -O2 -s src/merakiplus.c -lm` (Mach-O builds are not bit-reproducible) |

The ARM builds above are reproducible: running `./build.sh` on the same sources with zig 0.17.1 gives
the same md5, independent of the build directory.

`grab_shim.so` (`c42076c807c54d477a5868977404186d`) is still the exact build that was verified on a real
machine: its source change was comments/self-test text only and compiles to the same `.so` as before.
Like the earlier verified builds it has only the non-loaded trailer removed (debug info, symbol table
and section headers, which contained the original build machine's file paths); every loadable segment
is byte-identical to the verified build, which is why its md5 differs from a `./build.sh` build.

Earlier history: the V6.16 builds (after the rename) were `737db9b97dcea27764c682381b70dff2` (`merakiplus`) and
`9c3f1fdb3692283a4c9e1cbaebc05a56` (`merakiplus_sim_arm`). The translated-source v6.14 build of `merakiplus` was `eb4d9360756e391df4cc95f92b875c90`
(`merakiplus_sim_arm` `24c94f930935c7a276e70307c1f48882`). The v6.14 release shipped the on-device-verified builds (on-device md5 of `merakiplus`
`1dd63f42e4b0d649ef851660fc123a10`, shipped trimmed as `8d084f10094305e4be3701bc10fce5de`).
