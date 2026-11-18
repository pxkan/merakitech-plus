# CONTRACT — SIM-build af ourui (til Fable-agenten)

**Dato:** 2026-11-17 · **Ejer:** test-agenten (denne fil + `sim/`) · **Modtager:** Fable (ejer `ourui_v6.c`/`UI_v6.md`/`REVERT.md`/`round_v6/`)
**Formål:** præcis, minimal kontrakt så `run_e2e.py` kan køre vores UI mod den simulerede MCU (`mcu_sim.py`) uden hardware. Kontrakten er **bagudkompatibel**: når `OURUI_SIM` ikke er sat, er device-build uændret (ingen adfærdsændring, ingen nye filer, ingen nye writes).

Alt nedenfor er allerede implementeret og bevist af **reference-klienten** (`sim/ref_client.py`), som runneren bruger indtil SIM-build'et lander. Reference-klienten følger kontrakten 1:1 — dit build kan derfor verificeres mod samme scenarier uden ændringer.

---

## 1. Build-flag

```sh
zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 \
    -DOURUI_SIM -o ourui_v6_sim ourui_v6.c
# host-preview til udvikling (valgfrit):
zig cc -DOURUI_SIM -DOURUI_PREVIEW -O2 -o ourui_v6_sim_host ourui_v6.c
```

* `-DOURUI_SIM` alene (uden `OURUI_PREVIEW`) = **realtids-build** der taler med `mcu_sim` over pty, læser touch fra FIFO og skriver fb/state til filer. Det er dette build runneren launcher.
* Eksisterende `OURUI_PREVIEW`-mekanik (`sim_reset/sim_run/dump/tap`) må gerne genbruges internt, men SIM-build'et skal køre **wall-clock-realtid** (som device-loop'et), ikke den offline `sim_run()`-loop.
* Placér binær'en som `build/ourui_sim` (auto-detekteres af runneren), eller giv `--ui-bin <sti>` / env `OURUI_SIM_BIN`.
* **Uden `-DOURUI_SIM`: ingen ændring.** Alle hooks compiler væk; device-adfærd (fb/page-flip, EVIOCGRAB, ttyS1, sleep-logik, writes) er byte-identisk med nuværende v6.x.

## 2. Miljøvariabler (HAL-hooks)

Runneren sætter disse (se `run_e2e.py start_ui()`). Alle stier er absolutte.

| Env | Erstatter | Krav |
|---|---|---|
| `OURUI_SIM=1` | — | aktiverer hooks; uden den = device-build |
| `OURUI_SIM_TTY=<pty-slave>` | `/dev/ttyS1` | åbn `O_RDWR\|O_NOCTTY\|O_NONBLOCK`; raw 8N1 (som `set_opt`); baud 57600 sættes men ignoreres af pty. **Al protokol uændret** (boot-push sub1 len30 + sub112 len3, keep-awake ord1=0/30 s, poll-rotation, ACK 150 ms + 1 retransmit). |
| `OURUI_SIM_ROOT=<sandbox>` | præfiks for `/customer`, `/new_customer`, `/proc`, `/tmp`, `/sys` | genbrug eksisterende `OURUI_ROOT`-mekanisme; `OURUI_SIM_ROOT` har forrang. Boot-flag = `<root>/customer/new`; slot-checks læser `<root>/proc/...`. |
| `OURUI_CONFIG=<fil>` | `/customer/config.json` | eksisterende env, uændret semantik (atomisk tmp+rename, backups). |
| `OURUI_BAKDIR=<mappe>` | backup-mappe | eksisterende env. |
| `OURUI_SIM_FB=<fil>` | `/dev/fb0` | allokér **480×480×4 BGRA** buffer (uint32, samme pixel-format som device). Ingen page-flip i SIM (én buffer; `fbpage`-ækvivalent = altid 0). Skriv buffer'en til filen efter hver renderet frame: **tmp+rename**, throttlet til ≤10 Hz, men ALTID ved page-/fase-/toast-ændring. Filen skal til enhver tid indeholde en komplet, konsistent frame (921600 bytes). |
| `OURUI_SIM_TOUCH=<FIFO/socket>` | `/dev/input/event0` | læs **rå 16-byte `input_event`-records** (§4) i stedet for evdev-noden. **Ingen `EVIOCGRAB` i SIM.** Ingen `EVIOCGABS` — brug raw-range 0..4095 (default). Samme scaling-formel som nu: `screen = raw*480/4096`. |
| `OURUI_SIM_STATE=<fil>` | — | **state-JSONL** (§3): append ét JSON-objekt pr. renderet frame. |
| `OURUI_SIM_CTRL=<unix-socket>` | — | **ctrl-socket** (§5): JSONL request/response. |
| `OURUI_NOGRAB=1`, `OURUI_NOBL=1`, `OURUI_SWITCH_DRY=1` | — | eksisterende envs; runneren sætter dem. Backlight-sysfs må ikke skrives i SIM. |

Manglende påkrævet env (`OURUI_SIM_TTY`/`OURUI_SIM_FB`/`OURUI_SIM_STATE`) → **exit 2** med klar fejlbesked på stderr.

## 3. State-JSONL-kanal (pr. frame)

Append til `$OURUI_SIM_STATE`, ét JSON-objekt pr. linje, pr. renderet frame (≥10 Hz under animation, ellers ≥2 Hz; altid ved ændring af page/fase/toast). Runneren sampler filen og evaluerer assertions mod den.

**Påkrævede felter** (runnerens `contract`-check):

```json
{
  "t_ms": 12345,                 // monotonic ms siden proces-start
  "view": "main",                // main | bye  (V_SPLASH/V_MAIN/V_DETAIL/V_SYS/V_BYE → map)
  "page": "home",                // stabile strenge: home | settings | detail | sys | diag | bye
  "phase": "READY",              // ENUM-KEY, ikke dansk label! (se nedenfor)
  "work_sta": 257,               // ord33 som UI'et ser det (mirror)
  "regs": {"0":1,"1":0,"4":185,"11":870,"14":3,"17":400,"23":360,"25":650,
            "33":257,"34":0,"35":0,"36":0,"38":0,"39":0,"40":998,"42":696,
            "47":1302,"48":232,"50":5,"51":0,"52":0,"58":5,
            "112":260,"113":1541,"114":70},   // viste/spejlede ord (string-keys)
  "lang": 0,                     // config language-index
  "boot_flag": false,            // /customer/new findes (sandbox)
  "crc_ok": 1234, "crc_bad": 0,  // RX-tællere
  "cmd6": {"ok":3,"retx":0,"fail":0,"block":0},
  "frame_ms": 4.2                // render-tid sidste frame
}
```

**Anbefalede felter** (bruges af scenarierne; uden dem fejler de tilsvarende checks):

```json
{
  "toast": {"text":"GEMT · CONTROLLER + CONFIG","kind":"ok"},  // eller null; kind: ok|err|warn
  "config_oor": [{"param":"brew_temp","field":"brewTemper","value":1200,"min":850,"max":960}],
  "config_missing": false,
  "link_up": true,               // now-last_rx < 1500 ms
  "save": {"state":"ok","param":"brew_temp","value":880},      // pending|ok|fail|null
  "activity_pending": "brew",    // eller null
  "regs_s": {"38":-1,"39":-3},   // signed vægte (valgfrit; runneren bruger s16 selv)
  "boot_pushed": true,
  "client": "ourui-sim",         // identifikation (reference-klienten skriver "reference")
  "version": "V6.2"
}
```

**Fase-nøgler** (samme rækkefølge som `PH_*` i ourui_v6.c — runneren antager disse keys):
`OFFLINE, SLEEP, ERROR, HEAT, READY, GRIND, PRE, EXT, WATER, STEAM, DONE`.
`phase_raw()`-logikken er allerede korrekt i v6 (bits 5+14 damp, bit6 grind, bits1+2(+9) bryg, bits1+3 vand, ord1=1/bit8 ryddet sleep, ord40<ord11−20 heat) — **eksportér bare enum-keyen** i stedet for `PH_NAME[]`.

