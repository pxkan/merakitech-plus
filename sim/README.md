# sim/ — E2E-testmiljø til ourui (MerakiTech Gen 1 espresso-display)

> **Repo-layout (offentlig udgave):** noterne nedenfor blev skrevet i forfatterens arbejdsmappe.
> I dette repo gælder: UI-kilden er `src/ourui_v6.c`; SIM-build'et bygges af `./build.sh` til
> `build/ourui_sim` (auto-detekteres; prebuilt: `bin/ourui_sim-*`); switcher-koden ligger i `boot/`;
> config-baseline (Wi-Fi-felter erstattet med pladsholdere) og parity-spec ligger i `sim/fixtures/`;
> UART-optagelserne til replay ligger komprimeret i `sim/fixtures/captures/*.csv.gz`
> (kun kolonnerne `ts,direction,raw_hex`). Øvrige `artifacts/…`-stier er historiske.
>
> **Hurtigt:** `./build.sh host && python3 sim/selftest.py && python3 sim/run_e2e.py --all --quick --parallel 4`

**Dato:** 2026-11-17 · **Ejer:** test-agenten · **Status:** KOMPLET OG VERIFICERET LOKALT
**Politik:** kun lokal udvikling — **ingen kontakt med den fysiske enhed**. Fable-agentens filer (`ourui_v6.c`, `UI_v6.md`, `REVERT.md`, `round_v6/`) er **ikke rørt**. Kun Python stdlib (ingen Pillow i `.venv` → egne PNG-rutiner i `png.py`, samme metode som `artifacts/write_test/fb2png.py`).

Hele UI'et kan testes uden hardware: en **simuleret MCU** (`mcu_sim.py`) taler den rigtige UART-protokol over en pty, en **scenarie-motor** driver tidslinjer med fejl-injektion, og en **runner** (`run_e2e.py`) launcher ourui's SIM-build (eller reference-klienten indtil det lander), injicerer touch, sampler UI-state + framebuffer og kører assertions.

---

## 1. Arkitektur

```
                        sim/reports/run-*/<scenarie>/
                        ┌──────────────────────────────────────────┐
                        │ sandbox/            state.jsonl          │
                        │  customer/config.json  fb.bin            │
                        │  customer/new (flag)   snapshots/*.png   │
                        │  proc-mocks, tmp/      sim_assertions.jsonl
                        │  dev/input/event0.fifo report.json       │
                        └──────────────────────────────────────────┘
                              ▲            ▲             ▲
        ┌─────────────────────┘            │             └──────────────────┐
        │                                  │                                │
┌───────┴────────┐   evdev-FIFO    ┌───────┴────────┐   JSONL-ctrl   ┌───────┴────────┐
│  run_e2e.py    │ ──────────────► │  ourui SIM-    │ ◄────────────► │  mcu_sim.py    │
│  (runner)      │   touch-inject  │  build (Fable) │                │  (simuleret    │
│                │ ◄────────────── │  ELLER         │ ◄────────────► │   powerMcu)    │
│  events/samples│   state+fb      │  ref_client.py │   pty (UART    │                │
│  assertions    │                 │  (reference)   │   AA 55+CRC)   │ 129-ords model │
│  rapport JSON/MD│                └────────────────┘                │ fase-maskine   │
│  contact-sheet │                                                   │ fejl-injektion │
└────────────────┘                                                   │ assertions-log │
        │                                                            │ REPLAY-motor   │
        │  gestus-stream (fil-mode)                                  └───────┬────────┘
        ▼                                                                    │
┌────────────────┐                                                           │
│ switcher_host  │  (ÆGTE daemon-kode fra boot/)             │
└────────────────┘                                                           ▼
                                                              artifacts/uart_live_v2/v3/
                                                              uart_session/frames.csv
                                                              (REPLAY-ground-truth)
```

