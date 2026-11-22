# Changelog

All notable changes to Meraki+. Details (with evidence, frames and test figures) are in
[docs/dev/UI_v6.md](docs/dev/UI_v6.md) under *CHANGES-v6.x*. All dates are 2026.

## [7.0] — 2026-11-22
**The OEM redesign.** Meraki+ now looks like the machine's own UI, only better crafted, and keeps all of its power,
revealed step by step instead of crammed onto HOME. Built on v6.20 (the review fixes). **Display and navigation only:** the model, the guards, the save
path (MCU write + ACK, then config.json), the frame policy, the activity recipes, STOP, the boot push, keep-awake and
every other thing that reaches the UART are the same code as before. Before/after: [docs/design/](docs/design/).
### Changed
- **The look.** A copper background with a fine guilloche (pre-rendered once), white on copper everywhere; colour is
  reserved for what must not be missed: errors switch to a dark espresso layer with a red ring, blocked starts carry a
  warning icon in pale yellow.
- **Real typography.** Montserrat (SIL OFL 1.1) in four weights, baked into `src/mp/font_data.c` by the new
  `tools/gen_font.py` as TrueType outlines + GPOS kerning and rasterized on the display with exact-area coverage
  (glyph cache with ¼-px phases, tabular digits). Text is drawn as written: the string table is Title Case / sentence
  case in English and sentence case in Danish (was all caps with letter spacing). `build.sh` checks the generated file.
- **HOME = the stock 2×2 ring buttons** (Grinder, Hot Water / Brew, Steam) with line icons, each ring's name set along its
  arc and its target inside ("18.5 g", "36 g", "65°"). A ring whose boiler is heating fills up as an arc. Tap = the
  feature's page, hold = start (an arc sweeps around inside the ring). The chevron at the top (tap, or pull down from the
  top) opens SYSTEM — the gear is gone; the clock sits at the bottom; a one-line status under the chevron says only what
  matters now (heating, blocked, no connection, CRC errors, a planned pre-heat).
- **The live view** while something runs: the phase name, the big white weight (or milk temperature), a thin progress
  ring at the edge of the screen (weight / target, milk / target), a fine trace, three numbers (time, ≈bar, flow …) and
  a solid white **STOP** pill — the brightest thing on the screen. The DONE screen stays for 8 s as before.
- **Pages and settings:** white rows on copper with hairlines; HOLD TO START is a ring button with a play icon. The
  editor is a **roller** like the stock pickers: drag it, tap or hold −/+ (repeat + acceleration as before), OK saves;
  the chevron, a pull down or a swipe right cancels.
- **Every other screen** in the same language: SYSTEM, the system action (BOOT UI / SWITCH TO ORIGINAL NOW), DIAGNOSTICS,
  the splash (with a ring sweep), the power-off pages (dark layer), errors, offline (dimmed rings), sleep, toasts.
