# PLAY.md — interaktiv play-mode til SIM-miljøet (Meraki+ / ourui)

**Dato:** 2026-11-17 · **Ejer:** test-agenten · **Fil:** `sim/play.py` + `sim/play_viewer.html`
**Politik:** kun lokal udvikling — **ingen kontakt med den fysiske enhed**. Kun Python stdlib.
Fable's filer (`artifacts/ourui/`) er ikke rørt. Bagudkompatibel med `run_e2e.py`
(samme sandbox-layout, samme kontrakt `CONTRACT.md`).

Med play-mode kan du starte hele simmiljøet i ét kald og **klikke rundt på UI'et i en
browser** med simuleret MCU og data: live framebuffer, state-panel, protokol-log,
touch-injektion (ægte evdev-format), fejl-injektion, ACK-profiler, sprog, hastighed m.m.

---

## 1. Quickstart

```sh
cd .

# Ét kald — åbner browseren automatisk på http://127.0.0.1:8765:
python3 sim/play.py

# Nyttige varianter:
python3 sim/play.py --port 9000 --lang en          # anden port + engelsk UI-sprog
python3 sim/play.py --scenario steam_auto          # scenariets config/sim som udgangspunkt
python3 sim/play.py --ack observed_v3              # ACK-profil ved start
python3 sim/play.py --ui-bin /sti/til/ourui_v6_sim # eksplicit UI-binær
python3 sim/play.py --no-browser                   # åbn ikke browser selv
python3 sim/play.py --keep                         # bevar sandbox ved exit (fejlfinding)
python3 sim/play.py --smoke --no-browser           # headless smoke-test (se §7)
```

Stop med **Ctrl-C** — det rydder pty, touch-FIFO, ctrl-sockets og sandbox
(undtagen `--keep`). Snapshot-eksporter bevares altid i `sim/reports/play-exports/`.

### Hvad startes?

`play.py` genbruger `run_e2e.py`'s launch-mekanik (`ScenarioRun.make_sandbox/start_sim/start_ui`):

1. **Sandbox** i `sim/reports/play-<tidsstempel>-<pid>/play/sandbox/` (samme layout som
   CONTRACT §8: `customer/config.json`, `customer/new`, `proc`-mocks, `dev/input/event0.fifo`, `fb.bin`).
2. **`mcu_sim.py`** på en pty med ctrl-socket + assertions-log (JSONL).
3. **UI-klienten**: Fable's SIM-build hvis den findes — ellers **reference-klienten** (`ref_client.py`).
4. **Web-viewer** på `http://127.0.0.1:<port>` (stdlib `http.server`, ingen dependencies,
   binder kun til localhost).

### UI-binær: auto-detektion og de to tilstande

| Tilstand | Hvornår | Hvad ser du? |
|---|---|---|
| **ourui SIM-build** (fuld visuel play) | `build/ourui_sim` findes (Fable's v6.4), eller `--ui-bin`/`OURUI_SIM_BIN` peger på en binær | Det rigtige Meraki+-UI renderet til fb-filen; klik rammer de rigtige knapper |
| **Reference-klient** (protokol/state-play) | Indtil v6.4-binæren lander | Simpel fb-emulering (fase-ring + fire knapper + toast-region) = **placeholder-grafik**, men fuld protokol, state og interaktion. Viewer'en viser et blåt banner om dette. |

Auto-detektionen sker ved opstart. **Status 2026-11-17:** v6.4-binæren landede kl. 16:04
(`build/ourui_sim`) og auto-detekteres nu — fuld visuel play er aktiv og
verificeret (§7). Reference-tilstanden er stadig tilgængelig med `--force-reference`.

---

## 2. Viewer'en — hvad kan du klikke på?

### Framebuffer (venstre)

* **Live fb** opdateres ~6 Hz (PNG fra `fb.bin`, cachet pr. frame).
* **Klik** = tap (touch-down/up med det samme).
* **Hold ≥650 ms** på en aktivitets-knap = *hold-for-start* (præcis som ourui:
  du skal holde knappen nede i mindst 650 ms). En gul/grøn ring viser hold-tiden.
* **Højreklik** (eller knappen "Gestus-test") = gestus-test: 3×1,6 s hold i hjørne-boksen
  (75,75) injiceres som evdev-stream, og den **ægte `switcher_host`** køres i fil-mode
  mod samme stream. Resultatet ("GESTUS UDLØST" / "ingen trigger") vises i panelet.
* **Koordinat-overlay**: trådkors + `x=… y=… (raw …,…)` følger musen, så du kan se
  præcis hvor du rammer. Mapping: `raw = px*4096/480` (invers: `px = raw*480/4096`),
  samme formel som `tinj_hold`/CONTRACT §4.
* **"Vis tryk-mål (layout)"** lægger de faktiske tryk-mål oven på fb'et: mod SIM-build'et
  hentes de live fra build'ets ctrl `layout`-hook (hit_test-geometrien: grind 150,372 ·
  brew 210,372 · water 270,372 · steam 330,372 · revert + swipe-edges); mod reference-
  klienten bruges dets kendte knap-layout (grind 130,160 · brew 350,160 · water 130,330 ·
  steam 350,330 · settings 240,430). Gestus-hjørnet (40,40–110,110) vises altid.

