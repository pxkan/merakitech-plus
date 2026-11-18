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

`ourui`, `ourui_sim_arm`, `switcher`, `grab_shim.so`, `tinj_hold` and `keyinj` are the exact builds
that were verified on a real machine (e.g. Meraki+ v6.14: on-device md5 `1dd63f42e4b0d649ef851660fc123a10`),
with only the non-loaded trailer removed — debug info, symbol table and section headers, which
contained the original build machine's file paths. Every loadable segment (all code and data the
kernel maps) is byte-identical to the verified build; only the three ELF-header fields describing the
removed section table differ. That is why the md5 here differs from the on-device md5.

`./build.sh` builds the same sources reproducibly (`-s`, stripped, independent of the build path)
into `build/`. Those builds have the same source and compiler (zig 0.17.1) but a slightly different
link layout than the verified ones, so their md5 differs from `bin/`.
