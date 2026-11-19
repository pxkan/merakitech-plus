# Pressure estimate, graph and weight conditioning (calibration note)

**In short:** Meraki+ shows pressure as a **linear estimate** (≈bar · EST.), anchored at a single
point: the pre-infusion plateau in a UART recording (raw 358) is set equal to the config value
`preInfusionPressure` (default 4 bar). This is **not** a calibration — only a physical measurement
(a pressure gauge on a blind basket) can provide that. The raw value is always available under
*DIAGNOSTICS → GRAPH = RAW*. The graph and the weight are filtered in the *display* only; all control
logic, auto-stop and all safety guards use the raw values.

Extracted from [dev/UI_v6.md](dev/UI_v6.md) §14 and §16. Help with a real calibration is very
welcome: measure the pressure with a gauge and open an issue with the raw values from DIAGNOSTICS.

> References to `BREW.md`, `STEAMLIVE.md` and `REGISTERMAP` are to the author's analysis notes
> from which this summary was written; the relevant results are reproduced here and in
> [registermap.md](registermap.md).

## 16. Pressure estimate (v6.10)

**The request:** pressure was shown as *UNCALIBRATED*. A reasonable anchor exists, so the UI now
shows a defensible **estimate** in bar — clearly labelled as an estimate, with the raw value always
available.

### 16.1 The anchor and the scale

| | Value | Source |
|---|---|---|
| **P0** (`P_ANCHOR_RAW`) | **358 raw** (word 51) | The pre-infusion plateau in the UART recording (`BREW.md §6`): brew 20:54:05–06.7, duration 6.06 s (= config `preInfusionTime` 6), median 356, range 332..366. `STEAMLIVE.md` confirms ~360 / 6.84 s in two independent brews |
| **Anchor pressure** | config **`preInfusionPressure`** (default 4 bar) | `/customer/config.json` (`preInfusionPressure: 4` during the recording). Read via the settings table (`SET[S_PIPRESS]`): if the config changes, the scale changes. Outside 1..15 bar or without a config: `P_ANCHOR_BAR_DEF` = 4 |
| **k** | anchor pressure / P0 = 4 / 358 = **0.011173 bar per raw unit** | `p_k()` |
| **Displayed bar** | word 51 (conditioned, §14) × k | `p_bar()` |
| **Pulse limit** (`P_EST_MAX`) | **20 bar** (= 1790 raw at anchor 4) | Above the limit no number is shown |

| Plateau (recording) | Raw | ≈bar (anchor 4) |
|---|---|---|
| Pre-infusion | 358 | **4.0** |
| Extraction (median) | 1346 | **15.0** |
| Extraction (start peak / end) | 1379 / 1325 | 15.4 / 14.8 |
| Transition overshoot | 1481 | 16.5 |
| Hot water (v2) | ~1300 | 14.5 |
| Deadhead / maintenance / steam | 15 716..16 670 | (176..186 linear) → **PULSE**, no number |

The constants can be changed at build time: `-DP_ANCHOR_RAW=… -DP_ANCHOR_BAR_DEF=… -DP_EST_MAX=…`.

### 16.2 What the screen shows

| Where | Text (DA / EN) |
|---|---|
| HOME, the stat field during brewing | label **≈BAR · EST.**, value e.g. **15.0**; above 20 bar **PULS** / PULSE |
| HOME, the chart caption (brew, water, idle after a brew) | **TRYK ≈BAR · EST.** / PRESS. ≈BAR · EST. (replaces *TRYK\* UKALIBRERET*) |
| HOME, tap on the chart or the BAR field | toast (the footnote): **PRÆ-INF. 4 BAR = RÅ 358 · LINEÆR** / PRE-INF. 4 BAR = RAW 358 · LINEAR |
| DIAGNOSTICS, new block **PRESSURE · ESTIMATE** (between GRAPH and ABOUT) | *ANCHOR · PRE-INF. IN RECORDING* → **4 BAR = RAW 358 · k 0.01117** · *NOW · WORD 51 · PUMP SIDE, LINEAR* → **RAW 1346 = ≈15.0 BAR**, or in amber **RAW 16670 · MAINTENANCE PULSE** (idle) / **RAW … · PULSE, NO PRESSURE** (during an activity) · notes: *ESTIMATE · PUMP SIDE · NOT AT THE PUCK* and *> 20 BAR = PULSE · RAW: GRAPH PILL* |
| DIAGNOSTICS → GRAPH = **RAW** | the BAR field shows word 51 as a **raw number** under the label **PRESSURE RAW** (pulses too, unclamped); the chart is v6.9's raw series. Gauge and ring buffer are raw as before |
| SIM state JSON (addition) | `press{raw, k, anchor_bar, p0, max_bar, bar, pulse}` — `bar` is `null` and `pulse` is `true` above the limit |

