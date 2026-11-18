# Changelog

All notable changes to Meraki+. Details (with evidence, frames and test figures) are in
[docs/dev/UI_v6.md](docs/dev/UI_v6.md) under *CHANGES-v6.x*. All dates are 2026.

## Unreleased — 2026-11-19
### Changed
- Source comments, developer console/log output, simulator tooling and scenario descriptions are now
  English (the bilingual EN/DA UI string table is unchanged). Switcher log lines are English
  (`GESTURE TRIGGERED`, `switch A→B DONE`); ourui's DIAGNOSTICS still recognises the old Danish lines.
- `bin/`: `ourui`, `ourui_sim_arm`, `switcher`, `tinj_hold`, `keyinj` and the host SIM builds rebuilt
  from the translated sources (new md5s in `bin/MD5SUMS`, see `bin/README.md`); `grab_shim.so` unchanged.
### Fixed
- Full E2E gate: the replay scenarios now start the recording relative to the UI's clock instead of
  the simulator's process start, so they no longer drift on a busy CI runner.

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
