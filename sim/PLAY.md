# PLAY.md — interactive play mode for the SIM environment (Meraki+ / ourui)

**Date:** 2026-11-17 · **Owner:** the test work stream · **Files:** `sim/play.py` + `sim/play_viewer.html`
**Policy:** local development only — **no contact with the physical device**. Python standard library
only. The UI files were not touched. Backward compatible with `run_e2e.py` (same sandbox layout, same
contract `CONTRACT.md`).

With play mode you can start the whole sim environment with one command and **click around the UI in
a browser** with a simulated MCU and data: live framebuffer, state panel, protocol log, touch
injection (real evdev format), fault injection, ACK profiles, language, speed and more.

> The viewer and its logs are in English; only the UI under test can be switched to Danish
> ("UI language" control).

---

## 1. Quickstart

```sh
# One command — opens the browser automatically at http://127.0.0.1:8765:
python3 sim/play.py

# Useful variants:
python3 sim/play.py --port 9000 --lang en          # another port + English UI language
python3 sim/play.py --scenario steam_auto          # the scenario's config/sim as the starting point
python3 sim/play.py --ack observed_v3              # ACK profile at start
python3 sim/play.py --ui-bin /path/to/ourui_sim    # explicit UI binary
python3 sim/play.py --no-browser                   # do not open a browser
python3 sim/play.py --keep                         # keep the sandbox on exit (troubleshooting)
python3 sim/play.py --smoke --no-browser           # headless smoke test (see §7)
```

Stop with **Ctrl-C** — it cleans up the pty, the touch FIFO, the ctrl sockets and the sandbox (except
with `--keep`). Snapshot exports are always kept in `sim/reports/play-exports/`.

### What is started?

`play.py` reuses `run_e2e.py`'s launch mechanics (`ScenarioRun.make_sandbox/start_sim/start_ui`):

1. **Sandbox** in `sim/reports/play-<timestamp>-<pid>/play/sandbox/` (the same layout as CONTRACT §8:
   `customer/config.json`, `customer/new`, `proc` mocks, `dev/input/event0.fifo`, `fb.bin`).
2. **`mcu_sim.py`** on a pty with a ctrl socket + assertion log (JSONL).
3. **The UI client**: the UI's SIM build if it exists — otherwise the **reference client**
   (`ref_client.py`).
4. **Web viewer** at `http://127.0.0.1:<port>` (stdlib `http.server`, no dependencies, binds to
   localhost only).

### UI binary: auto-detection and the two modes

| Mode | When | What do you see? |
|---|---|---|
| **ourui SIM build** (full visual play) | `build/ourui_sim` exists (`./build.sh host`), or `--ui-bin`/`OURUI_SIM_BIN` points to a binary | The real Meraki+ UI rendered to the fb file; clicks hit the real buttons |
| **Reference client** (protocol/state play) | when no SIM build is found, or with `--force-reference` | A simple fb emulation (phase ring + four buttons + toast region) = **placeholder graphics**, but full protocol, state and interaction. The viewer shows a blue banner about this. |

Auto-detection happens at startup. **Status 2026-11-17:** the v6.4 binary landed at 16:04
(`build/ourui_sim`) and is now auto-detected — full visual play is active and verified (§7). The
reference mode is still available with `--force-reference`.

---

## 2. The viewer — what can you click?

### Framebuffer (left)

* **Live fb** is updated at ~6 Hz (PNG from `fb.bin`, cached per frame).
* **Click** = tap (touch down/up immediately).
* **Hold ≥ 650 ms** on an activity button = *hold-to-start* (exactly as in ourui: you must hold the
  button for at least 650 ms). A yellow/green ring shows the hold time.
* **Right-click** (or the "gesture test" button) = gesture test: the trigger gesture is injected as
  an evdev stream, and the **real `switcher_host`** is run in file mode against the same stream. The
  result ("GESTURE TRIGGERED" / "no trigger") is shown in the panel.
* **Coordinate overlay**: a crosshair + `x=… y=… (raw …,…)` follows the mouse, so you can see exactly
  where you hit. Mapping: `raw = px*4096/480` (inverse: `px = raw*480/4096`), the same formula as
  `tinj_hold`/CONTRACT §4.
* **"Show touch targets (layout)"** overlays the actual touch targets on the fb: against the SIM build
  they are fetched live from the build's ctrl `layout` hook (the hit_test geometry: grind 150,372 ·
  brew 210,372 · water 270,372 · steam 330,372 · revert + swipe edges); against the reference client
  its known button layout is used (grind 130,160 · brew 350,160 · water 130,330 · steam 350,330 ·
  settings 240,430). The gesture zone is always shown.

### State panel (right)