### State-panel (højre)

Fase (farvet enum-key: `READY/HEAT/PRE/EXT/GRIND/WATER/STEAM/ERROR/SLEEP/OFFLINE`),
side (`home/settings/detail/sys/diag/bye`), `work_sta` (hex), sprog-index, boot-flag,
link-status, CRC ok/bad, cmd6 ok/retx/fail/blok, frame-time (ms), `config_oor`,
save-state, activity-pending, seneste toast, **config-md5** (opdateres live og logges
ved hver ændring), MCU-mode/tid og ACK-profil.

### Kontrol-panel

| Gruppe | Kontroller |
|---|---|
| **Aktiviteter** | Grind/Bryg/Vand/Damp + Stop damp. **Simuleret touch-hold (750 ms)** på knappens tryk-mål — reference-layout mod ref_client, SIM-build'ets ctrl `layout` mod det rigtige UI. Ctrl `action` (kontrakt-hook) som fallback hvis målet ikke findes, og altid for `steam_stop`. Du kan også klikke/hold'e direkte på fb'et. |
| **Side** | ctrl `page` home/settings/detail/sys/diag |
| **Settings-GEM** | vælg parameter (brew_temp, grind_weight, …) + værdi → ctrl `save` (parity-write + config-persist). Se `save`-state, toast, config-md5 og protokol-loggens `WRITE sub11=[880]` + `CONFIG md5 …`. |
| **Fejl & link** | Fejl-cycle (ingen→E1→E2→E3→ingen), CRC-garbage (2 s), Offline (link silent)/Genonline, Luk pty (closed → ny pty ved reopen + automatisk UI-genstart). Wand til/fra (v6.6: flush-vejen, også under bryg) + legacy "kræv wand for damp" (no-op). |
| **ACK-profil** | normal/observed_v3/slow/lossy/no_ack — skiftes **live** (mcu_sim ctrl `ack_profile`). `no_ack` blokerer GEM (retransmit→FAIL, config uændret). |
| **System & tid** | Hastighed 1×/2×/8×, pause/resume, UI-sprog Dansk/Engelsk (lang-fil), stock-sprog (config-index 0..10), boot-flag toggle, runtime-skift (dry: bye-view), MCU-genstart (boot-push), Genstart UI, Reset sandbox. |
| **Snapshot** | Eksportér PNG + state (+ config-kopi) til `sim/reports/play-exports/<ts>_<label>/`. Seneste eksporter listes med links. |
| **Protokol-log** | Seneste ~160 hændelser fra mcu_sim's assertions-log: writes (blå), AFVIST/RANGE/ACK-tab (røde), START/STOP/boot-push/version (grønne), fejl/link/garbage/reboot/config-md5 (gule). Auto-scroll. |

### Tastatur-genveje

| Tast | Handling |
|---|---|
| `g` / `b` / `h` / `s` | start grind / bryg / hotwater / steam (simuleret hold, samme strategi som knapperne) |
| `e` | fejl-cycle: ingen → E1 (ord34=32) → E2 (ord35=16) → E3 (ord36=16) → ingen |
| `l` | UI-sprog dansk ↔ engelsk |
| `r` | reset sandbox (frisk session; exports bevares) |
| `space` | pause / resume sim-tid |
| `?` / `Esc` | vis / luk hjælpe-overlay |

(Genveje ignoreres når fokus er i et input-felt.)

---

## 3. HTTP-API (til scripts/curl)

