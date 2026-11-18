# GESTURE-FIX v6.12 — simpler center-hold gesture + grab bypass + automatic raw range

> **Note (public edition):** Development notes written during development. Paths such as
> `artifacts/…`, `boot_install.py` steps and md5 tables refer to the author's working environment; as
> an end user, use `./install.sh` and `./revert.sh` (see the [README](../README.md)). The
> manufacturer's files are not included; IP addresses and other personal data have been removed.

**Date:** 2026-11-18 · **Status:** PREPARED LOCALLY — **on-device verification PENDING**
(the machine was switched off by the user; no contact with the device took place during preparation).
**Owner:** the boot work stream (`artifacts/ourui/boot/`). The UI files (`ourui_v6.c`, `UI_v6.md`,
`build_sim.sh`, `REVERT.md`) were **NOT changed**; `sim/mcu_sim.py`, `sim/scenarios/` and
`sim/run_e2e.py` were **NOT changed** (as instructed).

---

## 1. Root cause (gesture debugging, 2026-11-18)

The A→B gesture did **not trigger physically** on the device. Diagnosis up to the pause:

* The touch hardware + driver work (131 IRQs; the stock UI reacts to buttons after an IC reset via
  the driver's sysfs). Orientation CORRECT (the grinder circle top-left opened the grinder).
* The device's ABS_MT raw range is **0..480** — not 4095 as tinj/the sim assumed.
* `/tmp/switcher.log` has **NO event lines ever** (only startup) while the IRQ count moves →
  **hypothesis: the stock `demo_v9` grabs `/dev/input/event0` EXCLUSIVELY (`EVIOCGRAB`)** → the
  read-only switcher never sees events → the A→B gesture is impossible while the stock UI is running
  and grabbing.
* An earlier "gesture passed" (INSTALL-REPORT v6.6/v6.11 §6) was probably **injection while the
  stock UI was SIGSTOPped/not grabbing** — not a physical proof.

Consequence: the gesture fix requires (1) a gesture that can be performed physically without hitting
stock buttons, and (2) a **grab bypass** so the switcher sees events while the stock UI is running.

---

## 2. New gesture specification (v6.12)

| Parameter | Value |
|---|---|
| Gesture | **ONE continuous hold ≥ 5000 ms** in the center of the screen — no multi-press |
| Zone | circle around **(240,240)** with radius **40 px** (the finger must stay inside the zone for the whole hold) |
| Trigger | on **release** after a valid hold (the driver only reports changes; a still hold produces no events in between) |
| Cooldown | 10 s after a trigger (unchanged) |
| Fallback | `SWITCHER_GESTURE=corner` = legacy 3× long press ≥ 1500 ms in x<90, y<90 within 6 s (unchanged logic) |

**Environment configuration** (default = center 5000 ms):

| Env | Default | Effect |
|---|---|---|
| `SWITCHER_GESTURE` | `center` | `corner` selects the corner fallback |
| `SWITCHER_HOLD_MS` | 5000 (center) / 1500 (corner) | minimum hold time |
| `SWITCHER_CENTER_X` / `_Y` | 240 / 240 | gesture center |
| `SWITCHER_CENTER_R` | **40** | gesture radius in px (1..240) |

### 2.1 The center is verified to be non-interactive — but a radius of 60 would collide

Measured on a capture of the stock home screen (`final_A_stock_v611.png`, script:
`measure_stock_home.py`): the four activity buttons (Grinder/Hot Water/Brew/Steam) are circles with
centers ≈ (160,154), (320,154), (160,326), (320,326) and radius ≈ 72 px. **The center (240,240) is
background** (between the buttons) ✓. But the nearest button edge is only **≈ 45 px** from the center:

| Button center | Distance to (240,240) | Edge (r≈72) |
|---|---|---|
| (160,154) | 117.5 px | 45.5 px |
| (320,154) | 117.5 px | 45.5 px |
| (160,326) | 117.5 px | 45.5 px |
| (320,326) | 117.5 px | 45.5 px |

A radius of ~60 px (the task's original suggestion) would therefore **overlap the buttons' touch
area**: a finger drifting 50 px towards a button during the hold would trigger the stock button (the
stock UI receives ALL touches, also during the gesture). That is why the **default radius is 40 px**
(5 px margin to the nearest edge) — still plenty of tolerance for a still hold (typical finger jitter
±5–10 px). The radius can be raised with `SWITCHER_CENTER_R` if the user accepts the drift risk. On
the sleep screen the center is also background (any touch only wakes the screen). The gesture should
be performed from the home/sleep screen; on other stock pages the center may contain navigation
(harmless — actuators are only started from the four buttons with confirmation).

---

## 3. Grab bypass (priority a → b → c, automatic fallback in the switcher)

**(a) Duplicate node (automatic):** the switcher now opens **ALL** touch nodes (`event0..9` with
`ABS_MT_POSITION_X`, names TECHWIN/tw31xx preferred) read-only **at the same time** and reads from
all of them (max 4). If there is a duplicate node that the stock UI does not grab, it delivers events
→ the gesture works without interfering with the stock UI. Both nodes being live is harmless: the
gesture state machine is idempotent with respect to duplicated down/up events, and the cooldown
prevents a double trigger. `EVIOCGABS` also works on a grabbed node (a grab only blocks event
delivery, not ioctls) → the raw range is detected regardless.

**(b) Sysfs fallback (automatic):** the tw31xx driver exposes **`lu_coordinate_x`,
`lu_coordinate_y`, `point_num`** in sysfs (found in the strings of `tw31xx_new.ko`;
`sysfs_create_group` + attribute names). A grab only affects evdev clients — not sysfs. The switcher
automatically scans `/sys/bus/i2c/devices/*/` (override: `SWITCHER_SYSFS_DIR`) and polls every
50 ms. **Blindness detection:** if evdev has delivered 0 events since start while sysfs sees touches
(point_num > 0) for ≥ 3 s, it switches automatically to the sysfs source (logged: "evdev BLIND …
switching to sysfs (bypass b)"). If evdev delivers events again (e.g. after shim + stock restart), it
switches back. `SWITCHER_SOURCE=evdev|sysfs` forces a source; `SWITCHER_SYSFS_SCREEN=1` if the
coordinates are already pixels (default: raw in the detected range — on the device raw 0..480 ≈
pixels, so the difference is ≤ 1 px and harmless for the center zone).

**(c) LD_PRELOAD shim (prepared, used by the verify script if a+b fail):** `grab_shim.c` →
`grab_shim.so` (4500 B, ARM EABI5 hard-float, **freestanding `-nostdlib`**: no DT_NEEDED, no UND
symbols — only raw EABI syscalls (ioctl 54, readlink 85) → loads into the stock glibc/armhf process
without a libc dependency). The shim interposes `ioctl` and returns **0 for `EVIOCGRAB` without a
kernel call** — but **ONLY** in processes whose `/proc/self/exe == /customer/demo_v9` (stock,
including watchdog restarts and the runtime B→A launch). All other ioctls and all other processes
(including ourui in B mode) get an unchanged pass-through → **ourui's own grab is preserved** (no
phantom touches in the SIGSTOPped stock UI). The stock UI believes it is grabbing, but is not → the
read-only switcher sees events.

The export happens in demo.sh's **existing guard line** (exactly ONE line kept):

```sh
[ -x /new_customer/ourui/boot/switcher ] && { [ -f /new_customer/ourui/boot/grab_shim.so ] && export LD_PRELOAD=/new_customer/ourui/boot/grab_shim.so${LD_PRELOAD:+:$LD_PRELOAD}; /new_customer/ourui/boot/switcher & }
```

The `-f` test makes a missing .so harmless; the export is inherited by the `test.sh` watchdog → a
stock UI restarted by the watchdog also gets the shim. `boot_install.py install` automatically
upgrades a legacy guard line (v6.11) to the new one (md5 gates, still ONE line).

**Risk (c):** the stock UI ONLY loses its exclusivity on event0 — evdev still delivers to the stock
UI as before (a grab only takes events away from *other* readers). The stock UI's own input behavior
is unchanged; ourui's grab in B mode is unchanged. If the preload failed in a process, glibc would
reject the process — which is why the verify script loads the shim into `/bin/true` (rc=0 gate)
BEFORE the guard line is changed, and the .so is verified to be self-contained (no UND/NEEDED,
hard-float ABI like the stock `ld-linux-armhf.so.3`).

---

## 4. Raw range alignment (screen ↔ raw identical everywhere)

Common formula (the inverse of tinj.c): `screen = (raw−min)·480/(max−min+1)`;
injection: `raw = px·(max−min+1)/480` (floor → ±1 px rounding, harmless for the zone).

| Component | Range source | Fallback | Status |
|---|---|---|---|
| switcher (device) | `EVIOCGABS` (ST+MT, MT takes precedence) when opening each node | **0..480** (device constant) | ✅ automatic |
| switcher (file mode) | `SWITCHER_RAW_MAX` | **0..480** (was 4095) | ✅ device default; sim-fed streams pass 4095 |
| tinj / tinj_hold | `EVIOCGABS` | **0..480** (was 4095) | ✅ automatic + rebuilt |
| ourui (device) | `EVIOCGABS` in `inp_open` (already present, proven rc=0 on the device) | 4095 (only if ALL ioctls fail — does not happen on the device) | ✅ source unchanged (UI parity gate; the SIM declares 0..4095) |
| ourui (SIM) | declared 0..4095 (SIM HAL, no ioctl) | — | ✅ unchanged |
| sim/proto.py | `touch_raw(px, rmin, rmax)`; **`DEVICE_RAW_MAX = 480`** added as a constant | default 4095 = the SIM's declared range | ✅ the sim keeps its range |

`pick_ranges()` (pure logic) is unit-tested: no ioctl replies → 0..480; MT takes precedence over ST;
a 4095 reply → 4095 (automatic detection works both ways).

---

## 5. Local verification (machine OFF — everything run locally, 0 warnings)

| Gate | Result |
|---|---|
| `boot/build.sh` (ARM static switcher/tinj_hold + ARM .so + host builds, `-Wall -Wextra -Werror`) | ✅ 0 warnings |
| `grab_shim.so` ELF gate: no DT_NEEDED, no UND symbols, only `ioctl` exported, hard-float | ✅ (build.sh gate) |
| `switcher_selftest` (center hold, limits, drift, corner fallback, scaling for both ranges, pick_ranges, sysfs samples, env, mode logic) | ✅ **50/50** |
| `grab_shim_selftest` (decision matrix + live ioctl no-op/pass-through) | ✅ **13/13** |
| `test_switcher.py` (file-mode e2e: center positive/negative×3, raw-4095 override, corner positive/negative, center-vs-corner, env override) | ✅ **ALL GREEN (9 gates)** |
| `boot_install.py` mock: setup/backup/install/boot-test/revert-test (new mock root `mock_root_v612`) | ✅ all gates |
| Legacy upgrade: simulated v6.11 device (demo.sh `53cfde05…` + state) → install upgrades the guard line → **same installed md5 `b295aa11…` as a fresh install**, exactly ONE line, placement gates | ✅ |
| tinj rebuild (ARM static) | ✅ |

Evidence: `boot/test-logs/` (selftest.log, v612_*.evdev/.switcher.log), `boot/install.log`,
`boot/mock_root_v612/`.

### MD5 (built locally 2026-11-18, v6.12)

| File | md5 |
|---|---|
| `boot/switcher` (ARM static) | `e6bcb460ddeec8504e45d664641cc4b3` |
| `boot/tinj_hold` (ARM static) | `9dfb369f3bc6ae697a13225769844591` |
| `boot/grab_shim.so` (ARM .so, 4500 B) | `62c4ae551bb95ccb5c8f213c351a6207` |
| `boot/evprobe` (ARM static, unchanged source) | `beac2fce98108e89b8f7402b54d5f57a` |
| `tinj` (ARM static, rebuilt with the 480 fallback) | `294650eb40081acd3708aeedadd3918a` |
| `boot/switcher_selftest` (host) | `4f137fdd96200e7a520e7169bed395af` |
| `boot/switcher_host` (host) | `8704444e226699cea5cea6eefc9127d6` |
| `boot/grab_shim_selftest` (host) | `67c88f7b333338741ced5144d5754b13` |
| `boot/demo.sh.new` (vendor copy + new guard line) | `b295aa11f2a66593e50ff810a6b402c4` |
| `ourui_v6` (the UI's v6.11, **unchanged**) | `725386ccadade594275f20cc998885eb` |

Device md5s (v6.11 final state, for comparison during verification): switcher `6222d8c2…`,
tinj_hold `32d04919…`, demo.sh with legacy guard `b8e3a47a…`, vendor original `c1ed3d66…`.

---

## 6. On-device verification (WHEN THE MACHINE IS SWITCHED ON) — PENDING

Run (user at the machine, stock A idle):

```sh
cd artifacts/ourui/boot/ondevice
python3 verify_gesture_v612.py          # --plan shows the plan without touching the device
```

The script (`verify_gesture_v612.py`, harness patterns from net_heal_proof_v611: short commands,
pushed scripts, md5 round trips):

1. **Preflight:** stock A exe link + config baseline md5; the user confirms the machine is idle.
2. **Push:** switcher/tinj_hold/grab_shim.so/evprobe → `/new_customer/ourui/boot/` (md5 gates).
3. **Node inventory:** `evprobe info` → touch nodes + raw ranges (expected 0..480).
4. **Grab diagnosis:** `evprobe cap` on all touch nodes at the same time (6 s) while the user touches
   the screen → which nodes deliver events while the stock UI is running?
5. **Sysfs probe:** `point_num/lu_coordinate_x/y` samples (24×250 ms) while the user holds the
   center → live?
6. **Bypass selection by priority:** (a) a duplicate/ungrabbed node delivers → switcher automatic;
   (b) sysfs live → switcher automatic fallback; (c) otherwise the shim: `/bin/true` load test →
   guard line upgraded atomically (backup `demo.sh.bak.v611`, md5 gates, ONE line) → one controlled
   `kill` of the stock UI (the watchdog restarts it WITH the shim env; fallback launch after 20 s) →
   `/proc/<pid>/maps` must show `grab_shim.so`.
7. **Switcher restart** (new binary); the startup log must show CENTER hold + sources.
8. **The USER holds the center ≥ 5 s** → A→B is verified: `GESTUS UDLØST` + `skift A→B FÆRDIG` in
   switcher.log + exe link `/new_customer/demo_v9` (no reboot).
9. **The USER taps SWITCH NOW** (gear → SYSTEM → SWITCH TO ORIGINAL NOW → SWITCH NOW, physically) →
   B→A is verified: exe link `/customer/demo_v9` + boot_ui.log.
10. **Final gates:** config md5 untouched, flag gone, switcher running, `/tmp/ourui_g` cleaned up.
11. Writes `ondevice/gestus_v612/{result.json,GESTUS-VERIFY-v612.md,telnet_transcript.txt}` and
    updates the marker section below.

No reboot, no writes to config/flag/UART/fb; device writes only for the boot binaries, `/tmp/ourui_g`
and (only with bypass c) demo.sh + one stock kill with watchdog fallback.

<!-- ONDEVICE-RESULT:BEGIN -->
**Status:** VERIFIED · **Bypass:** a_single_node_ungrabbed · **Date:** 2026-11-18

On-device verification: ALL GATES GREEN (`GESTUS-VERIFY-v612.md`, `result.json`).
<!-- ONDEVICE-RESULT:END -->

---

## 7. REVERT (undo the grab bypass / gesture fix)

**Bypass a/b (no demo.sh change):**
```sh
kill $(pidof switcher) 2>/dev/null
rm -f /new_customer/ourui/boot/switcher /new_customer/ourui/boot/grab_shim.so
# (the guard line is harmless without the binary: the [ -x … ] gate; for the legacy line without
#  the shim export, if it was upgraded: see below)
kill $(pidof demo_v9)     # the watchdog restarts the stock UI without preload (only needed for c)
```

**Bypass c (shim + guard line):**
```sh
# 1. demo.sh back to the v6.11 state (legacy guard, WITHOUT the shim export):
cp /new_customer/ourui/boot/demo.sh.bak.v611 /customer/.demo_sh.tmp
md5sum /customer/.demo_sh.tmp     # must be b8e3a47a7cc3f222066154ce82f85ee9
mv /customer/.demo_sh.tmp /customer/demo.sh && chmod 755 /customer/demo.sh && sync
# 2. remove the shim + the switcher:
rm -f /new_customer/ourui/boot/grab_shim.so
kill $(pidof switcher) 2>/dev/null
# 3. the stock UI is restarted without LD_PRELOAD (the watchdog inherits demo.sh's env):
kill $(pidof demo_v9)             # or reboot
```

**Full revert (all boot persistence gone, byte-identical vendor state):**
`python3 boot_install.py revert-test` (BOOT.md §5.1) — restores demo.sh from `demo.sh.bak` (vendor
original `c1ed3d66…`), kills the switcher, removes the flag, reboots, restores vendor B. Afterwards
neither the guard line, the shim nor the switcher is present.

> End users: `./revert.sh` does all of this for you (see the [README](../README.md#remove-meraki-revert)).

**Only the gesture back to the legacy corner (keep the install):** start the switcher with
`SWITCHER_GESTURE=corner` (env in the guard line or manual start) — the center gesture is the
default, but the corner logic is unchanged and tested.

---

## 8. Impact on the sim + pending items for the sim owner

* `sim/play.py`: gesture API/smoke test updated to v6.12 (center hold 5.2 s + corner fallback as two
  checks; `SWITCHER_RAW_MAX=4095`/`SWITCHER_GESTURE` are set temporarily around
  `run_e2e.run_gesture`, which inherits `os.environ`).
* `sim/proto.py`: `DEVICE_RAW_MAX = 480` added; `touch_raw`/`touch_stream` keep the SIM's declared
  0..4095 (unchanged behavior).
* **`sim/run_e2e.py` + `sim/scenarios/` were NOT changed** (as instructed). Their gesture step
  (`run_gesture`: 3×1.6 s at (75,75), raw 4095, no env) expects the OLD default and will fail
  against the v6.12 switcher. **Pending for the sim owner:** either set `SWITCHER_GESTURE=corner` +
  `SWITCHER_RAW_MAX=4095` in `run_gesture`'s env (a one-line fix) or rewrite the scenarios to a
  center hold (1×5200 ms at (240,240)). Until then the gesture scenarios are known-red against v6.12;
  the rest of the suite is unaffected (UI/protocol routes use the SIM's own 4095 range).
* `ourui_v6.c` was not changed: the device path already auto-detects the raw range via `EVIOCGABS`
  (proven on the device), and the UI's `build_sim.sh` parity gate would flag any change. The 4095
  fallback there only applies if all four ioctls fail (does not happen on the device). A future UI
  round may set the fallback to 480.

---

## 9. Deliverables (v6.12)

| File | Contents |
|---|---|
| `boot/switcher.c` + `boot/switcher` | center-hold gesture + multi-node (a) + sysfs fallback (b) + automatic raw range |
| `boot/grab_shim.c` + `boot/grab_shim.so` + `boot/grab_shim_selftest` | LD_PRELOAD bypass (c), freestanding ARM .so |
| `boot/tinj_hold.c` + `boot/tinj_hold`, `tinj.c` + `tinj` | 480 fallback + `gesture center/corner` |
| `boot/build.sh`, `boot/test_switcher.py` | builds + local gates (50+13+9) |
| `boot/boot_install.py`, `boot/demo.sh.new` | new guard line (ONE line, shim export) + legacy upgrade |
| `boot/measure_stock_home.py` | geometry measurement of the stock home screen (radius rationale) |
| `boot/ondevice/verify_gesture_v612.py` | on-device verification (run when the machine is switched on) |
| `boot/GESTUS-FIX.md` | this document |
| `boot/mock_root_v612/`, `boot/test-logs/v612_*` | local evidence |

*Boot work stream, 2026-11-18. The machine is switched off in stock A (v6.11 installed in B); v6.12
was built and tested locally and is waiting for on-device verification.*