### Added
- **Sim Studio** (`sim/studio.sh`): the real UI compiled to WebAssembly with a tunable JS machine model, in the browser
  at ≥ 30 fps with real pointer input; scenarios, a control drawer, a UART log, PNG export and a side-by-side compare
  with v6.19. See [sim/README.md](sim/README.md#sim-studio).
- `third_party/montserrat/` (the four TTFs + OFL), `tools/gen_font.py`, `tools/previews.py` (preview frames → PNG /
  contact sheet).
### Notes
- Frame budget (host, Apple silicon; the device must still be read on DIAGNOSTICS): brew frame 0.027 ms (v6.19: 0.041),
  full rebuild 0.20 ms (0.30), HOME rebuild with the four arc labels 0.36 ms, start-up (two background layers + font)
  4.1 ms. Device binary 429 KB (v6.19: 304 KB; +125 KB of font outlines), well inside the installer's 1.2 MB check.
- Frame budget on ARMv7 (qemu, not the device): brew frame 0.78 ms (v6.19: 1.16), full rebuild 3.4 ms (5.9), HOME rebuild
  with the arc labels 7.9 ms, start-up 177 ms (once, before the splash).
- Tests: UI self-test 583 checks EN + DA (v6.20: 570; the v6 layout checks rewritten for v7, plus checks for the rings, the
  roller drag, pull-down, the static layer, arc labels, the glyph engine, the DONE view and touches during cross-fades); the
  same as a 32-bit ARM binary under qemu (one environment-only failure that v6.19 shows too); sim self-test 195; full E2E
  46/46 EN + 46/46 DA (pixel checks for the copper look, the white rings, the STOP pill, the dark error layer); Sim Studio
  machine tests 150 + headless smoke (v7 and v6.19); installer ALL PASS (Python 3.12 and 3.9.6).
- Real bugs the new tests found and this release fixes: STOP dead during the cross-fade into the live view; the offline
  "tap for details" line dead after a brew and under the chevron; titles and the °F hero wider than their space; SYSTEM not
  reachable during an activity; the DONE screen blocking the rings; a boot-time cross-fade swallowing a hold.
- `bin/`: `merakiplus` `f33ce8a1…`, `merakiplus_sim_arm` `b3746ba9…`, Linux SIM `4b9aec3e…`, macOS SIM `47d42a1b…` (official
  zig 0.17.1); `merakiplus_snd`, `switcher`, `tinj_hold`, `keyinj`, `grab_shim.so` as in v6.20.
- **Not yet verified on a machine.**

## [6.20] — 2026-11-22
Fixes from a four-part code review (protocol, persistence, installer, memory/UI/simulator). Every finding was
reproduced first and each fix has its own regression test. The full report, with deferrals and their reasons, is in
[docs/dev/REVIEW-2026-11.md](docs/dev/REVIEW-2026-11.md).

**UART-visible changes are limited to:** (1) up to 2 extra copies of the stock cmd8 pre-arm/disarm frame, only when
its 0x88 echo is missing; (2) a STOP frame queued behind a frame that failed is now sent (it was swallowed); (3) the
user can now tap the unchanged STOP for a start word left set after *NO START* (nothing is sent automatically);
(4) a one-shot pre-heat saved while the clock was unset now gets its normal pre-arm cmd8 once the clock is valid;
(5) fewer frames: an MCU SAVE is refused while a start is pending. No new frame type, word, limit or timing.
### Fixed
- **Revert could write an empty `/customer/demo.sh`** when a read on the display silently failed (gzip failing,
  base64 rc 0). Every read is now md5-verified against the display, and a demo.sh that is empty or differs in more
  than the guard line is never written (also for `--gesture` and the legacy migration).
- **An interrupted activation left the stock UI paused** (SIGSTOP, frozen screen) and revert said "Done". The
  installer now resumes the old UI on Ctrl-C, a dropped connection or a timeout; install and revert resume a paused
  `demo_v9` first, and `status` shows it.
- **A newer vendor (OTA) build in slot B** was overwritten without a backup, and revert restored an older one.
  Install now backs it up as `demo_v9.vendor.<md5>`; revert only replaces slot B when it holds Meraki+ (and checks
  the backup's md5). **Revert on a display without Meraki+** no longer deletes a vendor's boot flag.
- `--persist` sets the boot flag only after Meraki+ is verified running; the free-space check includes the vendor
  backup; failed copies leave no partial temp files (and revert removes old ones); standalone (`curl`) mode logs to
  `~/merakiplus-logs/` instead of the temp dir's parent.
- **A lost pre-arm or disarm was never resent** (protocol): after a physical-button power-off the controller could
  wake the machine at a stale time. The arm now counts only after its echo; otherwise it is resent at most twice.
- **No way to clear a start that never ran:** after *NO START · CHECK THE MACHINE* the start word stayed set with no
  STOP button, and the power button / auto off stayed blocked. HOME now keeps offering that activity's STOP.
- **A STOP tapped while another frame was failing** was discarded unsent; it is now kept and sent.
- **Holding − / + while a SAVE failed** made the value jump to its limit (thousands of steps in one frame).
- A SAVE is refused (*ONE MOMENT…*) while a start is waiting for the machine, like the reverse.
- **Wi-Fi:** a password that is too long, has a malformed `\u` escape or is not a string was treated as "no password",
  and the NET guard wrote an **open** network for the SSID. It is now *BAD PASSWORD* and the network is left alone.
- A one-shot pre-heat saved while the clock was not set stayed ON forever; it is armed once the clock is valid.
- `config.json.orig` (the pristine backup) is written via tmp + fsync + rename; an empty or cut-off one is re-created.
- The gesture switcher no longer leaks its lock, input and log file descriptors into the merakiplus it launches
  (a restarted switcher could refuse to run). `bin/switcher` rebuilt.
- Renderer: a string cut off inside a UTF-8 sequence no longer reads past its end; absurd config values (INT_MIN /
  INT_MAX) are clamped for display (no integer overflow); the transition progress is clamped.
- Simulator / tests: non-finite floats print `null` in the SIM state JSON; the ctrl hook ignores NaN/Inf, range-checks
  integers and survives an embedded NUL; `mcu_sim`'s band check matches the documented write bands; the E2E runner
  reads the state stream incrementally, fails on invalid state lines and logs child stderr to files.
### CI
- `release.yml`: least privilege — only the publish job has `contents: write`; no persisted checkout token; manual
  releases only from main. `ci.yml`: the preview self-test also in Danish, a version/CHANGELOG consistency step, a
  Python 3.8 job and a sanitizer job (UBSan via zig, ASan + UBSan via clang). Actions stay on major tags.
### Notes
- Self-test: new section *v6.20 review fixes* (R1–R11, 25 checks): 570 checks EN + DA, also clean under ASan + UBSan.
  Switcher self-test 52/52 (+2). `sim/selftest.py` +3 band checks. New E2E scenario `ctrl_hardening` (46 scenarios).
  `tests/test_installer.py` gains mock failure injection and 13 scenarios (about 7 min now).
- Deferred with reasons (REVIEW-2026-11.md §2.2): the `grab_shim.so` errno issue (the committed `.so` is the
  on-device-verified build), the pre-heat being 1 h off across a DST change (stock has the same flaw; the fix adds
  cmd8 traffic and needs its own test matrix), and informational items.
- **Not verified on a machine.** Update: run the installer again (`./install.sh`).

## [6.19] — 2026-11-21
Two sound changes from user feedback. Display and audio only — no UART frame, guard, limit, timing, setting or helper
changed (`bin/merakiplus_snd` is byte-identical to v6.18.1).
### Changed
- **No ready tones at a power-on with hot boilers.** Switching on (or waking) a machine whose boilers are still hot
  played the jingle and both ready tones back-to-back. Now each boiler's ready latch is initialized from its state at
  the power-on: a boiler already at heat stage 100 counts as announced, so **only the jingle plays**. A ready tone now
  always means a real cold → hot transition during the session. The decision uses the last sample of a 1.5 s window
  after the power-on (words 62/63 are polled every 200 ms), so stale values right after a power-on or a wake do not
  count. Re-arming after real cooling, the per-boiler READY TONE setting, mute, OFF/sleep silence and skip-during-activity
  are unchanged. Details: [docs/audio.md §2.6](docs/audio.md#26-power-on-with-hot-boilers-v619).
- **Nicer sounds.** `tools/gen_audio.py` now synthesizes struck-instrument timbres instead of sine beeps: modal
  marimba (1 : 3.9 : 9.2) and glockenspiel (1 : 2.76 : 5.40) partials with mallet attack and damped release, a soft FM
  bell layer and a small Schroeder room. Start jingle = warm marimba + bell arpeggio G4-C5-E5-G5 → C6 (1.4 s); brew
  boiler = low marimba double note G4 → D5 (0.8 s); steam boiler = glockenspiel triple pip E6 G#6 B6 (0.75 s). Still
  our own, stdlib-only, 16 kHz mono; ~95 KB together (was ~68 KB). One sound theme (no theme selector).
### Notes
- Self-test: new section *v6.19 boot-already-hot* (8 checks: boot hot → jingle only; cold → hot during the session →
  tone; mixed; stale samples; 3 × sleep → wake hot → no ready tone; long sleep → tones again); the v6.18 sleep/wake and
  OFF → on checks now expect the jingle only. 545 checks EN + DA (also as a 32-bit ARM binary).
- Simulator: new scenarios `audio_boot_hot_no_ready` and `audio_cold_to_hot_ready`; `audio_power_cycle` now expects
  the jingle only (all three fail against the v6.18.1 build, as they should).
- `audio/` md5: `start.wav` `a03cb807d1cdb629b0614fdb740ec71d`, `ready_extract.wav` `4e7e24f18a80ee2f237e2f5ed220afbe`, `ready_steam.wav` `483d57ef122dc9929b004441fe01bc44` (`AUDIO_MD5`).
- `bin/`: `merakiplus` `e654f668…`, `merakiplus_sim_arm` `8624e302…` and the host SIM builds rebuilt (official zig 0.17.1);
  `merakiplus_snd`, `switcher`, `tinj_hold`, `keyinj`, `grab_shim.so` unchanged.
- **Not yet verified on a machine**: the new sounds need a listen on the real speaker and a power-on with hot boilers
  (docs/audio.md §3.5, steps 3 and 5). Update: run the installer again (`./install.sh`).

## [6.18.1] — 2026-11-21
Fix: **every Meraki+ sound was silent on a real machine** (start jingle, both ready tones, the SAVE test tone). Sound
helper only — no UART frame, guard, limit, timing or sound rule changed.
### Fixed
- **`bin/merakiplus_snd` could not load the display's MI libraries**: `merakiplus_snd --check` on the machine printed
  `dlopen: /lib/libmi_common.so: undefined symbol: sem_post` and exited 4. The display's `libmi_common.so` uses
  `sem_*` / `pthread_*` functions that glibc 2.28 has only in `libpthread.so.0`, without listing it as a dependency;
  the stock UI links libpthread, the v6.18 helper did not (`-ldl` only). The helper now puts `libpthread.so.0` into the
  global scope with `dlopen(RTLD_NOW | RTLD_GLOBAL)` before `libmi_common.so` (WAV and MP3 paths), and is also linked
  against it (DT_NEEDED). `--check` reports `MI_SYS + MI_AO ok (sem_post global)`.
  Details and the hardware findings: [docs/audio.md §3](docs/audio.md#3-hardware-check-and-the-v6181-fix).
### Hardware findings (2026-11-21)
- With the v6.18 helper started with `LD_PRELOAD=/lib/libpthread.so.0` the tones **play from the speaker**: the MI_AO path
  works and the speaker amplifier needs **no extra enable pin** beyond what `demo.sh` sets at boot. Checked with Meraki+
  as the only UI process; playback while a paused stock UI holds the MI layer is still untested.
### Notes
- `tests/hygiene.py` now fails if the helper lacks the `libpthread.so.0` dependency or stops loading it first.
- The v6.18 ARM-container check had used a stand-in `libmi_common.so`; it now runs the helper with byte-exact copies of
  the display's own glibc 2.28 and MI libraries (and its own loader), no LD_PRELOAD: v6.18 helper → exit 4 (`sem_post`),
  v6.18.1 helper → exit 0.
- `bin/`: `merakiplus_snd` `a7659c17…` (was `161edd2c…`); `merakiplus` `cc8c5fb9…`, `merakiplus_sim_arm` `ad29bce0…` and
  the host SIM builds changed only in the version string (V6.18.1). `switcher`, `tinj_hold`, `keyinj`, `grab_shim.so` and
  `audio/` are unchanged. All built with the official zig 0.17.1.
- Update: run the installer again (`./install.sh`); it replaces the helper (md5-gated) and the UI.

## [6.18] — 2026-11-21
Audio feedback: a start jingle and **one ready tone per boiler**. The stock audio path was reversed first:
[docs/audio.md](docs/audio.md). **Display and audio only** — no UART frame, guard, limit or timing changed.
### Added
- **Start jingle** once per power-on (button off → on, sleep → wake, or the first start after the display boots).
  Stock plays the audio track of `video/power.mp4` on its logo page.
- **A different tone per boiler**: one when the **brew boiler** is ready, another when the **steam boiler** is — so you
  can hear which one is hot. Stock plays a single *preheatCompele.mp3* once **both** are. The signal is the controller's
  own heat stage (words 62/63 = 100), the same one stock uses.
- Once per power-on; **re-armed only after the boiler has really cooled** (stage ≤ 30 or 15 °C below its ready point),
  so a shot or a steam session never re-triggers it. Silent while off or asleep; a tone that comes due while
  something runs is dropped, as stock drops sounds during its animations. A link glitch is not a power cycle.
- **SYSTEM › DISPLAY · SOUND** (the section was DISPLAY): **SOUND** off / on / stock sounds (the display's own mp3s,
  if present — never shipped), **READY TONE** off / brew boiler / steam boiler / both boilers, and **VOLUME** (config
  `volume`, moved here from STOCK UI because it now sets Meraki+'s sounds too; capped at 70 like stock, 0 = mute).
  SOUND and READY TONE live in Meraki+'s own file `/new_customer/merakiplus/sound`. SAVE plays a short test tone.
- **DIAGNOSTICS › SOUND**: mode · volume and the last sound with its outcome (*STEAM · PLAYED*, *BREW · SKIPPED*, …).
- **Our own sounds** in `audio/` (`start.wav`, `ready_extract.wav`, `ready_steam.wav`; 16 kHz mono, ~68 KB),
  synthesized by `tools/gen_audio.py` (stdlib only). No vendor audio is in the repository; `tests/hygiene.py`
  rejects any other audio/video file.
- **`bin/merakiplus_snd`** (8 KB, `src/merakiplus_snd.c`): plays one sound through the display's own
  `libmi_ao.so` with exactly the MI_AO calls and parameters of the stock player `libhvplayer.so` (48 kHz, 16 bit,
  6 × 2048 frames, the stock volume → dB curve). It is linked against the display's glibc and loads the vendor
  libraries at run time. The installer now also installs it and the three sounds (md5-gated) and revert removes them.
- Simulator: words 62/63 follow the controller's stage rules once the new `boilers` event switches the heat model on;
  state JSON `audio`; scenarios `audio_boiler_ready`, `audio_muted`, `audio_power_cycle`, `audio_sound_off`; the play
  viewer shows the audio log. Runner keys `sim.init_events` and `sandbox_files` (additive).
- Self-test section *v6.18 sound* (27 checks) + a host self-test of the helper against a mock MI_AO (14 checks).
  Meraki+ never signals the helper (it still contains no `kill()`): the helper ends itself after 8 s.
### Changed
- `docs/registermap.md`: words 62/63 are the boilers' **heat stage** (0/30/60/100), not a heating power.
### Notes
- `bin/`: `merakiplus` `3f02369b…`, `merakiplus_sim_arm` `d2266f86…`, the host SIM builds and the new
  `merakiplus_snd` `161edd2c…`. **All reproducible files are now built with the official zig 0.17.1 release**
  (the toolchain CI installs): the v6.17 files had been built with Homebrew's zig, which writes its own name into
  the ELF `.comment` section — same code, different md5 — so CI's new drift check had failed since it was added.
  `switcher` (`f2b95b16…`), `tinj_hold` and `keyinj` therefore changed in `.comment` only; `grab_shim.so` is unchanged.
  `tests/check_bins.py` now names each mismatch, `tests/hygiene.py` rejects Homebrew-built files in `bin/`, and
  `./build.sh` warns when it runs with a Homebrew zig.
- Verified locally: UI self-test 537/0 EN + DA (also as a 32-bit ARM binary), 9/9 mutation probes caught by the new
  section, simulator self-test 192/0, full E2E 43/43 scenarios (1126/1126 checks) in EN and in DA, installer tests.
- **Not yet heard on a machine** (install deferred). The helper resolves every MI symbol against the display's real
  `libmi_sys.so` / `libmi_ao.so` in a 32-bit ARM container; the speaker path itself is pending a hardware check
  ([docs/audio.md](docs/audio.md#3-hardware-check-and-the-v6181-fix)).

## [6.17] — 2026-11-20
A UX audit of v6.16 and the redesign it led to: [docs/ux-audit-v617.md](docs/ux-audit-v617.md).
**Display and navigation only** — no UART frame, guard, limit or timing changed.
### Changed
- **One page per function instead of settings lists.** Tap the grind / brew / water / steam icon on HOME
  to open that function's page: a live line (*BOILER 92.4° · GROUP 70°*, *ON THE SCALE 0.0 G*, …), **its own
  settings** as rows, and a labelled **HOLD TO START**. The three flat lists (BREW · STEAM·WATER · SYSTEM,
  31 rows) and the five-page swipe carousel are gone. Holding an icon still starts directly.
- **The icons say what they will do:** each carries its target underneath (*18.5 G · 36 G · 40 G · 65°*).
  A tap used to answer "HOLD THE BUTTON TO START"; now it opens the page that shows the hold.
- **SYSTEM (the gear) is five rows:** four sections — DISPLAY, CLOCK, SCHEDULE, STOCK UI — that open in
  place (one at a time, with a summary on the closed header: language, time, next pre-heat) and the
  **DIAGNOSTICS** entry with the machine's status. Everything that only matters to the stock UI (BOOT UI,
  SWITCH TO ORIGINAL NOW, VOLUME, STOCK LANG., WATER SHORTCUT, CLEANING REMINDER, AUTO TIME, PAUSE TIME) is
  together under STOCK UI.
- **DIAGNOSTICS is no longer a top-level page.** It opens from SYSTEM — or by tapping HOME's error /
  offline line, which now reads *CODE E2 · TAP FOR DETAILS*.
- **One way back:** the chevron at the top of every page and editor (the whole header is the target), or
  swipe right. No page dots, no invisible edge zones; HOME reacts to no swipe.
- **STOP is the biggest thing under the numbers:** 216 × 54 px (was 160 × 44), bright red with white text
  (4.0 : 1 against the background, was 1.9 : 1); two buttons 152 px wide in the larger font.
- **While something runs, the UI is on HOME.** A function's page is never left on screen during an
  activity (started there, on the machine or by the wand) — HOME is the one live view, where STOP is.
- **Less text.** No footer lines under the editors (*CONTROLLER NOW + CONFIG*, *STOCK UI: AFTER RESTART*,
  file paths); no *SAVED VALUE*; the target during an activity is said once and larger (*STOPS AT 36.0 G*
  instead of *IN THE CUP · TARGET 36.0 G* + *AUTO-STOP AT 36.0 G · OR STOP*); *≈BAR* instead of *≈BAR EST.*;
  one hint line on HOME instead of two; *SAVED · ACTIVE NOW* / *SAVED* instead of the config-file toasts; the
  notes no longer say *CONFIG ONLY* / *CMD8*. 25 string ids removed, 11 added; user-facing text −11 %.
- The read-only WATER SENSOR row is now the WATER page's live line.
### Added
- Editors with a long range show *HOLD - OR + TO GO FAST* while the value is untouched.
- A page's live line turns amber and names the reason when a start is blocked (*NO CONNECTION*,
  *GRIND = TIME · USE STOCK UI*, *CONFIG OUT OF SPEC · FIX FIRST*, …).
- Self-test section *v6.17 IA consolidation* (23 checks): every one of the 29 parameters is on exactly one
  page and its editor opens by real taps from HOME (≤ 3 taps) and closes the same way; HOLD TO START
  queues byte for byte the same frames as the icon hold; an activity started while a page is open brings
  HOME back; STOP size and contrast; no text below an editor's buttons; no *CONFIG ONLY* / *CMD8* /
  *· OR STOP* / *EST.* in any user-facing string.
- E2E scenarios `ia_navigation` (real taps through every level, no frame and no config write),
  `feature_page_start` (a brew started from the BREW page runs exactly like the `brew` scenario) and
  `activity_takes_home`.
- SIM contract (additive, `sim/CONTRACT.md` ADDENDUM v6.17): `tab` = home | grind | brew | water | steam |
  system | diag; `shown.live` / `shown.section`; `page` by those names; `layout` targets `gear`, `back`,
  `start`, `sec_*`, `diag` (the `edge_left` / `edge_right` targets are gone). The `page` keys are unchanged.
### Notes
- `bin/`: `merakiplus` `532e4bd5…`, `merakiplus_sim_arm` `dcb74a94…` and the host SIM builds rebuilt;
  `switcher`, `grab_shim.so`, `tinj_hold` and `keyinj` are byte-identical. **Not yet verified on a
  machine** (install deferred). What that leaves open is listed in §7 of the audit.

## [6.16, rebrand] — 2026-11-19
Same UI version (V6.16); rebuilt binaries. Pure rename + migration: control logic, UART frames,
thresholds and the EN/DA UI texts are unchanged except for the displayed file paths.
### Changed
- **One technical identifier: `merakiplus`.** The former internal code name is gone from the repository.
  Source `src/merakiplus.c`; binaries `bin/merakiplus`, `bin/merakiplus_sim_arm`,
  `bin/merakiplus_sim-linux-x86_64`, `bin/merakiplus_sim-macos-arm64`; install directory
  `/new_customer/merakiplus/` (backups, state, gesture switcher, `lang`, `schedule`, `cfgbak/`); environment
  variables `MERAKIPLUS_*`; build macros `-DMERAKIPLUS_SIM` / `-DMERAKIPLUS_PREVIEW`; `--is-merakiplus`;
  binary marker `@(#)merakiplus `; logs `/tmp/merakiplus_net.log`, `/tmp/merakiplus_boot.log`; console
  prefix `== merakiplus:`. The product name on screen stays **Meraki+**; the process stays `demo_v9`
  (vendor watchdog contract).
- On screen only the displayed paths change (`merakiplus/lang`, `merakiplus/schedule`); the schedule
  footer reads `MERAKI+ · merakiplus/schedule` (without ONLY/KUN, so the longer path fits). A frame-by-frame
  comparison of all 180 previews (EN + DA) against the pre-rename build differs only on those screens.
### Added
- **Migration of earlier installs** in `./install.sh` and `./revert.sh`: the old install directory is
  moved to `/new_customer/merakiplus/` (same filesystem, atomic; merged without overwriting if both
  exist), the old staged binary is dropped, a developer `run.sh` is rewritten, the `demo.sh` guard line
  is rewritten by install (removed by revert, which still restores `demo.sh` byte-identically), and older
  Meraki+ builds are recognised by their old marker. All legacy names live in one block of
  `tools/merakiplus.py` marked *legacy migration only*; `tests/hygiene.py` fails if the old identifier
  appears anywhere else (file names, text or binaries). `tests/test_installer.py` covers upgrade, revert,
  dry-run and purge of a pre-rename install.
- `bin/`: `merakiplus` `737db9b9…`, `merakiplus_sim_arm` `9c3f1fdb…` and `switcher` `a5e0128b…` rebuilt
  (renamed strings only); `grab_shim.so`, `tinj_hold` and `keyinj` are byte-identical to before.
  **Not yet verified on a machine** — an installed device migrates at its next `./install.sh`.

## [6.16] — 2026-11-19
### Fixed
- **TIME FORMAT was inverted relative to the stock UI.** The stock `update_time` (0xD5944) XORs the
  `timemode` flag with 1 before testing it, which the v6.1 analysis missed: **`timemode = 1` is 24-hour,
  `0` is 12-hour (A.M./P.M.)**. The row now shows 24-HOUR for 1, and SAVE writes the value the stock UI
  expects. See [docs/time-scheduling.md](docs/time-scheduling.md).
- The cmd8 frame's date/time is the stock UI's **local** time (UTC + time zone + DST); v6.14 sent the
  display's UTC clock.
### Added
- **Clock**: HOME's top line shows the local time (the display's system clock + the stock UI's time zone
  table + daylight saving when `sumtime` is on) in the stock format (`16:35` / `04:35 P.M.`); DIAGNOSTICS
  has a new *TIME · SCHEDULE* block (local time, zone + DST, auto off, last alarm sent, next pre-heat).
  Meraki+ never sets the clock; an unset clock shows the wordmark and arms no alarm.
- **Planned pre-heat**: *SYSTEM → PRE-HEAT / PRE-HEAT AT / REPEAT* (once, every day, Mon–Fri, Sat–Sun).
  The next occurrence is sent as the controller's wake alarm in the power-off cmd8 frame exactly as the
  stock UI does (`performSleepActions` → `powerOffSend(day, hour, minute)`); the controller's sleep task
  wakes and heats at that time. Additionally the alarm is pre-armed with the same frame while the machine
  is on (so a power-off Meraki+ does not see still wakes it) and re-armed after every wake / MCU restart;
  switching the schedule off sends 0/0/0. A one-time pre-heat switches itself off after use. HOME shows the
  next pre-heat.
- **Optional auto off** (*SYSTEM → AUTO OFF*: off / 15 / 20 / 30 / 60 min, default off): the stock power-off
  sequence after that long without touch, button or activity, with a warning 30 s before.
- The schedule and auto off are stored in Meraki+'s own file `/new_customer/merakiplus/schedule`; `config.json`
  is written only by the existing TIME FORMAT / DAYLIGHT SAVING rows. TIME ZONE (still locked) shows the
  stock label and place names.
- Simulator: scenarios `time_display`, `timemode_stock_parity`, `schedule_prewarm` (alarm wake end to end),
  scenario keys `ui_env` / `ui_only`, expect kind `state`, exact-alarm `time_sync_seen`; the sim's MCU now
  clears the cmd8 alarm on a CPU reset (as the firmware's RAM does).
### Changed
- `bin/`: `merakiplus` is now **V6.16**; `merakiplus_sim_arm` and the host SIM builds rebuilt with it; `switcher`,
  `tinj_hold`, `keyinj` and `grab_shim.so` unchanged. Not yet verified on a machine.

## [6.15] — 2026-11-19
### Fixed
- **The HOME graph resets when an activity starts.** Starting a grind, brew or hot water tares the
  weight to 0, but the graph's 40 s window still held the history from before the start — e.g. a
  200–300 g cup placed on the scale — so the y-autoscale was set by the cup step and the 0 → 36 g curve
  looked flat. The graph (pressure, weight and milk series, and the chart filters) now starts over on
  the idle → active transition and scales to the activity. A concurrent activity (steam/wand flush
  during a brew or grind) does not clear it, and a link gap does not count as idle. Within 1.5 s of the
  start a late tare or late weight-source latch re-anchors the curve once. Display only — regs, control,
  auto-stop and thresholds are unchanged; DIAGNOSTICS → GRAPH = RAW still shows raw values.
  See [docs/calibration.md §14.7](docs/calibration.md#147-the-graph-starts-over-when-an-activity-starts-v615).
- Full E2E gate: the replay scenarios now start the recording relative to the UI's clock instead of
  the simulator's process start, and the runner's event clock and the simulator's log times are
  re-based on the UI's clock origin, so the timing windows no longer drift on a busy CI runner.
### Added
- Simulator: scenario `graph_reset` (a cup at rest, then a brew with a wand flush) and the E2E expect kind
  `chart` (asserts the SIM build's `state.chart`, which now also reports `buf`, `w_max`, `w_top`,
  `resets`, `reanchors`, `act_ms`).
### Changed
- Source comments, developer console/log output, simulator tooling and scenario descriptions are now
  English (the bilingual EN/DA UI string table is unchanged). Switcher log lines are English
  (`GESTURE TRIGGERED`, `switch A→B DONE`); merakiplus's DIAGNOSTICS still recognises the old Danish lines.
- `bin/`: plain `./build.sh` builds of these sources (md5s in `bin/MD5SUMS`, `tools/merakiplus.py` and
  `bin/README.md`): `merakiplus` is now **V6.15**; `merakiplus_sim_arm` and the host SIM builds rebuilt with it;
  `switcher`, `tinj_hold` and `keyinj` rebuilt from the translated sources (unchanged by the graph fix);
  `grab_shim.so` is still the on-device-verified build. The new builds are not yet verified on a machine.

## [6.14] — 2026-11-18 — first public release
### Added
- **The physical power button**: the button node (k62091, `event1`) is read read-only; hold 1.5 s →
  the stock UI's power-off sequence (cmd8 time/alarm + word 0 = 0) byte for byte; TURNING OFF… /
  OFF / TURNING ON… screens; power-on sends PowerOn + awake as at boot. New DIAGNOSTICS block
  *POWER BUTTON*.
- **Public package**: `install.sh` / `revert.sh` (tools/merakiplus.py) with fingerprinting, md5
  gates, one-time backups, atomic install, activation without reboot, dry run and idempotency; mock
  display and installer tests; CI; documentation and disclaimer.
### Known limitation
- On the tested hardware the kernel/PMIC cuts the display's power after a ~0.8 s button hold, and the
  controller's sleep turns the display off ~8 ms after the power-off frame. Power off = power loss for
  the display, power on = cold boot (see [docs/power.md](docs/power.md)).

## [6.13] — 2026-11-18
- **Manual STOP for WATER, BREW and GRIND** (previously steam only). Stop frames reverse-engineered
  from the MCU firmware and cross-checked against the stock UI; feedback toasts (stopping… / stopped /
  no reply / not confirmed).

## [6.12] — 2026-11-18
- **Gesture switcher**: a single center hold ≥ 5 s (radius 40 px) in the stock UI switches to Meraki+
  without a reboot (previously: 3 × corner hold). Grab bypass (`grab_shim.so`, only in the stock
  process), automatic touch range, fallback gesture `SWITCHER_GESTURE=corner`.

## [6.11] — 2026-11-17
- **UX review**: plain language (no word numbers in user-facing text), better contrast, start hint,
  gear icon for settings, close chevron and swipe-down to cancel, swipe guard, DIAGNOSTICS grouped.

## [6.10] — 2026-11-17
- **Pressure as a ≈bar estimate** instead of "uncalibrated": linear anchor (pre-infusion 358 raw =
  config `preInfusionPressure`); pulses > 20 bar are shown as *PULSE*. See
  [docs/calibration.md](docs/calibration.md).

## [6.9] — 2026-11-17
- **Calm graph and stable weight**: median-3 + EMA, pulse gate, p90 scale, stable weight source
  (latch), weight dead band ±0.25 g. Display only — control logic uses raw values.

## [6.8] — 2026-11-17
- REVERT button removed from HOME (developer safeguard); the way back is
  *SYSTEM → SWITCH TO ORIGINAL NOW*.

## [6.7] — 2026-11-17
- **Network guard**: Wi-Fi is brought up at boot from slot B (the stock UI used to do it itself) and
  healed automatically; the password is never logged. Word 113 included in the boot push.

## [6.6] — 2026-11-17
- Phase logic based on a live recording (two steam paths, concurrent phases); **64-bit clock** (the
  32-bit clock wrapped after 24.86 days); config re-push when the controller restarts.

## [6.5] — 2026-11-17
- Meraki+ pushes the config to the controller at boot itself (also on the device); the RX parser can
  no longer stall (resync watchdog); new DIAGNOSTICS fields.

## [6.4] — 2026-11-17
- **SIM build and E2E**: the UI runs without hardware against a simulated controller; E2E suite green
  in English and Danish.

## [6.3] — 2026-11-17
- Product name **Meraki+**, splash and wordmark; all text in **English and Danish** (*UI LANGUAGE*).

## [6.2] — 2026-11-17
- **Boot slot integration**: *BOOT UI* (choose what starts) and *SWITCH TO ORIGINAL NOW* (runtime
  switch without a reboot); DIAGNOSTICS boot block.

## [6.1] — 2026-11-17
- **Settings parity**: limits, steps and labels aligned with the stock UI (address-proven), tiers,
  locked/estimated fields marked.

## [6.0] — 2026-11-16
- First v6: design for the round display (custom anti-aliased font, gauge, toasts, splash), 24
  settings with atomic `config.json` save + backup, GRIND/BREW/WATER/STEAM with hold-to-start, write
  queue with CRC + ACK + retransmit and frame policy, keep-awake fix (word 1 = 0), phases including
  steam/sleeping/error/offline.
