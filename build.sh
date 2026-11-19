#!/bin/sh
# build.sh — builds every Meraki+ binary from source with zig (no other toolchain needed).
#
#   ./build.sh            device + tools + host sim + self-tests  ->  build/
#   ./build.sh host       only what CI needs (host sim, switcher_host, self-tests)
#
# Device binaries are static ARMv7 (arm-linux-musleabihf, Cortex-A7) and stripped (-s): the
# unstripped debug info would otherwise embed the absolute build path of whoever built them.
set -eu
cd "$(dirname "$0")"
MODE=${1:-all}
command -v zig >/dev/null 2>&1 || { echo "build.sh: zig not found (https://ziglang.org/download/ — tested with 0.17.1)" >&2; exit 1; }

mkdir -p build
ARM="-target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -s"
# -fno-unwind-tables: otherwise zig leaves an UND __aeabi_unwind_cpp_pr1 in the .so
# (exidx personality) which glibc's ld.so cannot resolve -> LD_PRELOAD failure in stock.
ARMSO="-target arm-linux-musleabihf -mcpu=cortex_a7 -shared -fPIC -nostdlib -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions -O2 -s"
WERR="-Wall -Wextra -Werror"
say(){ printf '== %s\n' "$*"; }

say "host: merakiplus_sim (SIM build, used by sim/run_e2e.py)"
zig cc -DMERAKIPLUS_SIM -O2 -o build/merakiplus_sim src/merakiplus.c -lm
say "host: switcher_selftest + switcher_host + grab_shim_selftest"
zig cc -DSWITCHER_SELFTEST -O2 $WERR -o build/switcher_selftest boot/switcher.c
zig cc -O2 $WERR -o build/switcher_host boot/switcher.c
zig cc -DGRAB_SHIM_SELFTEST -DSHIM_HOST -O2 $WERR -o build/grab_shim_selftest boot/grab_shim.c

if [ "$MODE" != host ]; then
  say "device (ARMv7 static): merakiplus, merakiplus_sim_arm"
  zig cc $ARM -o build/merakiplus src/merakiplus.c
  zig cc $ARM -DMERAKIPLUS_SIM -o build/merakiplus_sim_arm src/merakiplus.c
  say "device (ARMv7 static): switcher, tinj_hold, keyinj, tinj, fbpage, evprobe"
  zig cc $ARM $WERR -o build/switcher boot/switcher.c
  zig cc $ARM $WERR -o build/tinj_hold boot/tinj_hold.c
  zig cc $ARM -o build/keyinj src/keyinj.c
  zig cc $ARM -o build/tinj src/tinj.c
  zig cc $ARM -o build/fbpage src/fbpage.c
  zig cc $ARM -o build/evprobe boot/evprobe.c
  say "device: grab_shim.so (freestanding LD_PRELOAD shim, no DT_NEEDED)"
  zig cc $ARMSO $WERR -o build/grab_shim.so boot/grab_shim.c
  python3 - build/grab_shim.so <<'PY'
# grab_shim.so must be self-contained: no DT_NEEDED and no undefined symbols,
# otherwise glibc's ld.so refuses the LD_PRELOAD in the stock process.
import struct, sys
d = open(sys.argv[1], 'rb').read()
e_shoff, = struct.unpack_from('<I', d, 32)
es, en, ex = struct.unpack_from('<HHH', d, 46)
secs = [struct.unpack_from('<10I', d, e_shoff + i * es) for i in range(en)]
st = secs[ex]; strtab = d[st[4]:st[4] + st[5]]
nm = lambda n: strtab[n:strtab.index(b'\0', n)].decode()
by = {nm(s[0]): s for s in secs}
sy = by['.dynsym']
sym = lambda i: struct.unpack_from('<IIIBBH', d, sy[4] + i * sy[9])
und = [i for i in range(sy[5] // sy[9]) if sym(i)[5] == 0 and sym(i)[0]]
need = []; dy = by['.dynamic']
for i in range(dy[5] // dy[9]):
    tag, val = struct.unpack_from('<II', d, dy[4] + i * 8)
    if tag == 1: need.append(val)
    if tag == 0: break
assert not und and not need, f"grab_shim.so has UND/NEEDED: {und} {need}"
print("   grab_shim.so: no DT_NEEDED, no undefined symbols")
PY
fi

say "self-tests (switcher gesture/mode logic, grab shim)"
for t in switcher_selftest grab_shim_selftest; do
  ./build/$t > build/$t.log 2>&1 || { cat build/$t.log; echo "build.sh: $t FAILED" >&2; exit 1; }
  printf '   %s: %s\n' "$t" "$(tail -n 1 build/$t.log)"
done

say "done -> build/"