* **`mcu_sim.py`** — komplet MCU-model: 129-ords register-file initialiseret fra sandbox-`config.json` (PARITY §1.3 inkl. byte-pakning word112/113/114), cmd3/cmd6/cmd8/cmd9 + 0x83/0x86/0x88/0x89, bånd-validering (ord 0..31 + 112..114), trigger-politik (26/27/30=0 ellers AFVIS+log), ACK-echo med latency/loss/retransmit-profiler, boot-push-detektion, versions-håndtryk, fase-maskine med syntetiske sensor-kurver, fejl-injektion og JSONL-assertions-log.
* **`ref_client.py`** — protokol-level reference-klient der implementerer **samme kontrakt** som ourui's SIM-build (`CONTRACT.md`): boot-push, keep-awake (ord1=0!), poll-rotation, frame-politik, ACK/retransmit (150 ms + 1), hold-for-start touch, settings-GEM, minimal fb-renderer (cirkel-clip r234, fase-farver, page-/toast-regioner). Beviser sim'en + runneren + assertions-pipelinen **nu**.
* **`run_e2e.py`** — runner: sandbox → sim → UI-klient → events → sampling → assertions → rapport (JSON + MD + PNG-snapshots + contact-sheet).
* **`scenarios/*.json`** — 22 tidslinjer (genereret af `gen_scenarios.py`).
* **`selftest.py`** — 107 protokol-checks uden runner (CRC, chunking, write-politik, auto-stop, boot-push, packing, keep-awake, fejl, ACK-profiler, replay-verify).
* **`proto.py`** — fælles CRC/frame/parse/registernavne/parity/evdev-hjælpere.
* **`png.py`** — dependency-free PNG (BGRA→PNG, thumbnails, contact-sheet m. 5×7 bitmap-font).
* **`CONTRACT.md`** — den præse kontrakt til Fable's SIM-build.

## 2. Quickstart

```sh
cd .

# 1. Selftests (protokol-niveau, ~2 s):
python3 sim/selftest.py

# 2. Ét scenarie (reference-klient, ~12-67 s):
python3 sim/run_e2e.py --scenario brew

# 3. Alle 22 scenarier (~6 min):
python3 sim/run_e2e.py --all

# 4. Når Fable's SIM-build lander (auto-detekteres som build/ourui_sim):
python3 sim/run_e2e.py --all --ui-bin build/ourui_sim

# Rapporter: sim/reports/run-<timestamp>/report.md (+ report.json, contact_sheet.png pr. scenarie)
python3 sim/run_e2e.py --list          # scenarie-oversigt
```

 Manuel leg med sim'en alene:

```sh
python3 sim/mcu_sim.py --pty-file /tmp/pty --ctrl /tmp/simctrl --assert-log /tmp/a.jsonl
# i en anden terminal: tal protokollen mod $(cat /tmp/pty) med fx sim/selftest.py's RawClient,
# eller injicér: echo '{"cmd":"inject","event":{"type":"fault","e1":32,"dur":5},"id":1}' | nc -U /tmp/simctrl
```

## 3. Scenarier (25 stk.)