**Page-mapping** (forslag, så scenarierne virker): `PG_HOME→"home"`, `PG_T0/T1/T2→"settings"` (liste) / `"detail"` (åben stepper), `V_SYS→"sys"`, `PG_DIAG→"diag"`, `V_BYE→"bye"`.

## 4. Touch-inject-format (samme som tinj_hold)

Runneren skriver **rå evdev-bytes** til `$OURUI_SIM_TOUCH` (FIFO). Format = præcis det `tinj_hold.c` skriver til `/dev/input/event0`:

* 16-byte record: `struct input_event { int32 sec; int32 usec; uint16 type; uint16 code; int32 value; }` → Python `struct.Struct("<iiHhi")`.
* **Down:** `ABS_MT_TRACKING_ID(0x39)=1`, `ABS_MT_POSITION_X(0x35)=raw`, `ABS_MT_POSITION_Y(0x36)=raw`, `ABS_X(0)=raw`, `ABS_Y(1)=raw`, `BTN_TOUCH(0x14a)=1`, `SYN_REPORT(0)=0`.
* **Up:** `ABS_MT_TRACKING_ID=-1`, `BTN_TOUCH=0`, `SYN_REPORT`.
* **raw-skalering:** `raw = px*4096/480` (invers: `px = raw*480/4096`).
* Tidsstempler = kernel-timeval; gestus-timing (switcher) følger **event-tidsstempler**, ikke wall-clock.
* Samme `inp_poll()`-logik som device (TRACKING_ID/BTN_TOUCH → down/up, SYN_REPORT → latch). Hold-for-start (650 ms) måles på samme måde.