Viewer'en bruger disse endpoints — du kan også bruge dem direkte:

```sh
curl -s localhost:8765/api/state | python3 -m json.tool     # alt state i én JSON
curl -s localhost:8765/fb.png -o fb.png                     # live framebuffer
curl -s -XPOST localhost:8765/api/touch -d '{"x":350,"y":160,"ms":700}'   # hold på BREW
curl -s -XPOST localhost:8765/api/activity -d '{"name":"brew"}'
curl -s -XPOST localhost:8765/api/fault -d '{}'             # fejl-cycle
curl -s -XPOST localhost:8765/api/ack -d '{"profile":"no_ack"}'
curl -s -XPOST localhost:8765/api/speed -d '{"factor":8}'
curl -s -XPOST localhost:8765/api/pause -d '{"value":true}'
curl -s -XPOST localhost:8765/api/lang -d '{"value":"da"}'
curl -s -XPOST localhost:8765/api/save -d '{"param":"brew_temp","value":880}'
curl -s -XPOST localhost:8765/api/snapshot -d '{"label":"min_test"}'
curl -s -XPOST localhost:8765/api/gesture -d '{"presses":3}'
curl -s -XPOST localhost:8765/api/reboot                    # MCU-genstart
curl -s -XPOST localhost:8765/api/restart_ui                # UI-genstart (ved crash)
curl -s -XPOST localhost:8765/api/reset                     # frisk sandbox
```

Øvrige: `/api/garbage`, `/api/link {state:silent|up|closed}`, `/api/wand {on}`,
`/api/require_wand {value}`, `/api/boot_flag {value}`, `/api/runtime_switch`,
`/api/page {name}`, `/api/stock_lang {value}`, `/api/config {field,value,reload}`,
`/api/layout` (de rigtige tryk-mål i aktuelle visning), `/api/scenarios`,
`/exports/<fil>` (snapshot-filer).

---

## 4. Robusthed

* **UI-crash** → viewer'en viser rødt banner med exit-kode + stderr-udsnit og en
  **Genstart UI**-knap (`/api/restart_ui`). State-filen startes frisk ved genstart.
* **mcu_sim offline/død** → UI'en viser OFFLINE (forventet adfærd: link-timeout 1,5 s);
  viewer'en viser MCU-status + Reset-knap.
* **Pause** fryser MCU-tiden → UI'en viser OFFLINE (forventet); resume genopretter link.
* **Luk pty** → OFFLINE; **Genonline** åbner ny pty og genstarter automatisk UI'en
  med den nye pty-sti (en klient kan ikke skifte pty i live-drift).
* **MCU-genstart** → sim'en rebooter (registre nulstilles, versions-håndtryk + boot-push
  forventes igen). Reference-klienten detekterer 0x89-igen og re-handshaker + re-boot-pusher
  (lille bagudkompatibel tilføjelse til `ref_client.py`; scenarierne er uændrede).
* **Exit (Ctrl-C/SIGTERM)** → ctrl `exit` til UI+sim, proces-wait/terminate, sockets,
  FIFO, pty og sandbox ryddes. `--keep` bevarer sandbox til fejlfinding.
* **Writes** sker kun i sandbox (`sim/reports/play-*/`), designated exports
  (`sim/reports/play-exports/`) og en kort `/tmp/ourui_e2e_<pid>_*`-mappe til
  ctrl-sockets (AF_UNIX-stier må være ≤104 bytes på macOS; mappen ryddes ved exit).
* **Hastighed >1×**: kun MCU'ens sim-tid skaleres; UI-klienten kører stadig wall-clock.
  Keep-awake-tærsklen skaleres tilsvarende i sim'en, så der ikke kommer falske
  `keepalive_gap`-logs. Aktiviteter gennemføres hurtigere (8× ≈ 8× hurtigere kurver).

---

## 5. Hvad er tilføjet til sim-miljøet (bagudkompatibelt)?