| Scenarie | Varighed | Hvad bevises |
|---|---|---|
| `idle` | 12 s | poll-rotation, keep-awake, boot-push, versions-håndtryk, circle/farve/region-tjek |
| `sleep_wake` | 28 s | ord1=1 → drain (ws 0x0001, kølende boilers) → ord1=0 → HEAT → READY |
| `grind` | 17 s | **touch hold-for-start (650 ms)** → bit6 → vægt-ramp → auto-stop ~187/185 (live) → ord6 konsumeret |
| `brew` | 40 s | pre-infusion plateau ord51≈358 (6 s) → ekstraktion ≈1346 → prædiktivt stop 352 (live) → efter-dryp 364 → ord22 ryddet af MCU (+0,16 s) |
| `hotwater` | 15 s | sub9 len11 → ws 0x030B → dosering → auto-stop ord39≥ord17 → ord16 ryddet |
| `steam_auto` | 40 s | display-steam (sub24 len8, latch) + wand-kontakt → ws 0x4121 → mælk-ramp → **auto-stop ord48≥ord25 uden stop-write** (ord24 MCU-clear +0,18 s) |
| `steam_wand_flush` | 34 s | **v6.6:** wand-flush UNDER bryg → ws 0x5327 (union), ord24=0/ord46=1, **ingen auto-stop >650**, release-stop, bryg-vægt fortsætter, ingen flush-writes |
| `steam_auto_overshoot` | 60 s | **v6.6:** kold mælk (116) → ramp → auto-stop 650 → **overshoot-peak ~689 (+39) ~11 s efter stop**; cur1 ~7340 / ord51 ~16650 under damp |
| `steam_manual_stop` | 12 s | display-steam + wand-kontakt → manuel stop sub24 ord24=0 før setpoint |
| `stop_water` | 14 s | **v6.13:** VAND (auto) → manuel stop sub16 ord16=0 midt i doseringen → ACK, bits 1+3+9 ryddet ≤0,5 s, vægt holdt + post/drip −11 som auto-stop, stop.ok=1 |
| `stop_brew` | 20 s | **v6.13:** BRYG → manuel stop sub22 ord22=0 midt i ekstraktionen → ACK, ventil-puls 0x0105, efter-dryp +12 som auto-stop, stop.ok=1 |
| `stop_grind` | 14 s | **v6.13:** KVÆRN → manuel stop sub6 ord6=0 midt i rampen → ACK, bit6 ryddet, vægten HOLDER (ingen ramp mod mål), cur2-decay, stop.ok=1 |
| `stop_control_auto` | 15 s | **v6.13-kontrol:** VAND uden stop-word → ingen stop-writes, ord16=1 under doseringen, MCU auto-stop ved ord39≥ord17 (uændret) |
| `power_off_wake` | 55 s | **v6.14:** HOLD 1,6 s → cmd8 (alarm 0/0/0) + sub0 len2 [0,0] ACK → **dvale** (tavs UART, regs frosset, keep-awake pause i 34 s) → sim-knap-hold → **reboot til fabriks-defaults** (ord11=920/ord114=50) → 0x89 → **re-push** genskaber config → PowerOn + HJEM + READY |
| `power_during_activity` | 42 s | **v6.14:** power-HOLD under bryg **spærres** (toast, ingen frames — stock stopper aldrig en aktivitet ved sluk); brygen auto-stopper uforstyrret; nyt fuldt hold efter brygen slukker normalt |
| `fault_e1/e2/e3` | 12-16 s | fejl-bits ord34/35/36 → fejl-UI + sikkerheds-stop af aktivitet |
| `crc_garbage` | 12 s | garbage-stream + CRC-korruption → resync, link bevares |
| `no_ack` | 12 s | **GEM blokerer** (retransmit→FAIL, config uændret) → recovery med normal profil |
| `offline` | 14 s | link silent → OFFLINE efter 1,5 s → recovery. Ægte pty-lukning (`link closed` → ny pty ved reopen) bevises i selftest T10. |
| `out_of_spec` | 14 s | config uden for spec → start spærret, sim logger boot-push-overtrædelser → rettelse → start |
| `language` | 10 s | sprog config-only (EN index 0..10; **DA findes ikke i stock's 11 sprog** — ourui's labels er fast dansk, dokumenteret afvigelse) |
| `boot_flag_toggle` | 10 s | `/customer/new` touch/rm i sandbox, ingen MCU-writes |
| `runtime_switch` | 10 s | "SKIFT TIL ORIGINAL NU" (dry-run): bye-view → tilbage, flag uændret |
| `gesture_switcher` | 12 s | **ÆGTE `switcher_host`** i fil-mode: 3×1,6 s hjørne-hold → trigger; 2 hold → ingen; UI ignorerer hjørnet |
| `settings_save_matrix` | 67 s | **min/max/step/original pr. parity_spec-parameter** (188 checks): word-for-word MCU-writes, byte-pakning 112/113, config-persist, config-only uden writes, låste felter afvises. Service-triggers (backflush/drain/kalib/reboot/self-test) og felter uden grænser (timezone/preset/versioner/wifi-strenge) er bevidst udeladt — de er ikke UI-settings (PARITY §2.3/§5). |
| `replay_v3` | 42 s | v3 grind+bryg-trace serveres som sandhed (8× hastighed) |
| `replay_v2` | 20 s | v2 hot water-trace (2 dispenseringer, 8×) |
| `replay_session` | 20 s | session sleep/drain-trace (8×) |
| `replay_steam_live` | 34 s | **LIVE steam+grind+bryg+flush-session** (2026-11-17) som sandhed (8×): auto-stop, flush 0x5327, samtidige faser |