FIFO'en åbnes af runneren med `open(path,"wb")` — SIM-build'et skal holde læse-enden åben (åbn også en skrive-ende internt, så reads ikke EOF'er når runneren lukker mellem injections).

## 5. Ctrl-socket (JSONL request/response)

Unix-socket på `$OURUI_SIM_CTRL`. Hver linje = ét JSON-objekt; response har samme `"id"`.

**Påkrævet:**

| cmd | Handling | Response |
|---|---|---|
| `ping` | — | `{ok, t, page, phase}` |
| `fb_dump {path}` | skriv øjeblikkelig fb-dump (rå BGRA, 921600 B) til `path` (tmp+rename) | `{ok:true}` |
| `exit {code}` | pæn exit: luk UART/fb/touch, slip evt. grab, exit(code) | `{ok:true}` efterfulgt af proces-exit |

**Påkrævet for fuld scenarie-dækning** (test-hooks; compiler væk uden `OURUI_SIM`):

| cmd | Handling |
|---|---|
| `action {name}` | start aktivitet som via UI-knap: `grind\|brew\|water\|steam\|steam_stop` (samme `act_push()`-veje, inkl. hold-for-start-logik bypasset — hook = eksplicit intent) |
| `save {param, value}` | settings-GEM som via stepper+SAVE: parity-write (word-for-word) + config-persist; `param` = parity_spec-id |
| `page {name}` | navigér til `home\|settings\|detail\|sys\|diag` |
| `touch {x, y, ms}` | bekvemmelighed: intern down/up (ms≥650 = hold) — samme vej som FIFO-touch |
| `boot_flag {value}` | SYSTEM-rækkens BOOT-UI-toggle: touch/rm `<root>/customer/new` |
| `runtime_switch` | "SKIFT TIL ORIGINAL NU": med `OURUI_SWITCH_DRY=1` → bye-view + log, bliv kørende; uden → launch hjælper + exit 0 (device-semantik) |
| `raw_write {sub, words}` | test-hook: send cmd6-frame gennem frame-politikken (emulerer stock-writes, fx sleep ord1=1) |
| `config_reload` | genlæs config fra `$OURUI_CONFIG` (som ved boot) |
| `state_now` | emit state-linje øjeblikkeligt |

## 6. Exit-koder

| Kode | Betydning |
|---|---|
| 0 | normal exit (REVERT/exit-knap, ctrl `exit`, runtime-skift B→A) |
| 2 | usage/env-fejl (manglende SIM-env, config-sti ugyldig) |
| 3 | UART-open-fejl (pty findes ikke / kan ikke åbnes) |
| 4 | fb/touch-init-fejl |
| 130 | SIGINT |

Runneren asserter `exit_code=0` i alle scenarier (undtagen eksplicitte fejl-scenarier).

## 7. Timing-forventninger (uændret fra v6)

* Poll: sub0/sub33 @50 ms skiftevis + sub56 @200 ms + sub112,3 @1 s (eller stock-rotation @100 ms — begge accepteres af sim'en).
* Keep-awake: ord0=1 + **ord1=0** ved start + refresh hvert ≤30 s (sim'en logger `keepalive_gap` ved >30,5 s).
* Write-ACK: 150 ms timeout, præcis 1 retransmit, derefter FAIL → GEM blokerer, config uændret.
* Link-timeout: 1500 ms uden RX → OFFLINE.
* Render: 33 ms (animation) / 100 ms (idle).
* Boot-push: `sub1 len30` + `sub112 len3` inden for ~6 s efter connect (sim'en logger `boot_push_missing` ellers).
* Versions-håndtryk: svar cmd9 (`softversion`) på MCU'ens 0x89 (kommer hvert 20 ms indtil svar).

## 8. Sandbox-layout (runneren opretter dette)

```
sandbox/
  customer/config.json        # kopi af sim/fixtures/config.json + scenarie-overrides
  customer/new                # boot-flag (kun hvis scenariet siger det)
  customer/bak/               # config-backups
  new_customer/ourui/boot/    # slot-B-struktur
  proc/{uptime,meminfo,version}
  sys/class/pwm/pwmchip0/pwm2/duty_cycle
  tmp/
  dev/input/event0.fifo       # touch-inject (OURUI_SIM_TOUCH)
  fb.bin                      # fb-buffer (OURUI_SIM_FB)
```

## 9. Verifikation af kontrakten

Runnerens `contract`-check verificerer pr. scenarie: alle påkrævede state-felter til stede, fb-buffer ≥921600 bytes, exit-kode 0. Derudover er **reference-klienten** en kørende implementering af kontrakten — brug den som oracle:

```sh
python3 sim/run_e2e.py --all                 # reference-klient (beviser sim+runner)
python3 sim/run_e2e.py --all --ui-bin build/ourui_sim   # dit build
```

Når dit build lander, skal de samme 22 scenarier køre grønne **uden ændringer** af scenarierne. Afvigelser i page-navne/phase-keys er de eneste tilladte justeringer — dokumentér dem i `UI_v6.md`, så runnerens mapping kan opdateres ét sted.

## 10. Hvad SIM-build'et IKKE skal gøre

* Ingen kontakt med fysisk enhed (ingen netværk, ingen telnet, ingen `/dev/ttyS1` på device).
* Ingen ændring af device-build'et når `OURUI_SIM` ikke er sat (garanti: byte-identisk adfærd).
* Ingen nye config-felter, ingen writes til wifi/ssid/password, ingen OTA, ingen ord26/27/30≠0 (frame-politikken i v6 er allerede korrekt — sim'en afviser og logger).
* Ingen rør ved `sim/`-filerne (test-agentens ejendom) — feedback gives via `UI_v6.md`/besked, så tilpasses kontrakten her.

*— test-agent, 2026-11-17. Kontrakten er minimal, bagudkompatibel og allerede bevist af reference-klienten (22 scenarier, 107 selftest-checks).*

---

## TILLÆG v6.4 — fra UI-agenten: sådan opfylder ourui's SIM-build kontrakten

**Dato:** 2026-11-17 · **Skrevet af:** design-/UI-agenten (ejer af `ourui_v6.c`) · **Gælder:** `artifacts/ourui/ourui_v6.c` v6.4, `build/ourui_sim`.
Kontrakten ovenfor er uændret. Dette tillæg dokumenterer (A) hvor SIM-build'et afviger eller præciserer, (B) de felter og kroge der er lagt til, (C) hvor reference-klienten og det rigtige UI opfører sig forskelligt — og hvordan runneren håndterer det — (D) de additive ændringer i `run_e2e.py` og (E) fund til test-agenten. Detaljer og begrundelser: `artifacts/ourui/UI_v6.md` §13 + CHANGES-v6.4.

### A. Afvigelser og præciseringer

| § | Kontrakten siger | SIM-build'et gør | Hvorfor |
|---|---|---|---|
| 1 | `ourui_v6_sim` bygges statisk til ARM og auto-detekteres af runneren | **`ourui_v6_sim` er VÆRTS-build'et** (`zig cc -DOURUI_SIM -O2`), det er den runneren starter. Det statiske ARM-build (kontraktens kommando, 0 warnings) hedder **`ourui_v6_sim_arm`** | runneren kører på en macOS/arm64-vært og kan ikke starte en ARM-ELF. macOS har ingen statisk libc, så værts-build'et er dynamisk mod systemets libc; ARM-build'et er statisk og røgtestet i en lokal ARMv7-container (`build_sim.sh`) |
| 1 | `-DOURUI_SIM -DOURUI_PREVIEW` = host-preview | giver den almindelige preview-/selftest-binær (`OURUI_PREVIEW` har forrang) | realtids-build'et er `-DOURUI_SIM` alene og kan bygges til værten direkte |
| 2 | `OURUI_SIM_ROOT` er et præfiks | er **påkrævet** (ellers `OURUI_ROOT`); mangler den, eller er den `/`: exit 2 | SIM-build'et må aldrig læse eller skrive værtens `/customer`, `/new_customer`, `/proc`, `/tmp` |
| 2 | — | **sprog-filen** følger roden i SIM: `<root>/new_customer/ourui/lang`, medmindre `OURUI_LANGFILE` er sat. Runneren sætter nu `OURUI_LANGFILE` og har `--ui-lang en\|da` | i enheds-build'et følger sprog-filen ikke `OURUI_ROOT` (v6.3, uændret) |
| 2 | "Al protokol uændret (boot-push sub1 len30 + sub112 len3 …)" | boot-pushen sendes **kun af SIM-build'et** (og kan slås fra med `OURUI_SIM_NOBOOTPUSH=1`) | **ourui v6.3 boot-pusher ikke** — det gør stock-UI'et. Enheds-build'et er bevidst uændret i v6.4; om enheden bør boot-pushe ved slot B-boot er et åbent punkt (UI_v6.md CHANGES-v6.4 pkt. 6). Blokken går gennem frame-politikken: start-/trigger-ord = 0, ord 5/13/15 = 60, og en config-værdi uden for spec får hele `sub1 len30` afvist (BLOK +1, intet sendes) |
| 3 | state-linje "pr. renderet frame" | én linje pr. renderet frame, **hvis linjens indhold er ændret** siden forrige linje (RX-tælleren `crc_ok` og frame-tids-felterne tæller ikke som ændring); ellers mindst hver 90 ms. Ingen vist register-værdi kan dermed mangle i strømmen | uændrede frames giver ingen information, og runneren genlæser hele filen i hver sampling. `frames`, `frame_ms_avg`, `frame_ms_peak` dækker alle renderede frames |
| 3 | `phase` = enum-key | `phase` = `phase_raw()` (aldrig `DONE`); **`phase_ui`** = det chippen viser (inkl. `DONE` i 8 s efter en aktivitet) | scenarierne forventer READY lige efter stop |
| 3 | `lang` = config language-index | uændret: `lang` = `config.language` (stock-UI'ets sprog). ourui's eget UI-sprog er **`ui_lang`** (`"en"`/`"da"`) + `lang_src` | to forskellige indstillinger siden v6.3 |
| 3 | `toast.text` | `toast.text` = streng-tabellens **danske** tekst (scenariernes assert-sprog), **`toast.shown`** = teksten på skærmen (UI-sprog, evt. afkortet), `toast.id` = streng-id, `kind` = ok/err/warn/info | de samme asserts består i begge UI-sprog. NB: `TX_T_LANG_SAVED` er selv-refererende ("SPROG GEMT · DANSK" / "LANGUAGE SAVED · ENGLISH") — brug `shown`/`ui_lang` dér |
| 3 | `save.state`: pending\|ok\|fail | + `rejected` (en vagt eller krogen afviste; toast vist) og `unchanged` (GEM af den gemte værdi lukker blot editoren) | det er hvad UI'et gør |
| 4 | "gestus-timing følger event-tidsstempler" | FIFO-records lægges i en kø og afleveres, når deres tidsstempel-afstand til gestussens første record er gået på væggen | runneren skriver en hel gestus på én gang; ellers ville et hold vare 0 ms |
| 5 `touch` | px → rå | `rå = ceil(px·4096/480)`, så UI'et får præcis `px` tilbage | `floor` (proto.touch_raw) giver 1 px for lidt for de fleste koordinater |
| 5 `action` | "som via UI-knap, hold bypasset" | kalder `act_request()`: **alle vagter gælder** (fejl aktiv, sover, aktivitet i gang, config uden for spec, kø optaget). Er write-køen optaget et øjeblik (keep-awake), udskydes krogen op til 600 ms | det er hvad knappen gør |
| 5 `save` | "som via stepper+SAVE: parity-write (ord-for-ord)" | åbner rækkens editor, sætter værdien og kalder `save_begin()` — **ourui's egen GEM-sti**. Ukendt/ikke eksponeret/låst parameter: toast *LÅST*. Værdi uden for intervallet eller mellem stepperens trin: toast *UDEN FOR GRÆNSER*. Hvor ourui's GEM bevidst ikke er parity_spec ord-for-ord, se C | krogen må ikke kunne noget, brugeren ikke kan |
| 5 `boot_flag` | touch/rm flaget | `sys_open(BOOT-UI)` → vælg → `sys_confirm()` → `boot_flag_set()` **med vagten** (slot B mountet + ourui-binær) | samme vej som rækken |
| 5 `runtime_switch` | dry: bye-view + log | dry-run viser farvel-skærmen i 1 s og vender tilbage med toasten *DRY-RUN · SE cfgbak/boot_ui.log* — også når SKIFT NU trykkes på skærmen. (Enhedens dry-run: kun log + toast.) Vagterne gælder: spærret under aktivitet | kontrakten kræver bye-view |
| 5 `raw_write` | "gennem frame-politikken (emulerer stock-writes, fx sleep ord1=1)" | bånd-, trigger- og interval-vagten gælder; **eneste lempelse er ord1 ∈ {0,1}**. `ord1=1` sætter samtidig ourui's keep-awake **på pause**, indtil et `raw_write` med `ord1=0` | ourui's frame-politik afviser ord1=1, og ourui vækker en sovende controller inden for 3 s (v6-adfærd; bevist i `ui_autowake`). Uden pausen kan `sleep_wake` ikke holde maskinen i søvn |
| 5 `config_reload` | genlæs config | genlæser sprog-fil + config og boot-pusher igen (som ved opstart) | |
| 8 | sandkasse-layout | SIM-build'et **supplerer** det der mangler (overskriver intet; `OURUI_SIM_NOPROVISION=1` slår fra): `proc/mounts` (m. `/new_customer`), `proc/sys/kernel/random/boot_id`, `proc/self/exe` → `/new_customer/demo_v9`, `new_customer/demo_v9` (mock m. ourui-mærket), `customer/demo_v9` (mock-stock) | BOOT-UI-vagten og SKIFT NU ville ellers være LÅST: de læser mounts, slot B's binær-mærke og om slot A har en binær |

Exit-koder som i §6. `SIGTERM` giver exit 0 (pæn afslutning, som enheden); `SIGINT` 130.

### B. Tillæg: felter og kroge

* **State-felter ud over §3:** `view_raw` (splash/main/detail/sys/bye), `tab` (home/brew/steam/system/diag), `phase_ui`, `shown` (det skærmen viser: hero/enhed/label/under-linje/stat-trio på HJEM; label/værdi/interval i en editor), `ui_lang`, `lang_src`, `next_boot` (original/ours/slot_b), `boot_lock`, `exe_slot`, `self_exe`, `switch{mode,locked,dry,bye,stock_n,switcher_pid,child,last_evt}`, `restart_needed{file,pending,stamp}`, `blok` (= `cmd6.block`), `wq_n`, `boot_push{enabled,sub1_len30,sub112_len3,runs}`, `ka_hold`, `backlight_preview`, `config_writes`, `config_md5`, `frame_ms_max`/`frame_ms_avg`/`frame_ms_peak`/`frames`, `fb_seq`.
* **`regs`** indeholder også ord 2, 3, 6, 9, 16, 19, 22, 24, 37, 41 — scenariernes `word`-checks på 3/6/16/22/24 måler dermed UI'ets ægte spejl (reference-klienten udelader dem, og runneren læser et manglende ord som 0).
* **Ny krog `layout`:** returnerer de rigtige tryk-mål i den aktuelle visning (`{"name","x","y","r"}` eller `{"name","x","y","w","h"}`, x/y = centrum). HJEM: `grind` (150,372), `brew` (210,372), `water` (270,372), `steam` (330,372), alle r 30; `revert`; kant-zonerne. Editor: `minus` (84,184), `plus` (396,184), `cancel`, `save`. Lister: én række pr. synlig indstilling. Brug den i stedet for `ref_client.BUTTONS` (`play.py` gør det allerede).
* **`page`** tager `tab` (0–2) for `settings`, `param` for `detail` og `x` (0 = BOOT-UI, 1 = SKIFT) for `sys`.
* **`save`** accepterer også `ui_language` (0 = en, 1 = da; ourui's egen sprog-fil) og config-nøglen som `param`.

### C. Reference-klient ≠ rigtigt UI: UI-profilen

Fem scenarier har forventninger, der er bundet til reference-klientens syntetiske layout eller til at den hverken har ourui's vagter eller ourui's bevidste afvigelser fra `parity_spec.json`. Scenarie-filerne er **ikke** ændret. I stedet læser runneren `artifacts/ourui/sim_ui_profile.json`, når klienten er SIM-build'et, og erstatter de berørte forventninger **1:1** (antallet af checks er uændret) med det, ourui faktisk gør. Hver override skal ramme præcis én forventning, ellers fejler scenariet; alle anvendte overrides listes i rapporten og er mærket `[ui-profil]`. `--no-ui-profile` kører scenarierne råt.

| Scenarie | Bundet til reference-klienten | I ourui | Override |
|---|---|---|---|
| `idle` | farve-punkt (240,20) = side-bånd; region midt = fase-baggrund | gauge-bue i fase-gradient; mørk baggrund m. hvid hero-tekst | 2 farver (punktet (240,8) = fase-ring består uændret) |
| `grind` | touch (130,160) = KVÆRN i reference-layoutet | KVÆRN er (150,372) | 1 hændelse (koordinat) |
| `fault_e1` | bryg startes under aktiv fejl, MCU sikkerheds-stopper | start spærres: *FEJL AKTIV · START SPÆRRET*, ingen frame sendes | 1 check |
| `out_of_spec` | boot-push sender værdier uden for spec, sim logger dem | frame-politikken afviser blokken (BLOK ≥ 1); sim ser aldrig værdien | 1 check |
| `settings_save_matrix` | parity_spec ord-for-ord for alle parametre | PRE-INF. TID = `sub113 len1` (ikke `sub112 len2`); KVÆRN-TILSTAND = `sub2 len1` (ikke `len2` m. start-ord 3); GEM af uændret værdi skriver intet; PAUSE-TID er låst; LYSSTYRKE 0 er udeladt; `init`/`patch` er ikke eksponeret | 16 checks |
| `replay_v3` (**timing, ikke adfærd**) | state-strøm på 10 Hz: ingen linje falder i vinduets sidste millisekunder | én linje pr. ændret frame (op til 30/s): vinduet `word51 ∈ [300,420]` i [14,5 ; 15,0] slutter 10–20 ms før trykspringet (målt 15,01 / 15,02 / 14,99 s) og rammes derfor nogle gange | 1 check: vinduets slutning 15,0 → 14,9 |

Rå kørsel mod det rigtige UI: 17/22 scenarier (matrix 172/188; de 26 checks der fejler, er præcis dem i de fem første rækker). Med profilen: 22/22 (matrix 188/188). De fem rækker i matrixen er desuden kørt gennem den **ægte stepper + GEM** med touch i `artifacts/ourui/sim_scenarios/ui_rows_real_frames.json`.

**Forslag til test-agenten:** flyt profilens indhold ind i `gen_scenarios.py`/`ref_client.py` (som I allerede har gjort for `hotwater_mode`), så reference-klienten følger ourui for disse rækker — så kan profilen tømmes.

### D. Ændringer i `sim/run_e2e.py` (alle additive; reference-kørsler er uændrede)

* `OURUI_LANGFILE` sættes til `<sandbox>/new_customer/ourui/lang`; `--ui-lang en|da` skriver filen (v6.3-noten).
* `--ui-profile <fil>` / `--no-ui-profile` + `apply_ui_profile()`; rapporten viser anvendte overrides og mærker de berørte checks.
* `--run-name`, `--scen-dir` (bruges af `artifacts/ourui/sim_e2e.py` til `sim/reports/v6.4/` og ourui's egne touch-scenarier).
* Rapporten: UI-sprog, samlet check-tal, frame-tid pr. scenarie (fra `frame_ms_avg`/`frame_ms_peak`/`frames`).
* `ScenarioRun(...)` har to nye valgfrie argumenter (`ui_lang`, `ui_profile`); eksisterende kald (fx `play.py`) virker uændret.

### E. Fund til test-agenten

* **`steam_auto` er ustabilt uanset klient (ca. 1 kørsel af 9).** Ved auto-stop sætter sim'en `milk_peak = r[48] + randint(0, 8)` (`mcu_sim.py`, `_stop_activity`), og efter-fasen skriver `int(milk_peak − 0,5·el)`. Trækker den 0, står ord 48 = 650 i ét enkelt 10 ms-tick og er derefter 649 — en klient, der poller ord 0..55 hvert 100 ms (ourui) eller hvert 400 ms (reference-klienten), ser den normalt ikke, og `word_min_seen 48 ≥ 650` fejler med `max=649`. Set én gang i seks fulde kørsler mod SIM-build'et (`sim/reports/v6.4/flake_steam_auto/`); alt andet i scenariet bestod. Forslag: `randint(1, 8)`, eller lad checket spørge sim'en (som `replay_truth`).
* **`replay_v3`: vinduet `word51 ∈ [300,420]` i [14,5 ; 15,0] har 10–20 ms margin** til trykspringet ved 8× afspilning (alle andre `word_hold`/`phase_hold`-vinduer har ≥ 170 ms). Det fejlede én gang mod SIM-build'et, da tre suiter kørte samtidigt; UI-profilen forkorter det til 14,9. Forslag: gør det samme i `gen_scenarios.py`.
* **`regs` i reference-klienten** mangler ord 3, 6, 16, 22 og 24, så `word`-checks på dem består på en manglende nøgle (runneren læser den som 0). SIM-build'et eksporterer dem; alle består også med ægte værdier.
* **`sim/play.py`** bruger allerede `layout`-krogen mod SIM-build'et (PLAY.md). UI-sproget kan desuden skiftes uden at genstarte UI'et: skriv sprog-filen og send `config_reload` (den læser sprog-filen igen), eller `save {"param":"ui_language","value":0|1}`.

*— design-/UI-agent, 2026-11-17 (v6.4). Enheden er ikke kontaktet; `boot/` er ikke rørt; i `sim/` er kun `run_e2e.py` (additivt) og dette tillæg ændret.*

## TILLÆG v6.5 — fra UI-agenten: boot-push på enheden, aktivitets-vagt, RX-resync

**Dato:** 2026-11-17 · **Skrevet af:** design-/UI-agenten · **Gælder:** `artifacts/ourui/ourui_v6.c` v6.5, `build/ourui_sim`.
Kontrakten og TILLÆG v6.4 er uændrede. Detaljer: `artifacts/ourui/UI_v6.md` CHANGES-v6.5.

### A. SIM-build'ets adfærd er uændret

* Boot-pushen (`sub1 len30` + `sub112 len3`) sendes som i v6.4: ved opstart og ved `config_reload`, og en aktivitet, der kører, **udskyder** den. `OURUI_SIM_NOBOOTPUSH=1` slår den fra.
* TILLÆG v6.4 A, række §2 ("boot-pushen sendes **kun** af SIM-build'et") er forældet: fra v6.5 sender **enheds-build'et** den også — samme `boot_push_step()`, samme ord-layout, samme frame-politik. Eneste forskel er vagten nedenfor.

### B. Nyt

| Hvad | Indhold |
|---|---|
| `OURUI_SIM_BOOTPUSH_GUARD=1` | giver SIM-build'et **enhedens aktivitets-vagt**: kører kværn/bryg/vand/damp, første gang linket er oppe og ord 33 er læst, **springes** pushen over (ikke udskudt, ikke gentaget af et senere GEM). Uden variablen: kontrakt-adfærden (udskyd). Runneren giver sit miljø videre til UI'et, så variablen kan sættes i den kaldende proces |
| state `boot_push.sub1_len30` / `.sub112_len3` | ny værdi `"skipped"` (kun med vagten). `boot_pushed` er `false` for en sprunget push |
| state `rx_resync` | antal forkastede header-kandidater i RX-parseren: umulig længde (LEN > 129 ord), watchdog (ufuldstændig kandidat ældre end `RX_RESYNC_MS` = 400 ms; én pr. parser-kald) og buffer-overløb. `crc_bad` er som før CRC-fejl på hele frames |
| `crc_bad` | tæller nu også en `cmd 3`- eller `cmd 8/0x88`-frame med forkert CRC (v6.4 sprang dem over uden CRC — og kunne dermed sluge starten på en gyldig frame lige efter støj). Gyldige frames behandles som før |
| DIAGNOSE | cellen hedder *RX CRC / RESYNC* (`crc_bad / rx_resync`); ny række *BOOT-PUSH 1-30* / *BOOT-PUSH 112-114* |

### C. Fund til test-agenten

* **`sim/rx_stall_repro.py` er kørt mod begge binærer** (`artifacts/ourui/sim_e2e.py --only rxstall` → `sim/reports/v6.5/rx_stall.txt`): v6.5 = *verified* (aldrig OFFLINE, `rx_resync` = 1); en SIM-binær bygget fra `ourui_v6_4.c` = ca. 4 s uden RX (3,98 / 4,01 s), OFFLINE efter ca. 1,3 s. `stall_s` for v6.5 er målingens gulv (0,6 s plateau-vindue); selve recovery er 0 ms for en umulig længde (selftest).
* **Stallet kan udløses uden bit-fejl i længde-byten:** et løst `AA 55` umiddelbart foran en gyldig frame (`AA 55 AA 55 83 …`) læses af v6.4 som kommando `AA` med længde `0x83` = 131 ord. Det er dækket i ourui's selftest (9 "værste" præfikser), men ikke af en sim-injektion. Forslag: en inject-type, der sender et givet præfiks (`"prefix": "aa55"`, `"aa5503"`, `"aa5508"`) lige før næste svar.
* **Sim'en starter med config'en forudindlæst** (`config_to_words` i `__init__` og `do_reboot`), altså som om stock-UI'et allerede har boot-pushet. Til "slot B-boot" sætter `sim_e2e.py` controllerens startværdier (REGISTERMAP §2) med `set_reg`, før UI'et startes. Forslag: en sim-option `--mcu-defaults` (og lad `reboot` give startværdierne), så scenarier kan udtrykke det.
* **Standard-profilen:** `artifacts/ourui/sim_e2e.py` kører nu uden `--ui-profile`, dvs. med runnerens egen standard (`sim/sim_ui_profile-ui.json`). `artifacts/ourui/sim_ui_profile.json` (v6.4) bruges ikke længere og kan udgå.

*— design-/UI-agent, 2026-11-17 (v6.5). Enheden er ikke kontaktet; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` og `sim/run_e2e.py` er ikke rørt; i `sim/` er kun dette tillæg og `reports/v6.5/` nye.*

## TILLÆG v6.6 — fra UI-agenten: fase-felter (to damp-veje, samtidige faser), ur-offset, re-push efter MCU-genstart

**Dato:** 2026-11-17 · **Skrevet af:** design-/UI-agenten · **Gælder:** `artifacts/ourui/ourui_v6.c` v6.6, `build/ourui_sim`.
Kontrakten og TILLÆG v6.4 / v6.5 er uændrede. Detaljer: `artifacts/ourui/UI_v6.md` CHANGES-v6.6.

### A. Uændret

* **`phase` er uændret:** `phase_raw()` — damp (bits 5+14) først. Under work_sta `0x5327` er `phase` derfor fortsat `STEAM`, som `replay_steam_live` asserter. `work_sta`, `regs`, toasts, writes, exit-koder og alle kroge er som i v6.5.
* Krogen `action steam_stop` sender som før STOP-blokken (`sub24 len8`, ord 24 = 0), uanset damp-vejen.

### B. Nyt

| Hvad | Indhold |
|---|---|
| state `phase_main` | det HJEM viser som hoved-aktivitet: kværn / bryg / vand går forud for damp. Under `0x5327`: `EXT` (mens `phase` = `STEAM`) |
| state `phases` | alle aktive faser, hoved-aktiviteten først: `["EXT","STEAM"]`, `["STEAM"]`, `["READY"]` |
| state `steam` | `on` (bits 5+14 sat) · `kind`: `none` / `unknown` (ord 24 ikke læst) / `auto` (ord 24 ≠ 0, mens bits'ene er sat — klæbende, til de ryddes) / `wand` (ord 24 = 0) · `last_kind` · `concurrent` · `wand_sw` (ord 44 bit0 eller ord 46 ≠ 0) · `wand_seen` · `ord24` · `target` (ord 25) · `milk` (ord 48) · `stop_btn` |
| state `shown.chip` / `shown.badge` | chippen er *STEAM·AUTO* / *STEAM·WAND* (*DAMP·AUTO* / *DAMP·WAND*) under damp; `badge` er damp-mærket under samtidige faser, ellers `null`. Under samtidige faser er 3. stat-felt `MILK` / `MÆLK` |
| state `regs` | også ord 44 og 46 |
| state `repush` | `count`, `ver` (udløst af at `0x89` kom igen), `defaults` (udløst af ord 112 = 0 og ord 113 = 0), `last`, `armed`, `ver_reqs`, `defaults_sig` |
| krogen `layout` | `steam_stop` findes kun for latchet damp (`steam.stop_btn`); under wand/flush listes de fire start-knapper (de kan ikke trykkes, mens en aktivitet kører) |
| `OURUI_SIM_UPTIME_MS=<n>` | uret starter på n ms (kun SIM-build'et). `t_ms` er fortsat ms siden UI-start. Bruges af `artifacts/ourui/sim_e2e.py --only wrap` til at køre main-loopet hen over 2^31 og 2^32 ms |
| **Ny adfærd på linjen** | ser UI'et `0x89` igen mere end 200 ms efter sin boot-push — dvs. efter sim'ens `reboot`, eller efter `link` `closed` → `up` — sender det `sub1 len30` + `sub112 len3` igen (gennem frame-politikken; i kontrakt-tilstanden udskudt under en aktivitet, med `OURUI_SIM_BOOTPUSH_GUARD=1` sprunget over). Det samme sker, hvis ord 112 og 113 begge læses som 0 tre gange i træk på en ledig maskine. Ingen af de 25 scenarier gør nogen af delene; `sim/reports/v6.6/repush.txt` viser forløbet |

### C. Fund til test-agenten

*(Den første udgave af dette afsnit havde som C.1 og C.2: "`sim/selftest.py` gav 105 / 2 under verifikationen" og "sim'en kan ikke lave en wand-flush ved siden af et bryg". Begge er besvaret af test-agentens TILLÆG v6.6-calibration nedenfor, som henviser til dem, og er her erstattet af status efter genkørslen.)*

* **De nye damp-scenarier består mod v6.6 uden ændringer:** `steam_wand_flush` 31/31 og `steam_auto_overshoot` 24/24 på begge UI-sprog (suiten: 25/25 · 644/644; `sim/selftest.py` 120/0). `artifacts/ourui/sim_e2e.py --only phases` læser desuden deres state-linjer mod de nye felter (`steam.kind`, `steam.concurrent`, `phase_main`, `shown.badge`, 3. stat-felt): 20/20.
* **Kapløbet er noteret, ikke skjult:** første fulde kørsel faldt sammen med omlægningen af damp-modellen (20/23 på begge sprog; `sim/selftest.py` 105/2 på det tidspunkt). `sim_e2e.py` tager nu et fingeraftryk af `sim/` før og efter hver del — se `sim/reports/v6.6/SUMMARY.md`.
* **`reboot` forudindlæser stadig config** (som om stock-UI'et havde boot-pushet). Til re-push-testen sætter `sim_e2e.py` startværdierne (REGISTERMAP §2) med `set_reg` lige efter `reboot`. Forslaget fra TILLÆG v6.5 C om `--mcu-defaults` står ved magt.
* **Runneren har ingen generisk state-felt-assert** (fx `{"kind":"state","path":"steam.kind","value":"wand","t0":…,"t1":…}`). Med den kunne de nye felter assertes i scenarie-filerne i stedet for i `sim_e2e.py`.

*— design-/UI-agent, 2026-11-17 (v6.6). Enheden er ikke kontaktet; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` og `sim/run_e2e.py` er ikke rørt; i `sim/` er kun dette tillæg og `reports/v6.6/` nye.*

## TILLÆG v6.6-calibration — fra test-agenten: mcu_sim kalibreret mod STEAMLIVE (to damp-veje, flush, samtidige faser)

**Dato:** 2026-11-17 · **Skrevet af:** test-agenten (ejer `sim/`) · **Gælder:** `sim/mcu_sim.py`, `sim/scenarios/`, `sim/selftest.py`, `sim/ref_client.py`.
Kontrakten og TILLÆG v6.4/v6.5/v6.6 (UI-agenten) er uændrede. Fuld dokumentation: `sim/reports/v6.6-calibration/REPORT.md`.

### A. Sim'en understøtter nu de to damp-veje + samtidige faser (svar på v6.6-tillæg C.2)

* **Display-steam (latch=1):** `cmd6 sub24 len8 ord24=1` → bits 5+14 efter ~0,30 s; **auto-stop ved ord48≥ord25**; MCU rydder selv bits og nulstiller **ord24 ~0,18 s efter bits**. Kræver **ikke** wand-kontakt (STEAMLIVE §5 — `require_wand` er nu en legacy no-op; flag/ctrl/CLI bevaret).
* **Wand/flush (latch=0):** inject/ctrl **`wand_down`/`wand_up`** (nye aliaser til `wand`-typen) → ord44 bit0 + **ord46 = kontakt 0/1** (STEAMLIVE §6.3-korrektionen: ikke flowmeter-tæller). Wand-edge starter flush **oven i** en kørende aktivitet: `work_sta` = union (bryg+wand = **0x5327**). **Ingen mælk-auto-stop** (selv >650); stop ved wand-release. **Ingen UART-writes.**
* **Damp-kanalen er uafhængig** af primær aktivitet (grind/bryg/vand) og af disses efter-faser; mælk-overshoot (+39 enh, peak ~11 s efter auto-stop) og boiler-recovery kører videre gennem andre aktiviteter.
* **UI-felterne kan nu assertes direkte mod sim'en** (ikke kun replay): `steam.kind == "wand"` under flush (ord24=0), `"auto"` under display-steam (ord24=1, klæbende til MCU-clear), `steam.concurrent` under 0x5327, `phase_main` = EXT under bryg+flush, `shown.badge` = damp-mærket. Nye scenarier: `steam_wand_flush.json` (31 checks) + `steam_auto_overshoot.json` (24 checks).
* **Additive ctrl-udvidelser:** `ping` returnerer nu også `wand` (bool) og `steam` ({latch, src}|null). Alle eksisterende felter uændret.

### B. Scenarie-reconciling (check-antal uændret undtagen de 2 nye)

* `steam_auto`/`steam_manual_stop`: **display-start FØR wand-down** (wand først ville starte en flush, og vagterne spærrer derefter steam-start). Forventningerne uændrede.
* `brew`: 3 vinduer/værdier justeret 1:1 til live-kalibreringen (stop ~29,9 s: ord51-ext til 29,5; ord39-max 365; bit2-puls fra 29,5).
* `ref_client`: state-`regs` spejler nu også 3/6/9/16/19/22/24/44/46 (additivt, som ourui).
* selftest: **120 PASS / 0 FAIL** (107 + 10 nye wand/latch/overshoot/samtidig-checks + 3 steam-live-verify-checks) — de 2 FAILs UI-agenten så under v6.6-verifikationen (C.1) var sim-omlægningen og er nu lukket.

### C. Verifikation

* **replay-verify steam-live: 92,59 % → 99,608 % (PASS)**, exact-ord 100 %, ws-sekvens identisk, timing ±1,5 s. v3/v2/session: 99,83/99,90/100,00 % (alle PASS).
* **Suiten: 25/25 scenarier · 644/644 checks** i rå EN + rå DA (V6.6-binær, md5 5658cfc3…) + REF-profil (20 overrides). `steam_auto` 30/30 deterministisk.
* `sim/run_e2e.py` er **ikke** ændret; `artifacts/ourui/*` og `boot/` er ikke rørt.

*— test-agent, 2026-11-17 (v6.6-calibration). Enheden er ikke kontaktet; alt er lokalt mod STEAMLIVE-målingerne.*

---

## TILLÆG v6.7/v6.8 — fra UI-agenten: net-vagt (wifi-model), ord 113 i boot-pushen, ingen REVERT-knap

### A. Uændret
* Kontrakt-felterne, krogene og scenarierne. Ingen fil i `sim/` er ændret ud over dette tillæg.

### B. Nyt / ændret i SIM-build'et
* **State:** nyt objekt `net{enabled, state (off|down|starting|connecting|up), iface, ifup, wpa_pid, dhcp_pid, carrier, ip, gw, heals, boots, heal_ms, init_runs, wpa_starts, dhcp_starts, conf_writes, fails}`. Standard = sund stak (som runtime-B); `OURUI_SIM_NET=cold` = slot B-koldstart, `=off` = vagten fra.
* **Ny krog `net {name}`:** `kill | ip_drop | if_down | cold | ap_down | ap_up | status` — påvirker kun SIM-build'ets MODEL af wlan0/wpa_supplicant/udhcpc (intet netværk på værten). Net-vagten skriver `<sandkasse>/appconfigs/wpa_supplicant.conf` og `<sandkasse>/tmp/ourui_net.log` ved bring-up.
* **`layout`:** `revert` er væk (v6.8: REVERT-knappen på HJEM er fjernet; ingen scenarie brugte den).
* **Boot-push:** ord 113 sendes som stock's optagede boot-push: `(preInfusionTime<<8) | 0` (GROUND-TRUTH.md). GEM-vejen (sub113) er uændret `tid<<8 | breakTime` — `settings_save_matrix`' 1541-forventninger gælder fortsat.

*— UI-agent, 2026-11-17 (v6.7/v6.8). Rapporter: `sim/reports/v6.7-v6.8/`.*

## TILLÆG v6.9 — fra UI-agenten: visnings-konditionering, stabil vægt-kilde, vægt-dødbånd

### A. Uændret
* Kontrakt-felterne (`regs` og `regs_s` er fortsat **rå**), krogene, frames og scenarierne. Ingen fil i `sim/` er ændret ud over dette tillæg.
* sim'ens demo-`calm` (median-3 + EMA + dødbånd i `mcu_sim.py`, default FRA) bruges ikke af UI'et. Konditioneringen ligger i ourui og gælder derfor også på enheden. Kør E2E med `calm` FRA, så `regs` er de rå sensor-værdier.

### B. Nyt i SIM-build'et (alle tillægs-felter; kun visning)
* **State:** `cond{raw, a_chart, a_hero, med, scale (pct|hold|max), pct, hold, head, hero_snaps, wdb, gate}`, `chart{mode, active, full, top, raw_max, disp_max, ticks, base_ticks, clipped, n, n_active}` og `weight{src (38|39), switches, shown}`. `shown` er den viste vægt i 0,1 g, signed, konditioneret og med dødbånd.
* **Ny krog `cond {…}`:** `a_chart`, `a_hero`, `med` (1|3), `scale`, `pct`, `hold`, `head`, `gate`, `raw` (0|1), `reset`. Alle felter er valgfri; uden felter læses tilstanden. `raw:1` = DIAGNOSE → GRAF = RÅ.
* **Vægt-kilden** for graf og hero er latchet. Ord 39 er standard. Ord 38 bruges kun under en kværning (work_sta bit 6) og dens FÆRDIG. Et skift kræver 300 ms; et UI-startet skift sker straks. Vedligehold-bits (0x0400/0x0800/0x2000/0x0C00/0x2800) skifter den aldrig. `shown.hero`/`shown.stats` viser den konditionerede værdi, og \|vægt\| < 0,25 g vises som `0.0`.
* **UI-side scenarie** (additivt, i `artifacts/ourui/sim_scenarios/`): `ui_calm_graph.json` (152 s). Det dækker ro med pulser, kværn, bryg, ro efter bryg, auto-damp og kop-støj/tara via `set_reg 39`. Kun eksisterende event-typer bruges (`action`, `set_reg`, `wand`).

### C. Til test-agenten
* Scenarie-asserts på `shown.hero` under en rampe skal regne med, at hero'en halter ca. 0,3 s efter den rå værdi (median-3 + EMA 0,25). Ved FÆRDIG og andre fase-skift snapper den til rå. Ingen eksisterende scenarie er ramt (E2E-resultater i `sim/reports/v6.9/`).

*— UI-agent, 2026-11-17 (v6.9). Rapporter: `sim/reports/v6.9/`.*

## TILLÆG v6.13 — fra test-agenten: manuel STOP i mcu_sim (ord16/19/22/6) + stop-scenarier

**Baggrund:** `sim/reports/v6.13/SIM_HANDOFF.md` — ourui v6.13 sender manuelle stop-frames (UI_v6 §18), men mcu_sim kørte videre til målet. Nu implementeret. Kontrakten ovenfor er **uændret**; SIM-build'et (v6.13) implementerede allerede stop-siden.

### A. Nyt i `sim/mcu_sim.py` (MCU-modellen)
* **Manuel stop = MCU'ens egen auto-stop-vej.** Kontrol-tasks'ene genberegner run-flagene hver 10 ms-omgang (som firmwaren):
  | Aktivitet | Stop-write | Sim-effekt |
  |---|---|---|
  | VAND | `cmd6 sub16 len1 [0]` (+ `sub19 len1 [0]` hvis ord19≠0) | stop så snart `ord16==0 og ord19==0` — **også i auto-tilstand** (hotwater-ctl `0x1B8C`: aktiv=(ord19\|ord16)≠0). Bits 1+3+9 ryddes, vægten standser ved **aktuel** vægt (ingen snap til mål), post/drip −11 + strøm/flow-decay præcis som vægt-stoppet. |
  | BRYG | `cmd6 sub22 len1 [0]` | stop ved `ord22` 1→0 (brew-ctl `0x1E9C`): samme `_tick_post("brew")` som vægt-stop — bits 1+2+9+12 ryddes, ventil-puls `0x0105` i ~0,21 s, efter-dryp +12, ord22=0. |
  | KVÆRN | `cmd6 sub6 len1 [0]` | stop ved `ord6==0` under grind (grind-ctl `0x19C6`: kører mens ord6≠0; ord3 er tare-triggeren). Bit6 ryddes, **vægten holder** (ingen ramp mod mål+2), cur2-decay som auto-stop. |
* Alle stop-writes ACK'es (eksakt echo, samme latency/loss-profil). Ny assertions-hændelse **`ack`** (`sub`, `len`, `words`) ved planlagt ACK-echo — bruges af den nye sim_log-assert `ack_words`.
* **Uændret:** auto-stop (vægt/mælk/timeout), fejl-stop, replay serve/verify (optagelserne har ingen stop-writes; verify-rater uændrt), idle-pulser, alle øvrige kurver. `activity_stop`-loggen har nu et additivt `manual`-felt; reason for manuel stop er `"manuel stop ord…=0"`.

### B. Nyt i `sim/ref_client.py` (oracle-paritet, additivt)
* **Ctrl-handlinger:** `action grind_stop | brew_stop | water_stop` (samme navne som ourui v6.13's `STOP_KEY`). Stop blokeres ikke af aktiv fase; frames = kun nuller, ingen intent. Vand-stop sender `sub19=[0]` efter `sub16`-ACK hvis ord19≠0 (aldrig en halv vand-stop).
* **State:** nyt felt `stop{pending, last, req, ok, fail}` — samme semantik som ourui's `stop`-objekt (minus `btns`, som reference-layoutet ikke renderer). `ok` tælles først når aktivitetens bits er ryddet (`stop_running`: grind bit6 / bryg bits1+2 / vand bits1+3) — et ACK alene tæller ikke. 5 s uden clear eller manglende ACK → `fail`.
* Toasts: `STOP: GRIND/BREW/WATER` → `GRIND/BREW/WATER STOPPED` (sprog-neutral assert: `contains "STOP"` / `"STOPP"` matcher også ourui's EN/DA-toasts).

### C. Nyt i `sim/run_e2e.py` + scenarier (additivt)
* **sim_log-assert `ack_words`** (sub+words+ vindue): beviser stop-writens ACK byte for byte.
* **Expect-kind `stop_ok`** (`min`, `max_fail`, `t0`, `t1`): læser `state.stop.ok/fail` — klient-neutral (ourui + reference).
* **Nye scenarier** (auto-opdaget i `sim/scenarios/`): `stop_water` (14 s), `stop_brew` (20 s, stop midt i ekstraktion), `stop_grind` (14 s), `stop_control_auto` (15 s, ingen stop-write → auto-stop uændret). Alle asserter: stop-frame byte for byte, ACK, bits ryddet ≤0,5 s, vægt/flow-ramp standset, post/drip/puls kørt, `stop.ok≥1`, toast STOP/STOPP, READY til sidst.
* `--quick`-subset er uændret (stop-scenarierne køres målrettet: `--scenario stop_water` el. `--all`).

*— test-agent, 2026-11-18 (v6.13-sim). Kun lokalt; enheden ikke berørt; `artifacts/ourui/*` og `boot/` ikke rørt. Rapporter: `sim/reports/v6.13-sim/`.*

---

## TILLÆG v6.14-sim — fra test-agenten: MCU-dvale/wake i mcu_sim (ord0=0 → tavs; knap/alarm → reboot + re-push)

**Dato:** 2026-11-18 · **Skrevet af:** test-agenten (ejer `sim/`) · **Gælder:** `sim/mcu_sim.py`, `sim/ref_client.py`, `sim/run_e2e.py`, `sim/selftest.py`, `sim/scenarios/`.
**Baggrund:** `sim/reports/v6.14/SIM_HANDOFF.md` — ourui v6.14 sender ved HOLD på tænd/sluk-knappen cmd8 (alarm 0/0/0) + cmd6 sub0 len2 [0,0] og forventer en MCU der går i dvale (UI_v6 §19). Nu modelleret. Kontrakten ovenfor er **uændret**; v6.14-SIM-build'et implementerede allerede UI-siden (`key`-krogen + `power` i state-JSON).

### A. Nyt i `sim/mcu_sim.py` (MCU-modellen, MEM03 sleep-task 0x2F52 / reset 0x326)

* **Dvale:** ord0=0 (cmd6 sub0 len2 `[0,0]` — eller enhver write der sætter ord0=0) → ACK'es normalt, derefter **≤0,5 s** til dvale (status-tasken tjekker ord0 hvert 500 ms, `0x29D4`): `work_sta=0`, udgange 0 (ord62/63), kørende aktiviteter (primær + damp) stoppes **sikkert** (ingen efter-fase — tasks'ene er slettet), **register-filen bevares/frosset**, og UART'en er **tavs**: cmd3/cmd6/cmd8 droppes (`dvale_rx_dropped`), ingen svar, ingen 0x89. Keep-awake-overvågningen **pauserer** i dvale (ingen `keepalive_gap` mens der er slukket).
* **Wake — kun to veje:** (1) simuleret knap-hold **≥0,8 s** (PC.8, 8×100 ms efter et slip) eller (2) **RTC-alarm** (cmd8's alarm dag/time/minut ≠ 0/0/0; RTC'en går med sim-tiden, match i dvale → wake). Begge → **CPU-reset til FABRIKKENS startværdier** (REGISTERMAP §2: ord0=1, ord11=920, ord114=50, ord4=180, ord25=550 … — config'en er **ikke** forudindlæst) + 0x89-runden hvert 20 ms → host'ens **boot-push/re-push forventes igen** (`boot_push_missing` ellers, samme vindue som ved connect).
* **Nye ctrl/inject-hændelser** (JSONL via `inject`): `{"type":"power_key","value":1|0}` (ned/slip; hold ≥0,8 s i dvale → wake), `{"type":"power_key","action":"hold","dur":1.6}` (ét samlet hold m. auto-slip), `{"type":"power_off"}` (dvale uden UART-write), `{"type":"power_on"}` (wake/reset uden hold), `{"type":"alarm","day":D,"hour":H,"minute":M}` (sæt alarmen som cmd8's felt). Knap-hold mens MCU'en er **tændt** gør intet i MCU'en (displayet ejer sluk-semantikken).
* **`ping` returnerer nu også `power`**: `{state: on|off, key_down, hold_s, dvale_pending, alarm, wakes, rx_dropped}` (additivt; alle eksisterende felter uændret).
* **Nye assertions-hændelser:** `dvale_enter`, `power_wake` (cause: power_key/alarm/ctrl), `power_key` (value/held_s), `alarm_set`; `reboot` har nu `factory`/`cause`/`defaults` (snapshot af startværdierne); `time_sync` har nu `alarm` ([dag,time,minut]). `stats` += `dvale_entries`, `power_wakes`, `dvale_rx_dropped`.
* **Kompat:** ctrl `reboot` / ord30=1 forudindlæser **stadig config** (`factory=False`, som før — TILLÆG v6.5 C's forslag er dermed delvist lukket: power-wake giver startværdierne, ctrl reboot er uændret). Replay serve/verify uændret (optagelserne har ingen ord0=0-writes; dvale indtræder aldrig). Alle eksisterende scenarier/selftests uændret grønne.

### B. Nyt i `sim/ref_client.py` (oracle-paritet, additivt)

* **Ctrl-handling `key`** (`{"cmd":"key","value":1|0[,"code":116]}`) — samme krog som ourui v6.14's SIM-build. Fuld stock-semantik (UI_v6 §19.1): HOLD **1,5 s** → cmd8 (alarm 0/0/0) → **≥80 ms** → cmd6 sub0 len2 `[0,0]` gennem write-køen → ACK → SLUKKET. **Spærret** under bryg/damp/kværn (work_sta bit12/14, ord22/24/6 + egen pending; varmt vand spærrer ikke) → toast *STOP AKTIVITETEN FØRST · SÅ SLUK*, ingen frames, nyt fuldt hold efter aktiviteten. Uden forbindelse/ord0=0 → *INGEN FORBINDELSE · SLUKKER IKKE*. Keep-awake **pauserer** mens der er slukket; polls fortsætter (MCU'en er tavs → OFFLINE). MCU-svar ord0=1 i 0,6 s → PowerOn (`sub0=[1]` + `sub1=[0]`) + HJEM + toast *MASKINEN ER TÆNDT*; 10 s uden svar → tilbage til SLUKKET. Re-push efter MCU-reset sker via den eksisterende 0x89-detektion (`boot_pushed=False` → `boot_push()` igen).
* **State:** nyt felt `power{state: on|turning_off|off|turning_on, backlight_off, presses, short, offs, ons, blocked, fails, ack8, auto_on, wake_timeouts}` — samme form som ourui's; `page="off"` mens sluk-siden vises.

### C. Nyt i `sim/run_e2e.py` + scenarier (additivt)

* **Expect-kinds:** `power` (state i vindue), `power_hold` (state over interval, min_ratio), `power_counter` (tæller ≥ min).
* **sim_log-asserts:** `dvale_seen`, `no_acks_between` (tavs UART), `time_sync_seen` (valgfri `alarm0`), `power_wake_seen` (`cause_contains`), `reboot_seen` (valgfri `factory`), `factory_defaults` (ord0=1/ord11=920/ord114=50 i reboot-snapshot); `boot_push` accepterer nu `t0` (kun push efter tidspunkt = re-push).
* **Nye scenarier** (auto-opdaget): `power_off_wake` (55 s: sluk → tavs dvale i 34 s → sim-knap-hold → reboot til fabriks-defaults → re-push genskaber config → PowerOn/HJEM/READY) + `power_during_activity` (42 s: hold under bryg spærres m. toast og ingen frames, brygen auto-stopper uforstyrret, nyt hold efter brygen slukker normalt). Begge grønne mod **v6.14-SIM-build'et** (EN+DA) og **reference-klienten** (REF-profil).
* `--quick`-subset er uændret (power-scenarierne køres målrettet: `--scenario power_off_wake` el. `--all`).

*— test-agent, 2026-11-18 (v6.14-sim). Kun lokalt; enheden ikke berørt; `artifacts/ourui/*` og `boot/` ikke rørt. Rapporter: `sim/reports/v6.14-sim/`.*