Phase (colored enum key: `READY/HEAT/PRE/EXT/GRIND/WATER/STEAM/ERROR/SLEEP/OFFLINE`), page
(`home/settings/detail/sys/diag/bye`), `work_sta` (hex), language index, boot flag, link status, CRC
ok/bad, cmd6 ok/retx/fail/blocked, frame time (ms), `config_oor`, save state, activity pending, the
last toast, **config md5** (updated live and logged on every change), MCU mode/time and ACK profile.

### Control panel

| Group | Controls |
|---|---|
| **Activities** | Grind/Brew/Water/Steam + Stop steam. **Simulated touch hold (750 ms)** on the button's touch target — the reference layout against ref_client, the SIM build's ctrl `layout` against the real UI. Ctrl `action` (contract hook) as a fallback if the target is not found, and always for `steam_stop`. You can also click/hold directly on the fb. |
| **Page** | ctrl `page` home/settings/detail/sys/diag |
| **Settings SAVE** | choose a parameter (brew_temp, grind_weight, …) + value → ctrl `save` (parity write + config persistence). See the `save` state, toast, config md5 and the protocol log's `WRITE sub11=[880]` + `CONFIG md5 …`. |
| **Faults & link** | Fault cycle (none→E1→E2→E3→none), CRC garbage (2 s), Offline (link silent)/Back online, Close pty (closed → a new pty on reopen + automatic UI restart). Wand on/off (v6.6: the flush path, also during a brew) + legacy "require wand for steam" (no-op). |
| **ACK profile** | normal/observed_v3/slow/lossy/no_ack — switched **live** (mcu_sim ctrl `ack_profile`). `no_ack` blocks SAVE (retransmit→FAIL, config unchanged). |
| **System & time** | Speed 1×/2×/8×, pause/resume, UI language Danish/English (lang file), stock language (config index 0..10), boot flag toggle, runtime switch (dry: bye view), MCU restart (boot push), Restart UI, Reset sandbox. |
| **Snapshot** | Export PNG + state (+ a config copy) to `sim/reports/play-exports/<ts>_<label>/`. The latest exports are listed with links. |
| **Protocol log** | The latest ~160 events from mcu_sim's assertion log: writes (blue), REJECTED/RANGE/ACK loss (red), START/STOP/boot push/version (green), faults/link/garbage/reboot/config md5 (yellow). Auto-scroll. |

### Keyboard shortcuts

| Key | Action |
|---|---|
| `g` / `b` / `h` / `s` | start grind / brew / hot water / steam (simulated hold, same strategy as the buttons) |
| `e` | fault cycle: none → E1 (word34=32) → E2 (word35=16) → E3 (word36=16) → none |
| `l` | UI language Danish ↔ English |
| `r` | reset the sandbox (fresh session; exports are kept) |
| `space` | pause / resume sim time |
| `?` / `Esc` | show / close the help overlay |

(Shortcuts are ignored while the focus is in an input field.)

---

## 3. HTTP API (for scripts/curl)

The viewer uses these endpoints — you can also use them directly:

```sh
curl -s localhost:8765/api/state | python3 -m json.tool     # all state in one JSON
curl -s localhost:8765/fb.png -o fb.png                     # live framebuffer
curl -s -XPOST localhost:8765/api/touch -d '{"x":350,"y":160,"ms":700}'   # hold on BREW
curl -s -XPOST localhost:8765/api/activity -d '{"name":"brew"}'
curl -s -XPOST localhost:8765/api/fault -d '{}'             # fault cycle
curl -s -XPOST localhost:8765/api/ack -d '{"profile":"no_ack"}'
curl -s -XPOST localhost:8765/api/speed -d '{"factor":8}'
curl -s -XPOST localhost:8765/api/pause -d '{"value":true}'
curl -s -XPOST localhost:8765/api/lang -d '{"value":"da"}'
curl -s -XPOST localhost:8765/api/save -d '{"param":"brew_temp","value":880}'
curl -s -XPOST localhost:8765/api/snapshot -d '{"label":"my_test"}'
curl -s -XPOST localhost:8765/api/gesture -d '{"presses":3}'
curl -s -XPOST localhost:8765/api/reboot                    # MCU restart
curl -s -XPOST localhost:8765/api/restart_ui                # UI restart (after a crash)
curl -s -XPOST localhost:8765/api/reset                     # fresh sandbox
```

Others: `/api/garbage`, `/api/link {state:silent|up|closed}`, `/api/wand {on}`,
`/api/require_wand {value}`, `/api/boot_flag {value}`, `/api/runtime_switch`, `/api/page {name}`,
`/api/stock_lang {value}`, `/api/config {field,value,reload}`, `/api/layout` (the real touch targets
in the current view), `/api/scenarios`, `/exports/<file>` (snapshot files).

---

## 4. Robustness