### Sådan tilføjes et scenarie

Scenarier er JSON i `sim/scenarios/` (kan skrives i hånden eller genereres — `gen_scenarios.py` er den reproducerbare kilde til de 22):

```json
{
  "name": "mit_scenarie",
  "description": "dansk beskrivelse",
  "duration_s": 20,
  "config": {"brewTemper": 880},
  "sim": {"profile": "normal", "seed": 1, "require_wand": true},  // require_wand: legacy no-op fra v6.6
  "replay": {"csv": "sim/fixtures/captures/uart_live_v3.csv.gz", "mode": "serve", "speed": 8},
  "events": [
    {"t": 2.0, "target": "ui", "cmd": "action", "name": "brew"},
    {"t": 2.0, "target": "ui", "cmd": "touch", "x": 350, "y": 160, "ms": 700},
    {"t": 2.0, "target": "ui", "cmd": "save", "param": "brew_temp", "value": 880},
    {"t": 3.0, "target": "sim", "event": {"type": "fault", "e1": 32, "dur": 5}},
    {"t": 3.0, "target": "sim", "event": {"type": "garbage", "dur": 2}},
    {"t": 3.0, "target": "sim", "event": {"type": "link", "state": "silent"}},
    {"t": 3.0, "target": "sim", "event": {"type": "wand", "on": true}},
    {"t": 3.0, "target": "sim", "event": {"type": "wand_down"}},
    {"t": 6.0, "target": "sim", "event": {"type": "wand_up"}},
    {"t": 3.0, "target": "sim", "cmd": "ack_profile", "profile": "no_ack"},
    {"t": 4.0, "target": "runner", "op": "snapshot", "label": "mit_billede"},
    {"t": 5.0, "target": "runner", "op": "config_set", "field": "brewTemper", "value": 870, "reload": true},
    {"t": 6.0, "target": "runner", "op": "gesture", "name": "positiv", "presses": 3, "inject_ui": true}
  ],
  "expect": [
    {"kind": "phase", "t": 4, "window": 1.5, "value": "PRE"},
    {"kind": "phase_hold", "t0": 3, "t1": 8, "value": "EXT", "min_ratio": 0.9},
    {"kind": "phase_seq", "t0": 0, "t1": 40, "values": ["READY","PRE","EXT","READY"]},
    {"kind": "word", "word": 22, "t": 33, "window": 1.5, "value": 0},
    {"kind": "word_hold", "word": 51, "t0": 10, "t1": 30, "min": 1250, "max": 1500},
    {"kind": "word_min_seen", "word": 48, "t0": 30, "t1": 35, "min": 650},
    {"kind": "work_sta_seen", "t0": 9, "t1": 31, "value": "0x1307", "mask": "0x2C00"},
    {"kind": "sim_log", "assert": "write_words", "sub": 11, "words": [880], "t0": 2, "t1": 3},
    {"kind": "sim_log", "assert": "activity_stop", "activity": "brew", "reason_contains": "vægt", "t0": 29, "t1": 34},
    {"kind": "config", "field": "brewTemper", "t": 3, "value": 880},
    {"kind": "circle"},
    {"kind": "point", "t": 6, "point": [240, 8], "color": [98, 222, 168], "tol": 30},
    {"kind": "region", "t": 6, "rect": [200, 200, 80, 80], "color": [24, 104, 92], "tol": 30},
    {"kind": "replay_truth", "word": 39, "t0": 17.7, "t1": 18.05, "val_min": 320, "val_max": 370, "signed": true}
  ],
  "universal": {"keepawake": true, "ranges": true, "no_cmd6_fail": true}
}
```

