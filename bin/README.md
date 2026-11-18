# bin/ — prebuilt Meraki+ binaries (our code only)

| File | What | Built from |
|---|---|---|
| `ourui` | **Meraki+ v6.14** device binary (static ARMv7, musl). Installed as `/new_customer/demo_v9`. | `src/ourui_v6.c` |
| `switcher` | gesture switcher daemon (center-hold 5 s → stock to Meraki+), only with `--gesture` | `boot/switcher.c` |
| `grab_shim.so` | tiny LD_PRELOAD shim so the switcher can see touches while the stock UI runs (only with `--gesture`) | `boot/grab_shim.c` |
| `tinj_hold` | touch injector used for on-device testing of the gesture | `boot/tinj_hold.c` |
| `keyinj` | key injector used for on-device testing of the power button | `src/keyinj.c` |
| `ourui_sim_arm` | SIM build (no hardware) as static ARMv7 | `src/ourui_v6.c -DOURUI_SIM` |
| `ourui_sim-linux-x86_64`, `ourui_sim-macos-arm64` | SIM builds for running `sim/` without zig | `src/ourui_v6.c -DOURUI_SIM` |

`MD5SUMS` lists every file; `tools/merakiplus.py` refuses to install anything whose md5 differs.
**No vendor firmware, libraries or scripts are included here or anywhere in this repository.**

## Provenance of the ARM binaries

**Since 2026-11-19 (English source text)** `ourui`, `ourui_sim_arm`, `switcher`, `tinj_hold` and
`keyinj` are plain `./build.sh` builds (zig 0.17.1, static, stripped with `-s`, so no build paths are
embedded) of the sources in this repository. The only source change since the on-device-verified v6.14
builds is that comments and developer log/console text were translated from Danish to English (the
EN/DA UI string table is unchanged); the code paths are the same, except that ourui's DIAGNOSTICS
switcher-log reader also recognises the switcher's new English log lines. These binaries therefore have
not been re-verified on a machine yet; behaviour is covered by the UI self-test (EN + DA), the switcher
self-test and the simulator E2E suite.

| File | md5 (this repo) | Built with |
|---|---|---|
| `ourui` | `eb4d9360756e391df4cc95f92b875c90` | `./build.sh` → `build/ourui` |
| `ourui_sim_arm` | `24c94f930935c7a276e70307c1f48882` | `./build.sh` → `build/ourui_sim_arm` |
| `switcher` | `de0d4920e4e80be32e3a70798886a3c0` | `./build.sh` → `build/switcher` |
| `tinj_hold` | `795e5d66bc22a724da89ccce6c9c95b1` | `./build.sh` → `build/tinj_hold` |
| `keyinj` | `645d223205a66b20d16cb408f03a73a4` | `./build.sh` → `build/keyinj` |
| `ourui_sim-linux-x86_64` | `9bf34ba7c9e076289118a06d050b3923` | `zig cc -target x86_64-linux-musl -DOURUI_SIM -O2 -static -s src/ourui_v6.c -lm` |
| `ourui_sim-macos-arm64` | `966ba0e9a89b7ad3e873ebedf9d7d483` | `zig cc -DOURUI_SIM -O2 -s src/ourui_v6.c -lm` (Mach-O builds are not bit-reproducible) |

The ARM builds above are reproducible: running `./build.sh` on the same sources with zig 0.17.1 gives
the same md5, independent of the build directory.

`grab_shim.so` (`c42076c807c54d477a5868977404186d`) is still the exact build that was verified on a real
machine: its source change was comments/self-test text only and compiles to the same `.so` as before.
Like the earlier verified builds it has only the non-loaded trailer removed (debug info, symbol table
and section headers, which contained the original build machine's file paths); every loadable segment
is byte-identical to the verified build, which is why its md5 differs from a `./build.sh` build.

Earlier history: the v6.14 release shipped the on-device-verified builds (on-device md5 of `ourui`
`1dd63f42e4b0d649ef851660fc123a10`, shipped trimmed as `8d084f10094305e4be3701bc10fce5de`).
