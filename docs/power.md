# POWER-VERIFY — v6.14 power button: on-device evidence + hardware findings (MerakiTech Gen 1)

> **Note (public edition):** Development notes written during development. Paths such as
> `artifacts/…`, `boot_install.py` steps and md5 tables refer to the author's working environment; as
> an end user, use `./install.sh` and `./revert.sh` (see the [README](../README.md)). The
> manufacturer's files are not included; IP addresses and other personal data have been removed.

**Date:** 2026-11-18 · **Device:** <display-ip> (Sanbon SAT62091M, kernel 4.9.84) · **Binary:** merakiplus v6.14
`1dd63f42e4b0d649ef851660fc123a10` (LIVE in slot B, see `INSTALL-v614-B.md`)
**Method:** passive strace (`-f -tt -yy -z -e trace=read,write`) on the LIVE process + a lossless
persistent stream (`tail -f | nc -l`) to the workstation; evdev write injection (`keyinj.c`) for a
synthetic button press; framebuffer captures via `fbpage`; reading DIAGNOSTICS. **No reboot, no
config/flag/slot/OTA writes.**

**Conclusion:**
1. ✅ **The POWER-OFF sequence is proven byte for byte on the device** with the real MCU: cmd8
   time/alarm frame (alarm 0/0/0, valid CRC) → the MCU's 0x88 acknowledgement → **cmd6 sub0 len2
   [0,0] = `aa 55 06 00 02 00 00 00 00 56 94`** (the stock UI's exact PowerOff frame).
2. ✅ **The POWER-ON sequence is proven byte for byte on the device**: PowerOn
   `aa 55 06 00 01 01 00 c5 5f` + awake `aa 55 06 01 01 00 00 c5 33` + boot push (`sub1 len30` +
   `sub112 len3`), all ACKed (0x86 with echo), word 0 = 1 afterwards, and the controller's words
   11/14/25/114 = the config's values (870/3/650/70).
3. ⚠️ **BUT: the physical button cannot trigger merakiplus's software path on this device.** Two hardware
   mechanisms cut the display's power: (a) a kernel/PMIC cut on a hold of ≥ ~0.8 s on the k62091
   node (event1) — **before** merakiplus's 1.5 s threshold; (b) the MCU's sleep cuts the display power
   ~8 ms after the PowerOff frame. §19's premise *"merakiplus does not suspend the display, so the
   network/telnet stays up"* and the live OFF screen / TURNING ON… transition are therefore **not
   achievable on this hardware** — power off = power loss, power on = cold boot. The evidence below
   was captured around these cuts.

---

## 1. The hardware finding — chain of evidence

| # | Attempt (UTC) | Trigger | Result | Conclusion |
|---|---|---|---|---|
| 1 | 20:09–20:10 | physical hold ~2 s (live 3963) | screen black/dead; telnet gone; strace log lost in the following boot | a hold kills the display; logging needs a stream that survives the death |
| 2 | 20:40:37→39.44 | physical minimal hold (~1.6 s) | dead at 20:40:39.44; the chunk-spool stream was lossy (lost chunks) | a lossless stream is required |
| 3 | 20:53:48 | **physical short press 0.52 s** | **events are delivered**: event1 code 28 down 20:53:48.103 / up .620; DIAGNOSTICS PRESS·OFF·ON = 1·0·0, LAST BUTTON `28 = 0` | the button node works for short presses; the kernel cut threshold is > 0.52 s |
| 4 | 21:00:00→02.106 | physical minimal hold | dead at 21:00:02.106; **lossless log: no button events, no cmd8/cmd6, only polls + keep-awake** until death | a hold of ≥ ~0.8 s delivers no events to merakiplus → merakiplus never fires |
| 5 | 21:06:46.835→47.664 | **injected** hold 1.6 s, code 28, **event1** | key-down delivered 21:06:46.835228; dead 21:06:47.664409 = **+829 ms**; no key-up, no power-off frames | kernel cut ≈ 0.8 s after key-down, regardless of physical/injected → handler in the input layer on k62091 |
| 6 | 21:12:4x | injected hold 1.6 s, code 28, **event0** | survived (but the events were discarded: keybit) | — |
| 7 | 21:17:56 | injected code **17**, event0 (declared in keybit) | delivered to both fds (touch + kbd) | evdev injection discards codes the node does not declare (event0 keybit low word = 0x20000 → code 17; 28/10 are discarded) |
| 8 | 21:18:13.695→15.282 | injected hold 1.6 s, code 17, **event0** (test mode `MERAKIPLUS_KBD=event0 MERAKIPLUS_NOGRAB=1`) | **merakiplus fires at +1.5 s**: cmd8 → 0x88 → PowerOff; dead **+7.8 ms after PowerOff** | the kernel handler is device-specific (k62091); the MCU sleep cuts the display power almost immediately after PowerOff |
| 9 | 21:21 / 21:24 | physical **short press** after death | **cold boot** (uptime reset, new pid, /tmp emptied) → merakiplus startup = PowerOn + awake + boot push | POWER ON on this hardware = cold boot, not a live resume |

**The kernel cut mechanism:** k62091.ko is stripped (no parameters, `/sys/module/k62091/parameters/`
is empty); the cut lives in the input layer, bound to the k62091 device (attempts 5 vs. 8). It cannot
be disabled without module/kernel changes (not done). **The MCU sleep cut** is the machine's actual
power-off behavior: the display module loses power when the controller sleeps — which is why neither
the stock UI's nor merakiplus's "live OFF screen" can be shown on this device, and telnet/fb are not
available in the off state.

---

## 2. Power-off frames byte for byte (capture: `power_v614_live/attempt5/uart_sluk_window.log`)

Test mode: B launched stop-first with `MERAKIPLUS_KBD=/dev/input/event0 MERAKIPLUS_NOGRAB=1` (watchdog-safe;
no files changed). Button injected on event0 with code 17 (merakiplus counts every EV_KEY code, §19).
Hold 1.6 s: key-down read by the kbd fd at **21:18:13.694905**.

| Time (UTC) | Direction | Frame (byte for byte) | Remark |
|---|---|---|---|
| 21:18:15.213663 | TX | `aa 55 08 ea 07 0b 12 15 12 0f 00 00 00 b5 ca` | **cmd8** time/alarm: year 0x07EA=2026, month 11, day 18, 21:18:15, alarm 0/0/0; CRC ok; = key-down + **1518.8 ms** (merakiplus's HOLD 1.5 s) |
| 21:18:15.227870 | RX | `aa 55 88 ea 07 0b 12 15 12 0f 00 00 00 1c 08` | **0x88** acknowledgement of cmd8 (echo), +14.2 ms |
| 21:18:15.273833 | TX | `aa 55 06 00 02 00 00 00 00 56 94` | **PowerOff = cmd6 sub0 len2 [0,0]** — exactly the stock UI's frame; +60.2 ms after the cmd8 write (= fire + 80 ms, `PW_GAP_MS`; the cmd8 write came 19 ms after fire because of the loop flush) |
| 21:18:15.281584 | — | last syscall in the log | **display power dies +7.8 ms after PowerOff** (MCU sleep cut) |

**0 CRC errors in TX and RX** in the window. No start/stop frames; no other cmd6 writes in the window
(keep-awake stopped from fire, cf. `proto_keepawake`: `pw.state != PW_ON → return`).

**Not observable because of the power loss (documented, not hidden):**
* The PowerOff **ACK** (0x86 sub0 len2): the MCU did not get to reply before the display died (cut at
  +7.8 ms).
* The **word 0 = 0** reply and the silent sleep period: the display dies before the MCU's sleep task
  replies; sleep is instead proven indirectly by the power cut itself + cold boot on power-on (MCU
  reset signature).
* **pwm2/enable = 0** and the **OFF framebuffer**: merakiplus turns off the backlight 1.5 s after fire
  and draws OFF after the ACK — both came after the power cut. (In attempt 1 the user briefly saw a
  black page; it cannot be captured from the framebuffer on this hardware.)
* The live **TURNING ON… → PowerOn → re-push** transition: requires the MCU to wake up while the
  display is alive — impossible here (the MCU only wakes from its own GPIO/alarm; a GPIO hold of
  ≥ 0.8 s cuts the display power).

---

## 3. Power-on frames byte for byte (capture: `power_v614_live/attempt7/uart_boot_window.log`)

Power-on on this hardware = the user's press → cold boot → merakiplus's startup (the same frames as the
stock UI's startup: PowerOn + awake + boot push). Captured from the first write after the watchdog
reload at 21:24:42 (strace armed before the reload; pid 3374):

| Time (UTC) | Direction | Frame (byte for byte) | Remark |
|---|---|---|---|
| 21:24:42.948401 | TX | `aa 55 06 00 01 01 00 c5 5f` | **PowerOn** (word 0 = 1) |
| 21:24:42.995537 | RX | `aa 55 86 00 01 01 00 c4 81` | ACK 0x86 with echo, +47.1 ms; same RX round: word 0 = **1** |
| 21:24:43.004228 | TX | `aa 55 06 01 01 00 00 c5 33` | **awake** (word 1 = 0), +55.8 ms after PowerOn |
| 21:24:43.012338 | RX | `aa 55 86 01 01 00 00 c4 ed` | ACK, +8.1 ms |
| 21:24:43.015686 | TX | `aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 00 00 00 00 00 00 00 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 00 00 68 01 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 62 3e` | **boot push sub1 len30** (words 1..30 from the config) |
| 21:24:43.046006 | RX | `aa 55 86 01 1e … e3 de` | ACK with echo of the whole block, +30.3 ms |
| 21:24:43.051800 | TX | `aa 55 06 70 03 04 01 00 06 46 00 c7 ce` | **boot push sub112 len3** (word 112=0x0104, 113=0x0600, 114=70) |
| 21:24:43.064110 | RX | `aa 55 86 70 03 04 01 00 06 46 00 a6 08` | ACK, +12.3 ms |

**Config restore proven:** the boot push's word 11 = **870** (87.0 °C), word 14 = **3**, word 25 =
**650**, word 114 = **70** — and the controller's subsequent replies (sub0/sub112 polls) show the same
values; DIAGNOSTICS framebuffer: **BOOT PUSH 1-30 OK · 112-114 OK**, CONFIG MD5 **B1F33E23**, AWAKE
(WORD 1) **YES**. After an MCU reset (cold boot) the controller is therefore back on the config's
values — that is the restore that §19's re-push is meant to guarantee on a live wake; here it is
delivered by the boot push at startup. **0 CRC errors.** No start/stop frames; no writes beyond
PowerOn/awake/boot push/keep-awake/polls.

*The re-push path (v6.6, the 0x89 round while merakiplus is running) could not be tested live on this
device:* an MCU reset requires a power cycle or word 30 (forbidden by the frame policy), and a power
cycle kills the display. The code is token-identical to v6.13 (build_v614.sh's FROZEN list:
`repush_step()` unchanged).

---

## 4. Framebuffer and DIAGNOSTICS evidence

| Evidence | File | Contents |
|---|---|---|
| Button node + counters before the test | `v614P_diag_power2.png` | POWER BUTTON: STATE ON · POWER WORD 0 = 1 · PRESS·OFF·ON 0·0·0 · LAST BUTTON NO PRESS YET · **BUTTON NODE /dev/input/event1 · k62091 · NO GRAB** |
| Short press registered | `v614P_diag_efter_kort.png` | PRESS·OFF·ON **1·0·0** · LAST BUTTON **28 = 0 · 26 s** (physical 0.52 s press) |
| Final V6.14 + boot push | `v614P_slut_diag.png` | **VERSION MERAKI+ V6.14** · LINK OK · PHASE READY · BOOT PUSH 1-30 **OK** / 112-114 **OK** · FLAG PRESENT · NEXT BOOT MERAKI+ · STARTED FROM SLOT B · SLOT B MERAKI+ FOUND |
| Final HOME healthy | `v614P_slut_home.png` | READY · 99.5 °C · TARGET 87.0 · 4 icons + gear |
| Install/reload framebuffer | `v614B_diag_version.png` · `v614B_slut_home.png` | VERSION V6.14 after the watchdog reload at 20:04 |
| **OFF framebuffer / backlight off** | **not achievable** | the display power is cut before merakiplus's backlight-off (fire + 1.5 s) and OFF rendering (after the ACK); see §2 |

Backlight before/after the whole round: `pwm2/enable = 1`, `duty_cycle = 50` (measured at 20:57 and
21:26 via telnet; intermediate values in the off state could not be measured because of the power
cut).

---

## 5. Deviations from the expectations of §19 / verify_power_v614.py (honest register)

| §19 expectation | On-device reality | Consequence for the evidence |
|---|---|---|
| HOLD 1.5 s → merakiplus powers off | the kernel cut at ~0.8 s on event1 comes first; merakiplus never sees the hold | power-off proven with an injected hold on event0 (test mode), not the physical button |
| OFF screen shown, backlight off, telnet alive | the display power is cut at PowerOff (+8 ms) | no OFF framebuffer, no enable=0 measurement, no sleep silence in the log; sleep proven by the power cut + cold-boot power-on |
| A NEW HOLD wakes the MCU live → TURNING ON… → PowerOn + re-push | an MCU GPIO hold of ≥ 0.8 s cuts the display power; wake = cold boot | power-on proven as the startup sequence (PowerOn + awake + boot push + ACKs + config words), not as a live re-push |
| cmd8 → ≥ 80 ms → cmd6 (stock `usleep(80000)`) | measured 60.2 ms between the cmd8 **write** and the cmd6 write (merakiplus's 80 ms is counted from fire; the cmd8 write came 19 ms after fire) | order + contents correct; gap deviation explained and documented |
| PowerOff ACKed (0x86) | not seen — the display died +7.8 ms after the frame | the ACK cannot be observed on this hardware; 0x88 for cmd8 was seen |
| run.sh mode (stock SIGSTOPped) usable for the test | B boot: no stock UI to pause (BOOT.md §9.3); B owns the touch grab | passive strace on the live process + lossless stream instead |

**Recommendation:** §19's semantics are mirrored correctly in the code (frames byte-identical to the
stock UI), but **the hardware of this device does not support the live off state**: (a) the kernel
cut at ~0.8 s on k62091 makes any software hold threshold ≥ 0.8 s impossible to trigger physically;
(b) the MCU sleep cuts the display power, so "only the backlight is switched off" cannot be done.
Possible paths (maintainer decision): lower the hold threshold below 0.8 s (e.g. 0.6 s) so merakiplus fires
before the kernel cut — but the display still dies at PowerOff, so the OFF screen remains invisible;
or accept that the button on this hardware revision is a hardware power switch and document §19 as
applying to other hardware revisions. **No code was changed in this round.**

---

## 6. Evidence files

| File | Contents |
|---|---|
| `power_v614_live/power_frames.json` | byte-exact TX frames + RX ACKs for the power-off and boot windows, with timestamps, 0 CRC errors |
| `power_v614_live/attempt5/uart_sluk_window.log` | strace window 21:18:1x (key-down, cmd8, 0x88, PowerOff, death) |
| `power_v614_live/attempt5/uart_stream_final.log` | full lossless stream from test mode (incl. startup 21:15:35+) |
| `power_v614_live/attempt7/uart_boot_window.log` | strace window 21:24:4x (PowerOn → boot push → ACKs) |
| `power_v614_live/attempt7/uart_stream.log` | full stream from the watchdog reload at 21:24 |
| `power_v614_live/attempt3/uart_stream3.log` | lossless stream showing a physical minimal hold **without** events/frames until death at 21:00:02 |
| `power_v614_live/attempt4/uart_stream.log` | injected event1 hold: death +829 ms after key-down |
| `power_v614_live/attempt2/uart_off.log` | (lossy chunk spool; for traceability only — superseded by attempts 3/4/5) |
| `power_v614_live/attempt5/config_words.json` · `attempt7/config_words.json` | the boot push's config words |
| `keyinj.c` / `keyinj` | the injector (evdev write injection; KEYINJ_DEV/KEYINJ_CODE) |
| `power_live_v614.py` | live power driver (passive strace; start/off/on/abort) |
| `boot/ondevice/INSTALL-v614-B.md` | install + reload + final state + revert |

---

*Test agent, 2026-11-18. The power-off and power-on frames are proven byte for byte on the hardware
with the real MCU; on this device the physical button is a hardware power switch (kernel cut ~0.8 s +
MCU sleep cut), so §19's live OFF / TURNING ON… states cannot be reached here. v6.14 is LIVE in slot
B, healthy, with intact backups and a documented revert; no config/flag/slot metadata/vendor OTA
writes, no reboots by the agent.*