**Expect-kinds:** `phase`, `phase_hold`, `phase_seq`, `word`, `word_hold`, `word_min_seen`, `work_sta_seen`, `stop_ok` (v6.13: `state.stop.ok/fail`), `page`, `view`, `toast`, `lang`, `config`, `flag`, `save`, `save_rejected`, `config_oor_min`, `exit_code`, `crc_bad_min`, `offline_seen`, `link_up_at`, `cmd6`, `circle`, `region`, `point`, `gesture`, `contract`, `replay_truth`, `sim_log`.
**sim_log-asserts:** `boot_push`, `version`, `keepalive_ok`, `no_reboot`, `no_calib`, `no_band_rejects`, `range_max`, `range_min`, `write_words`, `ack_words` (v6.13: ACK-echo sub+words), `no_write_word`, `no_writes_between`, `activity_start`, `activity_stop`, `sleep_write_seen`, `awake_write_seen`, `acks_dropped_min`, `writes_min`.
**Universal-checks** (køres altid medmindre slået fra): circle-tjek på alle snapshots, SIM-kontrakt (state-felter + fb-størrelse), ingen reboot/kalib-trigger-writes, ingen bånd-overskridelser, 0 range-overtrædelser, keep-awake ≤30 s, boot-push + versions-håndtryk, exit 0, 0 cmd6-fejl.

Kør derefter: `python3 sim/run_e2e.py --scenario mit_scenarie`.

## 4. Assertions-liste (hvad miljøet fanger)

1. **Protokol:** CRC-16/MODBUS på alle frames; chunked reads ≤32 B; resync efter garbage; cmd9 byte-LEN; cmd8 fast 15 B; boot-push (sub1 len30 + sub112 len3) ved connect; 0x89→cmd9 håndtryk.
2. **Write-politik:** bånd 0..31 + 112..114 (alt andet afvises+logges); trigger-ord 26/27/30=0 (afvises+logges); parity-grænser pr. ord (logges som `range_violation` — MCU'en har ingen checks, UI'et er eneste værn); ACK=eksakt echo.
3. **Keep-awake:** ord1=0 forventes hvert ≤30 s → `keepalive_gap` i sim-loggen (runneren fejler scenariet).
4. **Fase-kurver:** work_sta pr. fase (idle 0x0101, grind 0x0141, brew 0x1107/0x1307, vand 0x030B, damp 0x4121, sleep 0x0001); pre-infusion plateau ≈358 i ~6 s; ekstraktion ≈1346; prædiktivt vægt-stop + efter-dryp; auto-stop ved mål (vand/brew) og mælk-setpoint (damp); MCU-ejet flag-clear (ord16/22/24).
5. **UI-state:** fase/page/toasts/sprog/boot-flag/config-oor pr. frame mod forventninger; save-blokering ved ACK-udeblivelse (config uændret!); hold-for-start; låste felter afvises.
6. **Framebuffer:** circle-tjek (intet uden for r240, hjørner sorte, max-radius ≤234,71 — portet fra `check_circle.py`); farve/region-tjek (fase-ring, page-region, baggrund, toast).
7. **Settings-matrix:** word-for-word writes mod `parity_spec.json` (min/max/step/original), byte-pakning 112/113 med løbende config-tilstand, config-only-parametre uden MCU-writes.
8. **Gestus:** den ægte switcher-daemon udløses af 3×1,6 s hjørne-hold og ikke af 2 hold; UI'et starter ingen aktivitet af hjørne-holds.
9. **Replay:** sim'ens svar matches frame-for-frame mod optagne registre (se tolerancer nedenfor).

## 5. REPLAY-mode + tolerancer

To modes (`--replay <frames.csv> --replay-mode serve|verify`):

* **serve** (scenarierne `replay_v2/v3/session`): optagelsen afspilles som **sandhed** på live polls (registerspejlet føres af optagne RX-frames; optagne writes appliceres). Live klient-writes (boot-push/keep-awake) lægges ovenpå — i session-scenariet giver keep-awake kortvarige SLEEP→READY-flickers (dokumenteret; forventning bruger `min_ratio`). Hastighed styres med `speed` (scenarierne bruger 8×). Ved 8× kan kortvarige toppe (fx vægt-peak 405 i 1-2 frames) falde mellem UI'ens polls → **`replay_truth`-assertions** spørger sim'en om optagelsens min/max i vinduet (aliasing-sikkert) i stedet for at kræve at UI-spejlet så præcis toppen.
* **verify** (selftest T11 + `mcu_sim.py --replay-mode verify`): sim'ens **egen fase-maskine** drives af de optagne writes på de optagne tidspunkter, og svarene sammenlignes **frame-for-frame** med optagelsen. Rå strace-logs (`uart_trace.log`) dekodes først til `frames.csv` med den eksisterende decoder: `.venv/bin/python artifacts/uart_live_v3/decode_uart_v3.py <log> --fd 13 --outdir <mappe>` (v2/v3/session er allerede dekodet i artifacts). Resultat:

| Optagelse | Samples | Match | Exact-fejl | work_sta-sekvens | Timing |
|---|---|---|---|---|---|
| v3 (grind+bryg, 485 s) | 138.355 | **99,52 %** | 0 | ✓ identisk | ✓ ±1,5 s |
| v2 (hot water ×2, 365 s) | 106.778 | **97,89 %** | 0 | ✓ identisk | ✓ ±1,5 s |
| session (sleep/drain, 729 s) | 209.538 | **98,93 %** | 0 | ✓ identisk | ✓ ±1,5 s |

**Tolerance-klasser** (dokumenteret afvigelse mellem syntetisk kurve og optagelse):

| Klasse | Ord | Tolerance |
|---|---|---|
| `exact` | setup/flag/statiske (0..32 m.fl., 34-37, 43-46, 54, 56-57, 59-67, 73-114, 128) | **0** — skal matche præcist |
| `event` | 3, 6, 9, 16, 22, 24, 33 (work_sta) | tidsforskudt match ±0,75 s (trigger-konsumering er asynkron); work_sta maskeres for idle-pulser (bits 10/11/13) |
| `sensor` | 40, 41, 42, 47, 48, 49, 68-72 | \|Δ\| ≤ 15 (1,5 °C / ADC-enheder); **fysiske hændelser synkes** (ord48 mælk-påfyldning/probe: rec<sim−20 el. dev>60; ord68 vandforbrug: dev>40) — som weight-physync |
| `weight` | 38, 39, 53, 55 | \|Δ\| ≤ 30 (3 g) eller tidsforskudt ±0,75 s; **fysiske spring** (>100 kalibrerede / >40 rå celler: kop fjernet, tare-glitch) synkes fra sandheden (+ rå-celle-base justeres) og tælles separat (`physical_sync`) |
| `fast` | 50, 51, 52, 58 | \|Δ\| ≤ max(400, 35 %) eller tidsforskudt ±15 %; **stokastiske idle-vedligeholdscyklusser** (min<200 vs max>200 i idle — små 400-1000 og store >1000) tåles på begge sider (fasen er ikke deterministisk i originalen) |

**PASS-kriterier:** exact-ord 100 %, samlet match ≥92 %, work_sta-sekvens (maskeret) identisk i rækkefølge, alle transitions inden for ±1,5 s. **v6.6-kalibrering (STEAMLIVE.md):** grind-start +0,39 s, brew-start +0,17 s, vand-start +0,18 s, damp-start +0,30 s; vægt-rater 19,4/18,6/63,8 enh/s (bryg: ramp fra pre+2,75 s, stop mål−8, efter-dryp +12); sleep-køling 0,1125/0,07 enh/s; damp-ramp 14 enh/s midter-rate (kold start 9,8 / slut-fase 12,6), **auto-stop-overshoot +39 enh → peak ~11 s efter stop**, cur1 ~7340 / ord51 ~16650 under damp, s-boiler drop/recovery + flush 11,6 enh/s, wand-flush (latch=0) uden auto-stop. **Verify steam-live: 99,608 % (PASS)**; korte bit9-pre-pulser filtreres fra ws-sekvensen (stokastisk placering: v3 el 1,14 s vs live el 4,97 s). Se `reports/v6.6-calibration/REPORT.md`.

## 6. Hvad miljøet IKKE dækker