Display only: control logic, writes, auto-stop, guards and thresholds never use bar (build-checked:
37 control, protocol and conditioning fragments are token-identical to v6.9).

### 16.3 Caveats — why *EST.* and not a calibration

* **(a) Pump side, not the puck.** The linear anchor gives extraction ≈ 15 bar. That is plausible
  for a vibration pump **without an OPV** (budget machine; 12–15 bar is common), but it is the
  pressure on the pump side — not the pressure at the puck, which is typically lower.
* **(b) Pulses are not physical on a linear scale.** The deadhead/maintenance pulses at idle (raw up
  to ~16 670) and the steam level (~16 650, STEAMLIVE §5.3) would be 175–186 bar. The UI therefore
  never shows them as numbers: above 20 bar it shows *PULSE* (HOME) and *MAINTENANCE PULSE* /
  *PULSE, NO PRESSURE* (DIAGNOSTICS); the chart at idle shows them as ticks on the baseline (v6.9).
* **(c) A two-point affine fit does not work.** Idle ≈ 0, pre = 4 bar and ext = 9 bar (the common
  espresso norm) cannot lie on one straight line through 0 (1346/358 = 3.76, not 2.25), and the
  pulses fit neither (BREW.md §7, anchors A/B). Word 51 may be non-linear, or a pump drive level
  rather than a pressure (REGISTERMAP: SPI2 ADC channel 0xA, pressure *or* drive candidate). That is
  why the number is an **estimate** and not a certified calibration.
* **(d) The config anchor is an assumption.** Having the scale follow `preInfusionPressure` assumes
  that the plateau *is* regulated to that pressure. P0 = 358 was measured at 4 bar. If the config is
  set to 3, the UI uses k = 3/358, but it has not been measured whether the plateau then still sits
  at 358 (or moves). Only 4 bar has been recorded.
* **Physical verification** (a pressure gauge on a blind basket, or the sensor ID of the transducer)
  is still the only path to a real calibration (BREW.md §7, recommendation). The raw value is always
  available: DIAGNOSTICS → GRAPH = RAW and the "now" line in PRESSURE · ESTIMATE.

---

## 14. Display conditioning and stable weight (v6.9)

**The problem:** the graph and the numbers on HOME jumped around. There were three causes:
* **Word 51 (pressure/pump drive)** has ADC noise of ±9..±40. Outside an activity it has
  **maintenance pulses**: small heating cycles of 400–900 for 0.5–1.5 s and deadhead pulses of
  3000–16670 for 0.1–0.25 s (BREW.md §5–7, STEAMLIVE.md §5.3). v6.8 switched the chart to "active" on
  a single sample ≥ 40 and scaled to the maximum. Every pulse at idle therefore became a spike that
  flattened the rest of the curve.
* **The weight** (user complaint: *periodic weight spikes, even when the machine is not touched*).
  The graph chose the weight word per sample based on the phase
  (`phase_main()==GRIND ? word 38 : word 39`), and the hero number based on the displayed phase. One
  wrong work_sta sample, a link gap or a single bit-6 sample therefore put a different weight into
  the curve, e.g. the grinder weight 18.6 g in the middle of the cup's 36.7 g. On top of that there
  is cup noise of ±0.1–0.2 g at idle.
* **Words 38/39 are signed** (tare `0xFFFD` = −0.3 g). A filter on the unsigned words smears the tare
  towards 6553 g; the sim's demo "calm" had exactly that bug. The UI filters on int16.