* **UI crash** → the viewer shows a red banner with the exit code + a stderr excerpt and a
  **Restart UI** button (`/api/restart_ui`). The state file starts fresh on restart.
* **mcu_sim offline/dead** → the UI shows OFFLINE (expected behavior: link timeout 1.5 s); the viewer
  shows the MCU status + a Reset button.
* **Pause** freezes the MCU time → the UI shows OFFLINE (expected); resume restores the link.
* **Close pty** → OFFLINE; **Back online** opens a new pty and automatically restarts the UI with the
  new pty path (a client cannot switch pty while running).
* **MCU restart** → the sim reboots (registers are reset, the version handshake + boot push are
  expected again). The reference client detects 0x89 again and re-handshakes + re-pushes (a small
  backward-compatible addition to `ref_client.py`; the scenarios are unchanged).
* **Exit (Ctrl-C/SIGTERM)** → ctrl `exit` to the UI + sim, process wait/terminate, sockets, FIFO, pty
  and sandbox are cleaned up. `--keep` keeps the sandbox for troubleshooting.
* **Writes** only happen in the sandbox (`sim/reports/play-*/`), the designated exports
  (`sim/reports/play-exports/`) and a short `/tmp/ourui_e2e_<pid>_*` directory for the ctrl sockets
  (AF_UNIX paths must be ≤ 104 bytes on macOS; the directory is cleaned up on exit).
* **Speed > 1×**: only the MCU's sim time is scaled; the UI client still runs on the wall clock. The
  keep-awake threshold is scaled accordingly in the sim, so there are no false `keepalive_gap` logs.
  Activities complete faster (8× ≈ 8× faster curves).

---

## 5. What was added to the sim environment (backward compatible)?

| File | Addition |
|---|---|
| `mcu_sim.py` | ctrl `speed {factor}`, `pause {value}`, `require_wand {value}`; `ping` now also returns speed/paused/profile/require_wand. Live speed/pause in the main loop (1×/unpaused = byte-identical behavior). The keep-awake threshold scales with the speed (1× unchanged). `do_reboot` resets the boot push detection + first RX, so the boot push/version handshake are expected again after a reboot. |
| `ref_client.py` | 0x89 received *after* a completed handshake = MCU restart → re-handshake + re-boot-push (as on a new connect, PARITY §1.4). Does not affect the scenarios (no reboot in the scenarios). |
| `run_e2e.py` | `start_ui()` sets `OURUI_LANGFILE=<sandbox>/new_customer/ourui/lang` (the v6.3+ language file; a missing file = fallback EN, unchanged behavior for older builds). |
| `play.py`, `play_viewer.html`, `PLAY.md` | The play mode deliverable itself (new files). |

`selftest.py` and all scenarios were rerun after the changes — see §7.

---

## 6. Limitations

* **Full visual play requires the SIM build.** Without it, the fb is the reference client's simple
  emulation (phase ring + buttons + toast region) — not ourui's real graphics. All protocols, state
  fields, touch mechanics and controls are, however, the same.
* **The reference client's button layout** is not ourui's real layout. Against the SIM build, play
  uses the real touch targets from ctrl `layout`; against the reference client its own layout. Direct
  click/hold on the fb always works (it is just touch coordinates).
* **Language:** ourui's UI language (v6.3+) lives in its own file (`"en"`/`"da"`, `OURUI_LANGFILE`),
  not in config.json. Play writes the file; against the SIM build the UI is restarted automatically
  (the file is loaded at boot). **Danish is not among the stock UI's 11 languages** (config index
  0..10 = EN/DE/FR/IT/ES/PL/AR/简中/繁中/KO/JA) — the stock language row is config-only and does not
  affect ourui's labels. The reference client has no i18n (only the config index in the state).