| Fil | Tilføjelse |
|---|---|
| `mcu_sim.py` | ctrl `speed {factor}`, `pause {value}`, `require_wand {value}`; `ping` returnerer nu også speed/paused/profile/require_wand. Live hastighed/pause i hoved-loopet (1×/upauset = byte-identisk adfærd). Keep-awake-tærskel skaleres med speed (1× uændret). `do_reboot` nulstiller boot-push-detektion + first-RX, så boot-push/versions-håndtryk forventes igen efter reboot. |
| `ref_client.py` | 0x89 modtaget *efter* gennemført håndtryk = MCU-genstart → re-handshake + re-boot-push (som ved ny connect, PARITY §1.4). Påvirker ikke scenarier (ingen reboot i scenarierne). |
| `run_e2e.py` | `start_ui()` sætter `OURUI_LANGFILE=<sandbox>/new_customer/ourui/lang` (v6.3+-sprogfil; manglende fil = fallback EN, uændret adfærd for ældre builds). |
| `play.py`, `play_viewer.html`, `PLAY.md` | Selve play-mode-leverancen (nye filer). |

`selftest.py` (107 checks) og alle 22 scenarier er kørt igen efter ændringerne — se §7.

---

## 6. Begrænsninger

* **Fuld visuel play kræver v6.4-binæren.** Indtil den lander, er fb'et reference-klientens
  simple emulering (fase-ring + knapper + toast-region) — ikke ourui's rigtige grafik.
  Alle protokoller, state-felter, touch-mekanik og kontroller er dog de samme.
* **Reference-klientens knap-layout** er ikke ourui's rigtige layout. Mod SIM-build'et
  bruger play de rigtige tryk-mål fra ctrl `layout`; mod reference-klienten dets eget
  layout. Direkte klik/hold på fb'et virker altid (det er bare touch-koordinater).
* **Sprog:** ourui's UI-sprog (v6.3+) ligger i egen fil (`"en"`/`"da"`, `OURUI_LANGFILE`),
  ikke i config.json. Play skriver filen; mod SIM-build'et genstartes UI'en automatisk
  (filen indlæses ved boot). **DA findes ikke i stock's 11 sprog** (config-index 0..10 =
  EN/DE/FR/IT/ES/PL/AR/简中/繁中/KO/JA) — stock-sprog-rækken er config-only og påvirker
  ikke ourui's labels. Reference-klienten har ingen i18n (kun config-index i state).
* **Steam har to veje (v6.6, STEAMLIVE §5-6).** Display-steam (UI-knappen / ord24=1)
  kører **uden** wand-kontakt og auto-stopper ved mælk-setpoint. Wand-kontakten
  (PE.2, "Wand til/fra") starter en **flush** (ord24=0, ingen auto-stop) — også oven i
  et bryg (work_sta 0x5327) — og stopper ved wand-release. "Kræv wand for damp"-knappen
  er en legacy no-op fra v6.6 (display-steam gatedes ikke længere; SESSION §5.2's
  wand-krav blev live-modbevist).
* **Pause/offline:** UI'en viser OFFLINE når MCU-tiden fryses eller linket lukkes —
  det er korrekt adfærd (link-timeout 1,5 s), ikke en fejl.
* **Gestus-testen** kører `switcher_host` i fil-mode mod en syntetisk stream (samme
  metode som `gesture_switcher`-scenariet). Streamen injiceres også i UI'ens FIFO;
  hjørne-holds starter ingen aktivitet i UI'et (korrekt — switcher'en ejer gestus).
* **Ingen multi-touch/ghost-touches** — touch er syntetisk single-pointer (CONTRACT §4).
* **fb-refresh ~6 Hz** i viewer'en (PNG-konvertering i ren Python); SIM-build'et skriver
  selv fb ≤10 Hz (CONTRACT §2). Det er viewer-refresh, ikke UI-render, der er flaskehalsen.
* **Replay-mode** understøttes ikke i play (scenarie-replays køres med `run_e2e.py`).
  `--scenario` bruger kun scenariets `config`/`sim`/`boot_flag_initial` som udgangspunkt.

---

## 7. Verifikation

### Headless smoke (grøn NU, mod reference-klienten)

```sh
python3 sim/play.py --smoke --no-browser
# → [smoke] 30/30 PASS · klient=reference
```

Smoken starter hele miljøet, curl'er viewer-siderne (`/`, `/api/state`, `/fb.png`),
injicerer touch via API'et og asserter state-ændringer i JSONL:

1. Viewer-HTML + kontrakt-felter i state + PNG-magic i fb.
2. **BREW-hold via touch-API → fase PRE/EXT** i state-panel OG direkte i `state.jsonl`;
   fb-PNG'en ændrer sig under brew.