**The principle:** only what is **drawn** changes. `reg[]`, the ring buffer (`g_p/g_w/g_m`), the
state JSON's `regs`/`regs_s`, all writes, the frame policy, auto-stop expectations, guards,
thresholds, the phase logic and the DIAGNOSTICS counters all still read the raw values. The build
check proves it: 29 control/protocol fragments are token-identical to v6.8 (`sim_build_check.txt`).

### 14.1 What is drawn

| Element | Conditioning | Constant (`-D…` at build time · ctrl `cond` in the SIM) |
|---|---|---|
| Chart series (pressure, weight, milk) | per sample (10 Hz) median of the last 3 → EMA | `CD_A_CHART` 0.18 (time constant 0.5 s) · `CD_MED` 3 |
| Chart pressure: pulse gate | if median-3 jumps to > 2 × displayed + 400, the displayed value is held for up to 3 samples; if the jump lasts longer, it is followed | `CD_GATE` 3 (0 = off) |
| Chart on activity change | the next 2 samples follow median-3 directly (no EMA tail; a pulse sample right at the change does not get through) | `CD_FAST` 2 |
| Chart weight on source change and cup off/on | source change 38 ↔ 39: the weight filter restarts on the new word (no history from the other word — otherwise a flash of the old weight). A median jump > 2.0 g in one sample = cup off/on → snap (noise ±0.2 g; a brew rises about 0.2 g/sample) | `CD_W_JUMP` 20 |
| Chart scale | top = max(target/floor × 1.08, **p90 of the window's ACTIVE raw samples** × 1.15). Raw samples above the top are drawn as small ticks just below the ceiling | `CD_PCT` 90 · `CD_HEAD` 1.15 · `scale` = `pct` / `hold` / `max` (v6.8) |
| Pressure at idle | only drawn for samples during brew/pre-infusion/water (`g_a`). Outside the activity word 51 is maintenance, and the pulses are shown as small ticks on the baseline. The chart stays "active" at idle as long as the 40 s window contains a brew/water run | — |
| Hero (weight / milk / boiler) + gauge + BAR field | median-3 + EMA at 10 Hz; **snap to raw** on a phase change (incl. DONE), source change, setpoint/target change, a median jump > 15.0 and after > 0.5 s without an update | `CD_A_HERO` 0.25 |
| Weight (hero, chart, stat field) | **dead band**: \|weight\| < 0.25 g (\|word\| < 2.5) is shown as 0 | `CD_WDB` 2.5 |

**Why p90 only over the active samples:** a p90 over the whole 40 s window is 0 during the first
~4 s of an extraction, because the window is mostly idle. The curve would be squashed against the
ceiling. p90 tolerates up to 10 % of the active samples being pulses. If there are more, they are
part of the signal and are included in the scale.

### 14.2 Stable weight source (latch)

* The default is the **brew weight (word 39)**.
* The **grinder weight (word 38)** is only used while a grind *is* in progress (work_sta bit 6), and
  while its DONE screen is shown for 8 s. The latter only applies if the source was already latched
  to 38 by a real grind.
* A switch requires the wanted source to have been stable for **≥ 300 ms** (`WSRC_DEB_MS`).
  Maintenance bits (0x0400/0x0800/0x2000/0x0C00/0x2800), a link gap or a single deviating work_sta
  sample never switch it.
* **Exception:** if the UI itself started the activity (`act_request`, < 4 s ago) and the activity's
  bits are set, it switches immediately. The activity is real, and the hero then shows the tare of
  0.0 g instead of the cup's weight. If a grind starts *without* the UI (physically), the hero shows
  the latched source for the first 300–400 ms.
* The graph and the hero use the same source. State JSON: `weight:{src, switches, shown}`.

### 14.3 DIAGNOSTICS → GRAPH (smooth / raw)

DIAGNOSTICS has the row **GRAPH · TAP TO TOGGLE**. The default is **SMOOTH · MEDIAN 3 + EMA**; a
tap gives **RAW · UNFILTERED**. RAW is v6.8's series and max scale, without dead band and gate, and
the chart is labelled **RAW** in amber in the right corner. The choice only applies to the current
session: no config, no frames, and it is forgotten on restart.

### 14.4 Known trade-off

The hero lags behind a steady ramp: median-3 adds 1 sample, EMA 0.25 about 3 samples. During a
grind at about 2 g/s the hero shows about 0.8 g below the raw value (preview 03: 11.1 g vs. raw
12.0 g). At DONE it snaps to the raw final weight. The auto-stop belongs to the MCU and is not
affected. If the hero should follow faster, raise `CD_A_HERO` (e.g. 0.4 → about 0.35 g lag).

### 14.5 Tuning in the SIM

```sh
# ctrl socket (SIM build): all fields optional; no fields = read
{"cmd":"cond"}                                    # -> {"ok":true,"raw":false,"a_chart":0.180,"a_hero":0.250,"med":3,"scale":"pct","hold":6,"pct":90,"head":1.15,"gate":3}
{"cmd":"cond","a_chart":0.3,"gate":0}             # live — the filters restart
{"cmd":"cond","raw":1}                            # = DIAGNOSTICS → GRAPH = RAW
```
State JSON (addition, display only): `cond{…, wdb, gate, hero_snaps}`,
`chart{mode, active, full, top, raw_max, disp_max, ticks, base_ticks, clipped, n, n_active}` (v6.15: + `buf, w_max, w_top, resets, reanchors, act_ms`, §14.7),
`weight{src, switches, shown}`.

### 14.6 Before/after figures (graph + weight)

`round_v6/calm_stats.py` reads the state lines from `ui_calm_graph` (the v6.8 and v6.9 SIM builds,
same sim and seed) and computes per segment what was on the screen. The result is in
`sim/reports/v6.9/calm_stats.txt`, and snapshots are side by side in `calm_chart_v68_v69.png`
(chart detail) and `calm_full_v68_v69.png` (full screen). The figures are in
[CHANGES-v6.9](dev/UI_v6.md#changes-v69) item 6.

### 14.7 The graph starts over when an activity starts (v6.15)

**The bug (user report):** starting a grind, brew or hot water tares the weight to 0 (the MCU does
this), but the graph's 40 s window still held the samples from before the start, for example a
200–300 g cup that had just been placed on the scale. The weight axis was scaled to that cup step,
so the 0 → 36 g curve of the actual brew was a nearly flat line at the bottom of the chart.

**What v6.15 does (display only):**

* **Reset on idle → active.** `graph_push()` clears the ring buffer (`g_n = g_head = 0`) and restarts
  the three chart filters when the main phase (`phase_main()`: grind, pre-infusion/extraction, hot
  water, or steam on its own) goes from idle to active. The first sample of the activity is the first
  sample in the ring. The y-autoscale is computed from the ring on every draw, so it follows at once:
  1 s into a brew the weight axis top is the 10 g floor × 1.08 (108) instead of 250 g × 1.08 (2700).
* **No reset for a concurrent activity.** A wand flush or steam that starts *during* a brew or grind
  is not an idle → active transition (the main phase stays BREW/GRIND), so the curve is kept. The same
  applies to a brew started while steam is running: the main phase changes from STEAM to BREW without
  passing through idle, so nothing is cleared.
* **Re-anchor right after the start.** Within 1.5 s of the start the ring is cleared once more if the
  weight source latches late (a grind started at the machine: the source switches to word 38 after
  the 300 ms debounce, §14.2) or the tare lands late (the weight drops by more than 2 g in one sample).
  The cup's pre-tare samples therefore never stay in the curve.
* **Link gaps are not idle.** OFFLINE samples do not count as idle, so a short link loss during a
  brew does not wipe the brew curve when the link comes back.
* **All series.** There is one ring buffer for pressure (word 51), weight (38/39) and milk temperature
  (48), so all three are relative to the activity. Flow (word 52) is a stat number without history.
* **Unchanged:** `reg[]`, the state JSON's `regs`/`regs_s`, writes, the frame policy, auto-stop,
  thresholds, guards and the phase logic. DIAGNOSTICS → GRAPH = RAW still shows the raw
  (unconditioned) values; after a start that is the raw series of the fresh ring.
* **State JSON (SIM build):** `chart` now also has `buf` (samples in the ring), `w_max`/`w_top`
  (the weight line's maximum and axis top in brew/water mode, 0.1 g), `resets`, `reanchors` and
  `act_ms` (ms since the current curve started, −1 = none yet).

The x-axis stays a 40 s window, so early in an activity the curve starts part-way across the chart
and grows to the right, as before.