* **Ægte fb-timing på Cortex-A7** (SSD202): render-tid, page-flip-tearing, mmap-latens og LVGL-lignende compositor-effekt er ikke modelleret. SIM-build'et renderer til en buffer; `frame_ms` måles på host.
* **Ægte touch-elektrik:** tw31xx-controllerens støj, ghost-touches, multi-finger og raw-range-kalibrering er ikke modelleret (evdev-formatet er ægte, men events er syntetiske).
* **MCU-bootloader/OTA:** `CC DD`-OTA-protokollen, flash-staging, bootloader-kopi og V120-check er **ikke** simuleret (kun `AA 55`-linket). OTA-test kræver separat leverance.
* **Fysisk sensor-fysik:** kurverne er kalibrerede emuleringer, ikke en termodynamisk/vægt-model. Ord51's identitet (tryk vs. pumpe-drive) og bar-skala er **uafklaret** (BREW §7) — sim'en bruger rå plateau-værdier (358/1346), ikke bar.
* **Steam-kurvens form:** nu **live-kalibreret** (STEAMLIVE.md §5, capture 2026-11-17): ramp 1,403 °C/s (midter-rate), auto-stop ved ord48≥ord25, overshoot +3,9 °C (peak ~11 s efter stop), ord24 MCU-clear +0,18 s. Rest-afvigelser i verify (0,39 %): mælk-probens variable afkøling/flush-jet (ikke-observerbar fysik) og vandniveauets vedligehold-purges — se `reports/v6.6-calibration/REPORT.md` §8.
* **MCU'ens præcise stop-algoritme** (prædiktiv vægt-kompensation) er approximeret (stop ved mål−8 + efter-dryp +12, live-kalibreret; bryg#1 i replay seed'es med kvadratisk ramp-fit fra optagelsen).
* **WiFi/NFC/preset-apply/lave-tidssync (cmd8 DST-logik)** er uden for scope (cmd8 ACK'es dog protokol-korrekt).
* **Replay-aliasing:** ved høj afspilningshastighed kan UI-spejlet misse kortvarige toppe (se `replay_truth` ovenfor).
* **ourui's rigtige grafik/layout:** reference-klientens fb er en simpel emulering (fase-ring + knapper + regioner). Circle-/farve-/region-checks bliver først rigtigt meningsfulde med Fable's SIM-build — kontrakten sikrer at de samme assertions så gælder det rigtige UI.

## 7. Verifikations-status (LEVERANCE 5)

| Check | Resultat |
|---|---|
| `sim/selftest.py` (CRC, chunking, write-politik, auto-stop, boot-push, packing, keep-awake, fejl, ACK-profiler, offline, cmd8, replay) | **107 PASS / 0 FAIL** |
| Replay-verify v3 (BREW-trace) | **PASS** (99,52 %, exact 100 %, ws-sekvens+timing OK) |
| Replay-verify v2 + session | **PASS** (97,89 % / 98,93 %) |
| `run_e2e.py --all` m. reference-klient | **22/22 scenarier PASS, 553/553 checks** (definitiv kørsel: `sim/reports/run-20261117-152210/report.md`) |
| Settings-save-matrix (parity_spec min/max/step) | **188/188 checks** |
| Gestus mod ægte `switcher_host` | trigger ✓ / 2-hold nej ✓ |
| Ingen enhedskontakt; Fable's filer urørt; `sim/fixtures/config.json` md5 = baseline `<config-md5>…` | ✓ |

**Hvad der først kan køres når Fable's SIM-build lander:** de samme 22 scenarier mod det rigtige UI (`--ui-bin`), dvs. ægte ourui-grafik i circle-/farve-/region-checks, ægte stepper/settings-UI i matrix-scenariet, ægte BOOT-UI/SKIFT-knapper, ægte DIAGNOSE-side. Kontrakten (`CONTRACT.md`) er designet så scenarierne kører **uden ændringer**; reference-klienten er oracle for felt-navne og timing.

### v6.6-status (2026-11-17, STEAMLIVE-kalibrering)

| Check | Resultat |
|---|---|
| `sim/selftest.py` | **120 PASS / 0 FAIL** (107 + wand/latch/overshoot/samtidig + steam-live-verify) |
| Replay-verify **steam-live** (kalibrerings-målet) | **PASS 99,608 %** (exact 100 %, ws-sekvens+timing OK) — var 92,59 % FAIL |
| Replay-verify v3 / v2 / session | **PASS** (99,83 % / 99,90 % / 100,00 %) |
| `run_e2e.py --all` mod ourui V6.6 (rå EN/DA) + REF | **25/25 scenarier · 644/644 checks** |
| steam_auto deterministisk 30× | **30/30 PASS** |
| Nye scenarier | `steam_wand_flush` (31) + `steam_auto_overshoot` (24) |

Detaljer: `sim/reports/v6.6-calibration/REPORT.md` + CONTRACT.md TILLÆG v6.6-calibration.

### v6.13-status (2026-11-18, manuel STOP — SIM_HANDOFF)

| Check | Resultat |
|---|---|
| `sim/selftest.py` | **150 PASS / 0 FAIL** (120 + T12 manuel STOP + 4 stop-frame-vektorer) |
| Manuel stop i mcu_sim | ord16/19=0 (vand, **også auto-tilstand**), ord22=0 (bryg), ord6=0 (kværn) → samme stop-vej som auto-stop (post/drip/decay), ACK'et |
| Replay-verify v3 / v2 / session / steam-live | **Uændret** (99,83 / 99,92 / 100,00 / 99,64 %) — optagelserne har ingen stop-writes |
| Nye scenarier (EN + DA + REF) | `stop_water`/`stop_brew`/`stop_grind`/`stop_control_auto`: **4/4 · 100/100 checks i hver klient** |
| `run_e2e.py --all --quick --parallel 4` | **5/5 · 97/97** |
| Regression (gesture/steam/brew/hotwater/grind/replay-serve) | **8/8 · 178/178** |
| UI'ets egne stop-scenarier (v6.13, DA) genkørt | **3/3 · 51/51** — slutter READY (før: aktiviteten kørte videre) |

Detaljer: `sim/reports/v6.13-sim/SUMMARY.md` + CONTRACT.md TILLÆG v6.13.

### v6.14-status (2026-11-18, MCU-dvale/wake — SIM_HANDOFF)

| Check | Resultat |
|---|---|
| `sim/selftest.py` | **187 PASS / 0 FAIL** (150 + T13 dvale/wake: 37 checks) |
| Dvale i mcu_sim | ord0=0 → ACK + ≤0,5 s til **tavs dvale** (work_sta=0, udgange 0, aktiviteter stoppet sikkert, regs frosset, keep-awake pause); cmd3/cmd6/cmd8 droppes |
| Wake | knap-hold **≥0,8 s** eller **RTC-alarm** (cmd8) → CPU-reset til **fabriks-startværdier** + 0x89-runde → boot-push/re-push forventes (config genskabes) |
| Nye scenarier (EN + DA, v6.14-SIM + REF) | `power_off_wake` (38 checks) + `power_during_activity` (29 checks): **grønne i alle klienter/sprog** |
| `run_e2e.py --all --quick --parallel 4` | **5/5 · 97/97** (v6.14-SIM-build) |
| Regression (stop×4/gesture×2/steam×4/brew/grind/hotwater/sleep_wake/replay-serve×4 = 18, EN+DA) | **18/18 · 394/394 i hvert sprog** — optagelserne har ingen ord0=0-writes; dvale indtræder aldrig i replay |

Detaljer: `sim/reports/v6.14-sim/SUMMARY.md` + CONTRACT.md TILLÆG v6.14-sim.

## 8. Fil-inventar

| Fil | Formål |
|---|---|
| `mcu_sim.py` | simuleret MCU (pty, register-model, fase-maskine, fejl, assertions-log, replay) |
| `ref_client.py` | reference-klient (implementerer CONTRACT.md; beviser sim'en nu) |
| `run_e2e.py` | runner (sandbox, launch, events, sampling, assertions, rapport) |
| `selftest.py` | 187 protokol-/model-selftests (v6.14: + dvale/wake T13) |
| `proto.py` | CRC/frame/parse/parity/evdev-fællesbibliotek |
| `png.py` | dependency-free PNG + contact-sheet |
| `gen_scenarios.py` | reproducerbar generator til scenarios/*.json |
| `scenarios/*.json` | 31 tidslinjer (v6.14: + power_off_wake/power_during_activity) |
| `CONTRACT.md` | kontrakt til Fable's SIM-build |
| `README.md` | denne fil |
| `reports/run-*/` | kørsels-rapporter (JSON + MD + PNG + logs) |

*— test-agent, 2026-11-17. Alt kørt lokalt; enheden ikke berørt; Fable's filer ikke rørt.*