* **Steam has two paths (v6.6, STEAMLIVE §5-6).** Display steam (the UI button / word24=1) runs
  **without** the wand switch and auto-stops at the milk setpoint. The wand switch (PE.2, "Wand
  on/off") starts a **flush** (word24=0, no auto-stop) — also on top of a brew (work_sta 0x5327) —
  and stops when the wand is released. The "require wand for steam" button is a legacy no-op since
  v6.6 (display steam is no longer gated; SESSION §5.2's wand requirement was disproven live).
* **Pause/offline:** the UI shows OFFLINE when the MCU time is frozen or the link is closed — that is
  correct behavior (link timeout 1.5 s), not a bug.
* **The gesture test** runs `switcher_host` in file mode against a synthetic stream (the same method
  as the `gesture_switcher` scenario). The stream is also injected into the UI's FIFO; the gesture
  starts no activity in the UI (correct — the switcher owns the gesture).
* **No multi-touch/ghost touches** — touch is a synthetic single pointer (CONTRACT §4).
* **fb refresh ~6 Hz** in the viewer (PNG conversion in pure Python); the SIM build itself writes the
  fb at ≤ 10 Hz (CONTRACT §2). The viewer refresh, not the UI render, is the bottleneck.
* **Replay mode** is not supported in play (scenario replays are run with `run_e2e.py`).
  `--scenario` only uses the scenario's `config`/`sim`/`boot_flag_initial` as the starting point.

---

## 7. Verification

### Headless smoke (against the reference client)

```sh
python3 sim/play.py --smoke --no-browser --force-reference
# → [smoke] 30/30 PASS · client=reference
```

The smoke test starts the whole environment, curls the viewer pages (`/`, `/api/state`, `/fb.png`),
injects touch via the API and asserts state changes in the JSONL:

1. Viewer HTML + contract fields in the state + PNG magic in the fb.
2. **BREW hold via the touch API → phase PRE/EXT** in the state panel AND directly in `state.jsonl`;
   the fb PNG changes during the brew.
3. Speed 8× → **brew auto-stop** in the sim log.
4. **STEAM → auto-stop at the milk setpoint** (word48≥word25, stop reason "milk ≥ setpoint").
5. Fault cycle E1 → **ERROR phase** → clear → READY.
6. **no_ack → SAVE blocked** (save=fail, config md5 unchanged) → normal → **SAVE ok**
   (config.brewTemper=880 in the sandbox, md5 changed + logged, MCU write sub11=[880]).
7. Language file = "da", boot flag toggle in the state, pause/resume, gesture (triggers the switcher),
   touch tap raw mapping (240→2048), snapshot export (PNG + state exist), reset → fresh session.

### Full visual smoke (against the SIM build — landed 2026-11-17 16:04)

**The same command** — `--smoke` auto-detects the binary and runs the same checks against the real UI:

```sh
python3 sim/play.py --smoke --no-browser
# → [smoke] 30/30 PASS · client=sim-build   (2026-11-17 16:08)
#   binary: build/ourui_sim · md5 354976e839a26663310493446c7a3f97
```

The visually relevant checks against the real UI:

* **BREW via a touch hold on the real touch target (210,372 via ctrl `layout`) → phase PRE/EXT in the
  state + the fb changes** (real graphics).
* **STEAM → auto-stop at the target** (milk ≥ setpoint word48≥word25, sim log + state).
* **Settings SAVE → the config in the sandbox changes (brewTemper=880) + md5 log** + word-for-word MCU
  write sub11=[880].

In addition verified in a **real browser** (headless Chromium against a live play session): the
viewer renders the real Meraki+ UI (Danish labels, gauges, READY badge); a real mouse hold ≥ 650 ms on
the BREW button in the fb image → **EXT phase**; right-click → **gesture triggered** (the real
switcher_host); keyboard `e` → ERROR → clear → READY; button zones/touch target overlay + coordinate
overlay live. Screenshots + smoke log: `sim/reports/play-exports/verification-20261117/`
(`play_real_ui.png`, `play_real_brew.png`, `play_viewer_top.png`, `smoke_simbuild.log`; not tracked in
git).

Also run the whole E2E suite against the binary for full contract coverage:

```sh
python3 sim/run_e2e.py --all --ui-bin build/ourui_sim
```

### Backward compatibility (run after the changes)

```sh
python3 sim/selftest.py                 # all PASS
python3 sim/run_e2e.py --all            # all scenarios
```

An interactive server test (28 checks: touch→JSONL, MCU reboot→re-handshake + boot push, pty
close→OFFLINE→reopen + UI restart, UI crash→banner→restart, speed/pause/ACK live, language file,
snapshot serving, Ctrl-C/SIGTERM cleanup) was run green against a live play session during
development.

---

## 8. Troubleshooting

| Symptom | Cause / solution |
|---|---|
| `kan ikke binde port 8765` (cannot bind port) | The port is in use — use `--port <other>`. |
| The viewer shows the "REFERENCE CLIENT" banner | No SIM build was found. Run `./build.sh host` (→ `build/ourui_sim`) or use `--ui-bin`. |
| UI crash banner | Click "Restart UI"; a stderr excerpt is shown in the banner. The full stderr is in the play process's output. |
| Touch does nothing | Hold ≥ 650 ms on an activity button (a tap only gives a hold hint toast). Against the SIM build: use the real button layout. |
| Steam does not start | "require wand for steam" is enabled and the wand is off — turn the wand on or the requirement off. |
| Everything feels sluggish at 8× | The MCU time is 8× faster, but the UI polls/render are wall-clock — expected. |
| Sandbox gone after exit | Ctrl-C cleans up by design; use `--keep` to keep it, or a snapshot export before exit. |

*— test work stream, 2026-11-17. Everything run locally; the device was not touched.*