3. Hastighed 8× → **brew auto-stop** i sim-loggen.
4. **STEAM → auto-stop ved mælk-setpoint** (ord48≥ord25, stopgrund "mælk ≥ setpoint").
5. Fejl-cycle E1 → **ERROR-fase** → clear → READY.
6. **no_ack → GEM blokerer** (save=fail, config-md5 uændret) → normal → **GEM ok**
   (config.brewTemper=880 i sandbox, md5 ændret + logget, MCU-write sub11=[880]).
7. Sprog-fil = "da", boot-flag toggle i state, pause/resume, gestus (3× hold udløser
   switcher), touch-tap raw-mapping (240→2048), snapshot-eksport (PNG+state findes),
   reset → frisk session.

### Fuld visuel smoke (GRØN — v6.4-binæren landet 2026-11-17 16:04)

**Samme kommando** — `--smoke` auto-detekterede binæren og kørte de samme checks mod
det rigtige UI:

```sh
python3 sim/play.py --smoke --no-browser
# → [smoke] 30/30 PASS · klient=sim-build   (2026-11-17 16:08)
#   binær: build/ourui_sim · md5 354976e839a26663310493446c7a3f97
```

De visuel-relevante checks mod det rigtige UI:

* **BREW via touch-hold på det rigtige tryk-mål (210,372 via ctrl `layout`) → fase
  PRE/EXT i state + fb ændrer sig** (rigtig grafik).
* **STEAM → auto-stop ved target** (mælk ≥ setpoint ord48≥ord25, sim-log + state).
* **Settings-GEM → config i sandbox ændres (brewTemper=880) + md5-log** +
  word-for-word MCU-write sub11=[880].

Derudover verificeret i en **ægte browser** (headless Chromium mod live play-session):
viewer renderer det rigtige Meraki+-UI (danske labels, gauges, KLAR-badge); ægte
musse-hold ≥650 ms på BREW-knappen i fb-billedet → **EXT-fase**; højreklik →
**GESTUS UDLØST** (ægte switcher_host); tastatur `e` → ERROR → clear → READY;
knap-zoner/tryk-mål-overlay + koordinat-overlay live. Screenshots + smoke-log:
`sim/reports/play-exports/verification-20261117/` (`play_real_ui.png`,
`play_real_brew.png`, `play_viewer_top.png`, `smoke_simbuild.log`).

Kør også hele E2E-suiten mod binæren for fuld kontrakt-dækning (Fable's leverance):

```sh
python3 sim/run_e2e.py --all --ui-bin build/ourui_sim
```

### Bagudkompatibilitet (kørt efter ændringerne)

```sh
python3 sim/selftest.py                 # 107 PASS / 0 FAIL
python3 sim/run_e2e.py --all            # 22/22 scenarier (reference-klient)
```

Interaktiv server-test (28 checks: touch→JSONL, MCU-reboot→re-handshake+boot-push,
pty-luk→OFFLINE→reopen+UI-genstart, UI-crash→banner→genstart, speed/pause/ACK live,
sprogfil, snapshot-servering, Ctrl-C/SIGTERM-cleanup) er kørt grønt mod en live
play-session under udviklingen.

---

## 8. Fejlfinding

| Symptom | Årsag / løsning |
|---|---|
| `kan ikke binde port 8765` | Porten er optaget — brug `--port <anden>`. |
| Viewer viser "REFERENCE-KLIENT"-banner | v6.4-binæren findes ikke endnu (forventet). Læg den i `build/ourui_sim` eller brug `--ui-bin`. |
| UI-crash-banner | Klik "Genstart UI"; stderr-udsnit vises i banneret. Fuld stderr ligger i play-processens output. |
| Touch gør intet | Hold ≥650 ms på en aktivitets-knap (tap giver kun "HOLD ≥650 ms FOR START"-toast i reference). Mod SIM-build: brug det rigtige knap-layout. |
| Steam starter ikke | "kræv wand for damp" er slået til og wand er fra — slå wand til eller kravet fra. |
| Alt virker trægt ved 8× | MCU-tiden er 8× hurtigere, men UI polls/render er wall-clock — forventet. |
| Sandbox væk efter exit | Ctrl-C rydder som designet; brug `--keep` for at bevare den, eller snapshot-eksport før exit. |

*— test-agent, 2026-11-17. Alt kørt lokalt; enheden ikke berørt; Fable's filer ikke rørt.*