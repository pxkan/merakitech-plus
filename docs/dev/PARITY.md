# PARITY — Komplekt feature-parity-specifikation for ourui (MerakiTech Gen 1, 480×480)

> **Note (offentlig udgave):** Dette dokument er udviklingsnoter fra reverse engineering af en MerakiTech Gen 1, som forfatteren ejer. Producentens firmware, binærer og scripts, som der henvises til (fx `demo_v9`, `MEM03_V514_260923.bin`, `demo.sh`, `test.sh`), er **ikke** inkluderet i repoet. Adresser og korte uddrag er medtaget alene for interoperabilitet. IP-adresser og andre personlige data er fjernet. Se [README → Ansvarsfraskrivelse](../../README.md).

**Dato:** 2026-11-17 · **Metode:** udelukkende STATISK reverse-engineering af `artifacts/demo_v9` (display-app, ARMv7 ELF, not stripped, debug_info, MD5 `d44c2414…`) + `artifacts/MEM03_V514_260923.bin` (powerMcu 5.1.4). **Ingen kontakt med enheden** (offline). Alle adresser er verificerbare i `demo_v9.dis` / MCU-binæren.
**Forudsætninger:** `PROTOCOL.md` (frame/CRC), `REGISTERMAP.md` (129-ords kort), `BREW.md`/`HOTWATER.md`/`SESSION.md` (live-verificerede writes), `WRITETEST.md` (config-persistens).
**Maskinlæsbar pendant:** `parity_spec.json`. **Analyse-værktøj:** `parity_analysis.py` (resolver alle `usart_send`-kald, config-accessors og mirror-skrivninger), `sdump.py` (annoteret disassembly). Mellemlister: `parity_accessors.json`, `parity_calls.json`, `parity_funcs.json`.

---

## 0. TL;DR — de vigtigste parity-kendsgerninger

1. **To parallelle sandheder:** Original-UI'et holder (a) en **config.json** (persistent, cJSON) og (b) en **register-mirror** i RAM (`machine_struct+0xFE8`, 129 ord) som synkes til MCU'en via `cmd 6`. En parameter skal ofte skrives **begge steder**: `setXxx()` → config.json **og** mirror-word → `usart_send`. ourui skal gøre det samme for fuld parity.
2. **`setNumber` skriver config.json ØJEBLIKKELIGT** (ikke kun ved exit): `setNumber`/`setString` → `cJSON_ReplaceItemInObject` → `configFileClose` → `fseek(0)/ftruncate/fwrite/fflush/fsync` ([0xd0fa4](#), [0xd0c40](#)). Hver `setXxx`-accessor kalder `setNumber`. Så enhver UI-ændring persisteres med det samme.
3. **KORREKTION til WRITETEST:** Der **ER** en parameter-push til MCU'en ved boot/genstart. `fuart_thread` sender ved opstart `cmd6 sub1 len30` (ord 1..30, [0x14685c](#)) og kort efter `cmd6 sub112 len3` (ord 112..114, [0x146b0c](#)) fra mirror (som `model_data_init` har fyldt fra config). WRITETEST's "ingen config-push ved boot" er forkert — den så kun `app_init`/`PowerOn`. **Kill+watchdog-genstart synker altså nye config-værdier til MCU'en automatisk.**
4. **Hot Water har INGEN temperatur-indstilling.** Den viste "hot water-temperatur" er **e-boilerens målte temperatur** (ord 40, sensor, read-only) — `hotWaterTemperUpdate` [0x112c9c](#) læser ord 40. Der findes intet hotwater-temp-setpoint i hverken config eller write-blokke. (Opfylder opgavens "find hotwater-TEMP": den er **fixed = boiler-temp**.)
5. **`brewMode` er vestigial:** `getExtractModeValue`/`setExtractModeValue` (config `brewMode`) har **0 kaldere** i hele binærken. Feltet findes i config men bruges ikke af UI'et.
6. **`sleepMode`/`sleepModeSetting` kan ikke redigeres i original-UI'et:** `setSleepModeState`/`setSleepModeValue` har **0 kaldere**. Felterne læses kun ved boot (`model_data_init` → model+0x54/0x55); sleep-timing er **hardcodet** (screensaver efter ~600 s idle, power-off efter ~1800 s idle — `recodeTime` [0x148164](#) sætter tæller=1800, `SleepRefresh` [0x149268](#)). `sleepModeSetting` vises kun som "%dmin" i settings-menuen.
7. **Word 112/113 er byte-pakkede pre-infusion-felter** (ikke simple ord):
   - **word112** = `(preInfusionSwitch << 8) | preInfusionPressure` (hi-byte=switch/pro_sw, lo-byte=tryk).
   - **word113** = `(preInfusionTime << 8) | breakTime` (hi-byte=pre-inf-tid, lo-byte=pause).
   - **word114** = `headTemper` (hele °C, MCU ganger med 10).
8. **Word 14 er multiplexet** mellem `steamGears` (steam-gear) og pre-infusion-tryk-rollen i opskrift-blokken. MCU'ens opskrift-funktion `0x1E10` læser **word112 lo-byte** (tryk) og håndterer kun tryk **3 eller 4**.
9. **Word 13 (pre-inf-tids-parameter) hardcodes til 60 ved boot** men **overskrives af MCU'ens opskrift-funktion `0x1E10`** under bryg-setup (hvis `preInfusionSwitch` er til). `preInfusionTime` (word113 hi) kopieres af MCU'en til word113 lo og indgår i timingen `word13 + word113_lo`.
10. **MCU'en har INGEN range-checks på cmd6** (ren `memcpy`+ACK, `REGISTERMAP §3`). **Original-UI'ets grænser er eneste værn.** ourui skal håndhæve samme min/max/step — ellers kan ekstreme værdier nå MCU'en ufiltreret.

---

## 1. Persistens- & sync-arkitektur (implementations-kritisk)

### 1.1 Config-læsning (boot)
* `configfileOpen` [0xd0ac4]: returnerer straks hvis globalt cJSON-træ (`0x1acedc`) ≠ NULL; ellers `fopen("/customer/config.json","r")` → `fread` → `cJSON_Parse`. **Filen læses kun én gang ved boot.**
* `model_data_init` [0xcef40]: læser alle config-felter til model-globals **og** mirror-ord (se tabel §1.3). Kaldes af `app_init` [0xd90bc] **før** `fuart_thread` startes.
* `getNumber(key, default, &out)` [0xd0eec]: hvis felt mangler → opret med default **og genskriv filen** (`configFileClose`). dvs. selv en læsning kan tilføje manglende felter.

### 1.2 Config-skrivning (øjeblikkelig)
* `setXxx(v)` → `setNumber(key, v)` [0xd0fa4] → `cJSON_ReplaceItemInObject` (eller `AddItemToObject` hvis ny) → `configFileClose` [0xd0c40] → `fopen("w")` (persistent fd `0x1aced8`) → `cJSON_Print` → `fseek(0)/ftruncate(0)/fwrite/fflush/fsync`.
* **Konsekvens:** enhver `setXxx` persisterer straks. Der findes ingen "vent til exit"-semantik.

### 1.3 model_data_init — config → model/mirror (boot) [0xcef40]
| config-felt | model-offset | mirror-word | Note |
|---|---|---|---|
| Temperature | +0x51 | — | °C/°F-flag (display) |
| timemode | +0x59 | — | 12/24h (display) |
| Automatically | +0x58 | — | auto-tidssync (display) |
| timeZone | +0x56 | — | UTC-offset-index (display) |
| language | +0x50 | — | sprog-index (display) |
| backlight | +0x57 (=v×10) | — | PWM duty (display) |
| volume | +0x53 | — | lyd (display) |
| sleepMode | +0x54 | — | **kun læst, ingen UI-editor** |
| sleepModeSetting | +0x55 | — | **kun læst, vises som "%dmin"** |
| clearTip | +0x5a | — | tip-flag |
| brewTemper | — | **word11** | 0,1 °C |
| autoGrindWeight | — | **word4** | 0,1 g |
| grindMode | — | **word2** | 1/2 |
| steamGears | — | **word14** | 1..4 |
| preInfusionPressure | +0x5c | (word112 lo) | pakkes senere |
| preInfusionTime | +0x60 | (word113 hi) | pakkes senere |
| breakTime | +0x64 | (word113 lo) | pakkes senere |
| headTemper | +0x68 | (word114) | pakkes senere |
| preInfusionSwitch | +0x5b | (word112 hi) | pakkes senere |
| sumtime | +0x6c | — | display-flag |
| autobrewWeight | — | **word23** | 0,1 g |
| AtuoSteamTarge | — | **word25** | 0,1 °C |
| autoHotWaterTarget | — | **word17** | 0,1 g |
| hotWaterShortcut | +0x6d | — | display-flag |
| presetStore | +0x1174 | — | JSON-objekt |

Efter felt-læsning pakker `model_data_init` [0xcf284..0xcf43c]:
* **word112** = `(preInfusionSwitch?0x100:0) | (preInfusionPressure & 0xFF)`
* **word113** = `(preInfusionTime << 8) | (breakTime & 0xFF)`
* **word114** = `headTemper`

### 1.4 Boot-push til MCU (KORREKTION til WRITETEST)
`fuart_thread` [0x14671c], efter `open_port(1)`+`set_opt(57600,8N1)`:
1. Sætter mirror **word5=60, word13=60, word15=60** [0x146810..0x14684c] (hardcoded opskrifts-parametre).
2. **`usart_send(6, 1, 30)`** [0x14685c] → skriver **ord 1..30** fra mirror (alle setup-ord inkl. vægte/temperaturer/gear).
3. I poll-loopet, første gang flag `0x1a63d2`≠0: **`usart_send(6, 112, 3)`** [0x146b0c] → skriver **ord 112..114** (pre-infusion + head-temp), nulstiller flag.
4. Derefter normal poll-rotation (cmd3 sub0/56 len56, sub33 len7 ×2).

**Parity-konsekvens:** Når ourui ændrer config.json og genstarter demo_v9 (kill+watchdog), vil den nye værdi blive **pushet til MCU'en ved boot** via ovenstående. Men hvis ourui *selv* holder ttyS1 og skriver direkte, skal den sende de samme blokke. Reihenfølge: **config først, så MCU-write** (eller lad boot-push klare det ved genstart).

### 1.5 Spejl- vs. MCU-ejede ord
* **Skrivbare (display→MCU):** ord **0..31** (`sub+len<33`) og ord **112..114** (`sub+len∈{113,114,115}`). Alt andet afvises af MCU'en ("06 error").
* **MCU-ejet status (ord 32..111):** display må **ikke** skrive disse (sensorer/flag/work_sta). ourui skal kun læse dem.
* **Trigger-ord** (konsumeres+nulstilles af MCU): ord 3/9 (start/tare), 6 (grind-tare), 26/27 (kalibrering), 30 (reboot). **Skal være 0 i normale write-blokke** — ellers udløses utilsigtet handling.

---

## 2. PARAMETER-INVENTAR

Konfidens: **H**=høj (entydig kode+live), **M**=middel (kode, ej live-verificeret her), **L**=lav/udledt.
"write" angiver den **primære enkelt-parameter-write** (sub, len, word-offset i payload). Blok-writes (opskrift/start) er i §3.
`persist`: `config+mcu` = både config.json og MCU-write; `config` = kun config.json (display-lokal); `mcu` = kun MCU-write (ingen config).

### 2.1 TIER 1 — Kerne-brygparametre

| # | Parameter | UI-label (EN) | Side / handler | config-felt (nuværende) | Enhed/skala | word | write (sub,len,off) | min–max (step) | default | persist | Konf. |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **Grind-vægt** (dose) | "grinder weight" / "Dose" | page_grinder_weight, onClick [0xf4938] | `autoGrindWeight`=185 | 0,1 g (185=18,5 g) | **4** | (4,1,0) | **1,0–30,9 g (0,1)** | 180 | config+mcu | H |
| 2 | **Bryg-vægt** (yield) | "brew temper"-side, yield-roller | page_brew_temper, onClick [0x12bc5c] | `autobrewWeight`=360 | 0,1 g (360=36,0 g) | **23** | (23,1,0) | **10–99 g (1)** | 360 | config+mcu | H |
| 3 | **Hot water-vægt** | "water weight" | page_water_weight, onClick [0xfbeb4] | `autoHotWaterTarget`=400 | 0,1 g (400=40,0 g) | **17** | (17,1,0) | **30–500 g (10)** | 3600 | config+mcu | H |
| 4 | **Bryg-temperatur** | "brew temper" / "Brew Temperature" | page_brew_temper, onClick [0x12bc5c] | `brewTemper`=870 | 0,1 °C (870=87,0 °C) | **11** | (11,1,0) | **85–96 °C (1)** [=185–205 °F] | 870 | config+mcu | H |
| 5 | **Gruppehoved-temp** | "Temperature of Group Head" | page_time_zone (mode 4), onClick [0xeb1ac] | `headTemper`=70 | **hele °C** (70=70 °C); MCU ×10 | **114** | (114,1,0) | **30–80 °C (10)** ¹ | 50 | config+mcu | H |
| 6 | **Steam/mælk-target** | "Steam level"/mælk-temp | page_brew_temper (mode 2), onClick [0x12bc5c] | `AtuoSteamTarge`=650 | 0,1 °C (650=65,0 °C) | **25** | (25,1,0) | **45–70 °C (1)** [=110–160 °F] | 550 | config+mcu | H |
| 7 | **Steam-gear** | "Steam level": Gentle/Mild/Medium/Strong | page_steam_select, onClick [0xdcfe0] | `steamGears`=3 | gear 1..4 | **14** | (14,1,0) | **1–4 (1)** | 4 | config+mcu | H |
| 8 | **Pre-infusion tryk** | "Pre-infusion" (pressure) | page_time_zone (mode 1) / switch_ctr / tip_dec | `preInfusionPressure`=4 | bar (heltal) | **112 lo-byte** | (112,2,0)² | **3–4 bar (1)** ³ | 4 | config+mcu | M |
| 9 | **Pre-infusion tid** | "Pre-infusion" (time) | page_time_zone (mode 2), onClick [0xeb1ac] | `preInfusionTime`=6 | sekunder (heltal) | **113 hi-byte** | (112,2,0)² | **5–? s (1)** ¹ | 15 | config+mcu | M |
| 10 | **Pre-infusion switch** | "Pre-infusion" (on/off) | page_switch_ctr [0x103858] / tip_dec [0x133074] | `preInfusionSwitch`=1 | bool 0/1 | **112 hi-byte (bit8)** | (112,2,0)² | **0/1** | 0 | config+mcu | H |
| 11 | **breakTime** (pause) | "Pre-infusion" (break/pause) | page_time_zone (mode 3), onClick [0xeb1ac] | `breakTime`=5 | sekunder (heltal) | **113 lo-byte** | (112,2,0)² | **0–? s (1)** ¹ | 5 | config+mcu | M |
| 12 | **Grind-mode** | grinder mode (vægt/tid) | home/grinder, move_deale [0x122180], grinderScaleUpdate [0x119c68] | `grindMode`=1 | 1=vægt-auto, 2=tid | **2** | (2,2,0) | **1–2 (1)** | 1 | config+mcu | H |
| 13 | **Hot water-mode** | Hot Water auto/manuelt | hotWaterSetMode [0x11344c], page_water | `hotWaterMode`=1 | 1=auto, 2=manuel | **16/19** (i blok) | (9,11,0) | **1–2** | 1 | config+mcu | H |
| 14 | **Hot water-TEMP** | (visning) | hotWaterTemperUpdate [0x112c9c] | **INGEN config** | — | **40 (sensor, read-only)** | **ingen write** | **fixed = e-boiler-temp** | — | — (read-only) | H |

¹ **Picker-max ikke endeligt kortlagt:** værdi-formlen er bevist (se §2.1.1), men antallet af roller-options i picker'en er ikke direkte synligt (picker-navigation er anomal, se §6.6). min+step er sikre; max er angivet som MCU/config-konsistent skøn. Verificér når enheden er online.
² Pre-infusion-felterne pakkes i **word112/113** og sendes samlet som `sub112 len2` (ord 112+113) eller `len3` (+114). Enkelt-felt-writes findes ikke; man skriver hele word112/113 med de øvrige byte bevaret.
³ **MCU'ens opskrift-funktion `0x1E10` håndterer kun tryk 3 eller 4** (sætter forskellige word13/14/17 resp. word15/16/18 afhængigt af netspænding 120/220 V). Andre tryk-værdier ignoreres af MCU'en. UI-formlen er `idx+3` (min 3).

#### 2.1.1 Værdi-formler fra roller-index (bevist i onClick-kode)
* **Grind-vægt** [0xf4984]: `word4 = (roller1_sel+1)*10 + roller2_sel`. roller1 = 01..30 (`lvgl_roller_create(1,30)` [0xf4c8c]), roller2 = 0..9. → 10..309 (1,0–30,9 g).
* **Bryg-temp** [0x12bcfc]: `atoi(roller_str) × 10`; hvis °F-visning: `GetCelsius(F)` først. roller 85..96 °C / 185..205 °F ([0x12c370/0x12c478]).
* **Bryg-vægt** [0x12be5c]: `atoi × 10`. roller 10..99 ([0x12c754]).
* **Steam-target** [0x12bf80]: `atoi × 10` (eller GetCelsius). roller 45..70 °C / 110..160 °F ([0x12c9bc/0x12cab4]).
* **Hot water-vægt** [0xfbf0c]: `roller_sel × 10`. `roller_step_create(30,500,10)` [0xfc194] → 30,40,…,500 g.
* **Steam-gear** [0xdd028]: `sel 0..3 → word14 = 1..4` (cmp #3, 4 cases). Options "Gentle\nMild\nMedium\nStrong".
* **Head-temp** [0xeb8a0]: `word114 = idx*10 + 30` → 30,40,50,… °C.
* **Pre-inf-tryk** [0xeb374]: `model+0x5c = idx + 3` → word112 lo-byte.
* **Pre-inf-tid** [0xeb5b4]: `model+0x60 = idx + 5` → word113 hi-byte.
* **breakTime** [0xeb740]: `model+0x64 = idx` → word113 lo-byte.

### 2.2 TIER 2 — Komfort / display-lokale (ingen MCU-write)

| # | Parameter | UI-label | Side / handler | config-felt (nuv.) | Enhed | word | write | min–max (step) | default | persist | Konf. |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 15 | **Backlight** | "Brightness" | page_brightness, onClick [0xe4bcc] + slider_event_cb [0xe4d28] | `backlight`=9 | 0..10 (slider 0..100 ÷10) | — | ingen MCU | **0–10 (1)** | 8 | config | H |
| 16 | **Volume** | "Sound Volume" | page_brightness slider [0xe4ce4] + page_setting_com [0xe1b98] | `volume`=31 | 0..100 (slider) / diskret High/Medium/Low/Mute | — | ingen MCU | **0–100** ⁴ | 40 | config | M |
| 17 | **Temperatur-enhed** | "Temperature Unit" (°C/°F) | page_setting_com, onClick [0xe1938→0xe1b00] | `Temperature`=1 | 0/1 (°F/°C) | — | ingen MCU | **0/1** | 0 | config | H |
| 18 | **Tema** | "Customization" (Light/Dark) | page_setting_com [0xe1c08] | `theme` (ej i config.json) | 0/1 | — | ingen MCU | **0/1** | 1 | config | H |
| 19 | **Sprog** | "Language" | page_setting_com [0xe1c78] | `language`=0 | index 0..10 | — | ingen MCU | **0–10** | 0 | config | H |
| 20 | **hotWaterShortcut** | (genvej til hot water) | page_switch_ctr [0x103cd0] | `hotWaterShortcut`=1 | bool | — (model+0x6d) | ingen MCU | **0/1** | 0 | config | H |
| 21 | **sumtime** | (sommertid/DST — ikke timer-visning; accessor `getSumerTimeSwitchValue`, jf. v6.1-fund) | page_switch_ctr [0x103c70] | `sumtime`=0 | bool | — (model+0x6c) | ingen MCU | **0/1** | 0 | config | H |
| 22 | **clearTip** | (clear-tip) | page_clear, onClick [0xfe060→0xfe168] | `clearTip`=0 | bool | — | ingen MCU | **0/1** | 0 | config | H |
| 23 | **Sleep-mode** | (sleep on/off) | **ingen UI-editor** | `sleepMode`=1 | bool | — | ingen MCU | 0/1 | 1 | config (kun læst) | H |
| 24 | **Sleep-tid** | "%dmin" (visning) | **ingen UI-editor** | `sleepModeSetting`=0 | minutter | — | ingen MCU | — | 0 | config (kun læst) | H |

⁴ **Volume har to kontroller:** (a) page_brightness **slider 0..100** (værdi gemmes direkte, `setVolume` [0xe4ce4]); (b) page_setting_com **diskret vælger** "High/Medium/Low/Mute" (4 options, `setVolume` [0xe1b98], værdi=option-index). Config `volume=31` stammer fra slideren. Den præcise mapping for den diskrete vælger (om den mapper til faste værdier) er ikke endeligt kortlagt — slideren er den finkornede, autoritative kontrol. **Ingen MCU-write** (lyd er display-lokal via `mmplayer_setVolume`/`set_volumn_type`).

**Sprog-liste (11 sprog, index 0..10):** English, Deutsch, French, Italian, Español, Polish, العربية, 简体中文, 繁体中文, 한국어, 日本語 (strenge @ 0x166d84..0x166df4). `language=0` = English.

### 2.3 TIER 3 — System / tid / wifi (dokumentér, men udelad fra ourui v1)

| # | Parameter | UI-label | Side / handler | config-felt (nuv.) | Enhed | word | write | persist | Konf. |
|---|---|---|---|---|---|---|---|---|---|
| 25 | **timeZone** | "Date & Time"→zone | page_time_zone (mode 0) [0xeb238], time_sync_thread | `timeZone`=48 | UTC-offset-index | — | cmd8 (tid) | config | H |
| 26 | **timemode** | 12/24h | page_dateAndtime [0xfcb58→0xfcdec] | `timemode`=1 | 0/1 | — | ingen MCU | config | H |
| 27 | **Automatically** | auto-tidssync | page_dateAndtime [0xfcb58→0xfccd0] | `Automatically`=1 | bool | — | ingen MCU | config | H |
| 28 | **Dato/tid** | "Date & Time" | page_time_select [0xdded4], page_date_select [0xe6074] | `datetime` (streng) | — | — | cmd8 + `biz_set_system_time` | config+system | H |
| 29 | **brewMode** | (ingen) | **0 kaldere — vestigial** | `brewMode`=0 | — | — | ingen | config (ubrugt) | H |
| 30 | **wifionoff** | Wi-Fi on/off | app_wifi_setOn [0x13a538] | `wifionoff`=1 | bool | — | ingen MCU | config | H |
| 31 | **ssid / password** | Wi-Fi | page_wifi [0xef0ac] | `ssid`="<ssid-redacted>", `password`="<password-redacted>" | strenge | — | ingen MCU | config | H |
| 32 | **presetStore / NFC** | "Preset"/NFC-profil | page_preset* + app_nfc_thread | `presetStore`={}, `presetAppliedIndex` | JSON | **4..25** | (4,21/22,0) | config+mcu | H |
| 33 | **init / patch / version** | (setup/OTA) | page_first_config, lage_patch | `init`=1, `patch`=1, `*version` | — | — | ingen | config | H |

**⚠️ WiFi-politik (opgave-krav):** ourui skal **IKKE røre** `wifionoff`/`ssid`/`password` eller wifi-funktionerne. Kun dokumenteret her. Tilsvarende bør OTA (`page_software_update`, `page_mcu_soft_update`) og fabrikstest undgås i v1.

#### 2.3.1 Service-kontroller (write-only, ingen config — udelad fra normal UI)
Disse findes i original-UI'et som service-/fabrikssider og er medtaget i write-atlas (§3.8) og `parity_spec.json` (tier 3, `persist:"mcu"`). **De er farlige eller service-only og bør ikke expose'es i ourui v1:**
| Kontrol | Side | word(s) | write | Fare |
|---|---|---|---|---|
| Back-flush clean (~30 s) | page_clear / clear_run | 20 | (20,1) | rense-cyklus |
| Drain Mode / Drain Valve | page_drain_value | 28, 29 | (28,1)+(29,1) | udtømning |
| Grind-skala kalibrering | page_test_scale | 26 | (26,1) | skriver MCU-flash (grind_k) |
| Extract-skala kalibrering | page_test_scale | 27 | (27,1) | skriver MCU-flash (extract_k) |
| MCU-reboot (factory reset) | page_reset | 30 | (30,1) | **resetter MCU'en** |
| Self-test / factory test | page_self_test / factoryTest | 8 | (8,1) | test-mode |
| Service-blok | page_service_setting | 1..29 | (1,29) | word1=1, word28/29=2 |

---

## 3. WRITE-BLOK-ATLAS (word-for-word)

Frame-format: `AA 55 06 SUB LEN <LEN ord, 16-bit LE> CRC16lo CRChi` (CRC-16/MODBUS over bytes[0..4+2·LEN]). `usart_send(cmd,sub,len)` [0x146d5c] kopierer `len` ord fra mirror `machine+0xFE8+2·sub`. **Payload = mirror-ordene på skrivetidspunktet** — ourui skal sikre at mirror indeholder de ønskede værdier (inkl. 0 i trigger-ord) før send.

### 3.1 sub3 len4 — GRIND START (ord 3..6) · live-verificeret [BREW W1]
Kald: home onClick [0x109278]. Forinden: word3=1 [0x109250], word38=0 (clear grind-vægt) [0x109268]. Efterfølges af `sub6 len1` word6=1 (tare) [0x10928c].
| Word | Felt | Værdi (observeret) | Rolle |
|---|---|---|---|
| 3 | grind_start_trigger | **1** | MCU konsumerer+nulstiller |
| 4 | grind_auto_weight | 185 | mål (0,1 g) |
| 5 | grind_over_time | 60 | timeout (hardcoded 60 ved boot) |
| 6 | grind_tare_trigger | **1** | tare grind-vægt |
Observeret frame: `ord3=1, ord4=185, ord5=60, ord6=1`. MCU stopper autonomt ved vægt-mål (bit6 work_sta).

### 3.2 sub5 len18 — BRYG START (ord 5..22) · live-verificeret [BREW W3]
Kald: home onClick [0x109700]. Forinden: word9=1 [0x109584], word39=0 (clear bryg-vægt) [0x10959c], word22 toggles til 1 [0x10965c].
| Word | Felt | Værdi | Rolle |
|---|---|---|---|
| 5 | grind_over_time | 60 | medsendt |
| 6 | grind_tare | 0 | — |
| 7 | grind_run | 0 | — |
| 8 | self_mode | 0 | — |
| 9 | **brew_start_trigger** | **1** | MCU konsumerer+nulstiller |
| 10 | (reserveret) | 0 | — |
| 11 | **brew_set_temp** | **870** | = config brewTemper |
| 12 | (reserveret) | 0 | — |
| 13 | brew_opskrift_param (pre-inf) | 60 | se §6.3 (overskrives af MCU 0x1E10) |
| 14 | steam_gear / pre-inf-tryk | 3 | = config steamGears (multiplexet) |
| 15 | steam_opskrift_param | 60 | medsendt |
| 16 | hotwater_auto | **0** | IKKE varmt vand |
| 17 | hotwater_weight | 400 | medsendt (bruges ej til bryg) |
| 18 | steam_switch | 0 | — |
| 19 | hotwater_sw | 0 | — |
| 20 | water_target | 0 | bryg er vægt-styret |
| 21 | (reserveret) | 0 | — |
| 22 | **brew_aktiv-flag** | **1** | MCU rydder selv ved stop |
Observeret payload (ord 5..22): `60,0,0,0,1,0,870,0,60,3,60,0,400,0,0,0,0,1`. MCU stopper autonomt ved vægt-mål (prædiktivt, efter-dryp).

### 3.3 sub9 len11 — HOT WATER START (ord 9..19) · live-verificeret [HOTWATER W1]
Kald: hotWaterStartAutoSwitch [0x113d88] (word16=1,word19=0) / hotWaterStartManualSwitch [0x113e0c] (word16=0,word19=1). Forinden: word9=1, word39=0.
| Word | Felt | Værdi (auto) | Rolle |
|---|---|---|---|
| 9 | **start_trigger** | **1** | MCU konsumerer+nulstiller (tare) |
| 10 | (reserveret) | 0 | — |
| 11 | brew_set_temp | 870 | medsendt |
| 12 | (reserveret) | 0 | — |
| 13 | brew_opskrift_param | 60 | medsendt |
| 14 | steam_gear | 3 | medsendt |
| 15 | steam_opskrift_param | 60 | medsendt |
| 16 | **hotwater_auto** | **1** | auto-flag (MCU rydder ved stop) |
| 17 | **hotwater_weight** | **400** | doserings-mål (0,1 g) |
| 18 | steam_switch | 0 | — |
| 19 | hotwater_sw | **0** | manuel-switch (1 ved manuel mode) |
Observeret frame: `aa 55 06 09 0b 01 00 00 00 66 03 00 00 3c 00 03 00 3c 00 01 00 90 01 00 00 00 00 08 3e`. MCU stopper autonomt ved ord39≥ord17. **Ingen stop-write** (auto). Manuel stop: `sub16 len1` word16=0 [0x11468c] + `sub19 len1` word19=0 [0x114720].

### 3.4 sub24 len8 — STEAM START/STOP (ord 24..31) · statisk+SESSION
Kald: home onClick [0x109548/0x109900]. word24 toggles 0↔1 [0x1094fc/0x1098b4].
| Word | Felt | Værdi (start) | Rolle |
|---|---|---|---|
| 24 | **steam_switch** | **1** (0=stop) | start/aktiv-flag |
| 25 | **milk_set_temp** | **650** | = config AtuoSteamTarge |
| 26 | grind_k_calib | **0** | ⚠️ trigger — SKAL være 0 |
| 27 | extract_k_calib | **0** | ⚠️ trigger — SKAL være 0 |
| 28 | drain_param | 0 | — |
| 29 | drain_param | 0 | — |
| 30 | **reboot** | **0** | ⚠️ SKAL være 0 (ellers MCU-reset!) |
| 31 | (reserveret) | 0 | — |
Observeret start-frame [SESSION §10.2]: `aa 55 06 18 08 01 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 cd 67`. **Auto-stop:** MCU stopper selv ved ord48(mælk)≥ord25, rydder ord24 + work_sta bits 5+14 (0x4020). Manuel stop = samme blok med ord24=0. **Fysisk wand-kontakt (PE.2) kræves** for start.

### 3.5 sub18 len14 — STEAM MIRROR-SYNC (ord 18..31) · SESSION
Kald: home onClick [0x109848]. word18=0 [0x109818]. **Ikke en trigger** — synker opskrifts-ordene inkl. word25.
| Word | Felt | Værdi |
|---|---|---|
| 18 | steam_switch | 0 |
| 19 | hotwater_sw | 0 |
| 20 | water_target | 0 |
| 21 | (reserveret) | 0 |
| 22 | brew_aktiv | 0 |
| 23 | brew_auto_weight | 360 |
| 24 | steam_switch | 0 |
| 25 | milk_set_temp | 650 |
| 26..31 | calib/drain/reboot/resv | **0** |
Eksempel [SESSION §10.2]: `aa 55 06 12 0e 00 00 … 68 01 00 00 8a 02 … fc 29`. Kan sendes før steam-start for at sikre setpoint i mirror.

### 3.6 sub4 len21 / len22 — OPSKRIFT-BLOK (NFC / preset)
* **NFC:** `sub4 len21` = **ord 4..24** (app_nfc_thread [0x1437e8]). NFC sætter word4/11/23 fra kortet; øvrige ord fra mirror.
* **Preset-apply:** `sub4 len22` = **ord 4..25** (pagePresetStoreApply [0x127e18]). Sætter word4/11/23/14/25 fra preset.
| Word | Felt | Kilde |
|---|---|---|
| 4 | grind_auto_weight | preset/NFC |
| 5 | grind_over_time | mirror (60) |
| 6..8 | grind tare/run/self | 0 |
| 9 | brew_start_trigger | 0 (ikke start) |
| 10 | reserveret | 0 |
| 11 | brew_set_temp | preset/NFC |
| 12 | reserveret | 0 |
| 13 | pre-inf param | mirror (60) |
| 14 | steam_gear | preset |
| 15 | steam param | mirror (60) |
| 16 | hotwater_auto | 0 |
| 17 | hotwater_weight | mirror |
| 18 | steam_switch | 0 |
| 19 | hotwater_sw | 0 |
| 20 | water_target | 0 |
| 21 | reserveret | 0 |
| 22 | brew_aktiv | 0 |
| 23 | brew_auto_weight | preset/NFC |
| 24 | (steam-relateret) | 0 |
| 25 | milk_set_temp | **kun preset (len22)** |
**Forskel len21 vs len22:** NFC inkluderer ikke word25 (mælk-temp); preset gør. Begge starter i word4.

### 3.7 sub112 len2 / len3 — PRE-INFUSION + HEAD-TEMP (ord 112..114)
Kald: page_time_zone [0xeb570 len2], switch_ctr [0x103bc8 len2], tip_dec [0x133334 len2], boot [0x146b0c len3].
| Word | Byte-pakning | Indhold |
|---|---|---|
| 112 | hi-byte = **preInfusionSwitch** (bit8/pro_sw), lo-byte = **preInfusionPressure** | `(switch<<8)\|pressure` |
| 113 | hi-byte = **preInfusionTime**, lo-byte = **breakTime** | `(time<<8)\|break` |
| 114 | hele word = **headTemper** (°C) | MCU ×10 → 0,1 °C |
* Enkelt-felt-ændring: læs mirror-word, erstat kun den relevante byte, send `sub112 len2` (eller `sub113 len1` for kun word113, `sub114 len1` for kun head-temp).
* **sub113 len1** [0xeb700/0xeb880]: kun word113 (pre-inf-tid/break).
* **sub114 len1** [0xeb9f4]: kun word114 (head-temp).

### 3.8 Øvrige enkelt-ord-writes (fuldt atlas)
| sub,len | Word(s) | Felt / handling | Kald (adresse) | Værdi |
|---|---|---|---|---|
| 0,1 | 0 | **power** on/off | PowerOn [0x146ef8]/PowerOff [0x146f34] | 1/0 |
| 1,1 | 1 | **sleep/drain** (1=sleep,0=awake) | MachSleep/Awaken [0x146f70/0x146fac] (død kode), ensure_service_mode [0xdfebc] | 0/1 |
| 1,29 | 1..29 | **service-blok** (word1=1,word28=2,word29=2) | page_service_setting [0xf7054] | blandet |
| 1,30 | 1..30 | **boot-push** (alle setup-ord) | fuart_thread [0x14685c] | mirror |
| 2,2 | 2..3 | **grind-mode** (word2=1/2) | grinderScaleUpdate [0x119d14], move_deale [0x122278], home [0x108eb8] | 1/2 |
| 3,1 | 3 | grind-start-trigger | home [0x10a888], test_scale [0xe0cdc] | 1 |
| 4,1 | 4 | **grind-vægt** | page_grinder_weight [0xf49fc], goto_grinder_page [0x11680c] | 0,1 g |
| 4,3 | 4..6 | grind-opskrift (word6=0) | home [0x109150] | — |
| 6,1 / 6,2 | 6(/7) | **grind-tare** | home [0x10928c/0x1093ac] | 1 |
| 8,1 | 8 | **self-test mode** | page_self_test [0xf5584/0xf55ec] | 1/0 |
| 9,1 | 9 | brew/hotwater start-trigger | home [0x10a740], hotWaterSetMode [0x1134ac], scale [0xe3a00] | 1 |
| 11,1 | 11 | **bryg-temp** | page_brew_temper [0x12bda8], home onEnter [0x10aec0] | 0,1 °C |
| 14,1 | 14 | **steam-gear** | page_steam_select [0xdd0d4], clear onHandle [0xfff94] | 1..4 |
| 16,1 | 16 | **hotwater_auto** (stop=0) | hotWaterStartStop [0x11468c], home onHandle [0x10d16c] | 0/1 |
| 17,1 | 17 | **hotwater-vægt** | page_water_weight [0xfbf88] | 0,1 g |
| 18,1 | 18 | steam-switch (puls/manuel) | home [0x109970] | 0/1 |
| 19,1 | 19 | **hotwater manuel sw** | set_manual_hot_water_switch [0x1051b0], hotWaterStartStop [0x114720] | 0/1 |
| 20,1 | 20 | **water_target / back-flush clean** | page_clear [0xfe218/0xfff28/0x100074] | 0/1 |
| 21,1 | 21 | grindability / fejl-clear | page_error [0xf80d8], hidGrindeability [0x1187f0], home [0x10a810] | 1 |
| 23,1 | 23 | **bryg-vægt** | page_brew_temper [0x12bee8], goto_brew_page [0x11a564] | 0,1 g |
| 25,1 | 25 | **steam/mælk-target** | page_brew_temper [0x12c07c], goto_steam_page [0x11e2f0] | 0,1 °C |
| 26,1 | 26 | **grind_k kalibrering** | page_test_scale [0xe0b94…] | 100/200/500 (vægt) eller 1..6 (gear) |
| 27,1 | 27 | **extract_k kalibrering** | page_test_scale [0xe0bc0…] | 100/200/500 |
| 28,1 | 28 | **drain-param** | page_drain_value [0x1265ac] | 1 (2 i service) |
| 29,1 | 29 | **drain-param** | page_drain_value [0x12665c] | 1 (2 i service) |
| 30,1 | 30 | **MCU-reboot** | page_reset [0xe2f5c] | 1 ⚠️ |
| 31,1 | 31 | (reserveret/uartPageGoto) | uartPageGoto [0x12348c] | 0 |
| 113,1 | 113 | pre-inf-tid/break | page_time_zone [0xeb700/0xeb880] | pakket |
| 114,1 | 114 | **head-temp** | page_time_zone [0xeb9f4] | °C |

**Ukendte/reserverede ord (eksplicit):** word 10, 12, 21, 31, 32, 76..79, 115..127 har ingen individuel MCU-adgang (REGISTERMAP §2). I write-blokke sendes de som 0 (eller mirror-værdi). Word 24 i opskrift-blok sub4 er "steam-relateret" men uden entydig semantik — send 0.

---

## 4. MCU-side fortolkning af pre-infusion (multiplexing-detaljer)

MCU'ens bryg-kontrol `brew-ctl` [0x1e9c] kalder opskrift-funktionen **`0x1E10`** under setup:
```
0x1E14: læs word112 hi-byte (preInfusionSwitch)
0x1E18: hvis 0 → word113 lo=0 (ingen pre-infusion), return
0x1E1A: læs word113 hi-byte (preInfusionTime)
0x1E1E: kopiér til word113 lo-byte   ← preInfusionTime bliver "breakTime"-slot
0x1E36: læs word112 lo-byte (preInfusionPressure)
0x1E34: sammenlign netspænding (0x78=120V / 0xdc=220V)
  120V: tryk==3 → word17=840, word13=340, word14=360
        tryk==4 → word17=840, word13=440, word14=460
  220V: tryk==3 → word18=870, word15=300, word16=320
        tryk==4 → word18=870, word15=350, word16=370
  (andre tryk-værdier → ingen ændring)
```
Derefter bruger `brew-ctl` [0x1fd4] **`word13 + word113_lo`** som pre-infusions-varighed (sammenlignet mod en timer).

**Parity-konsekvenser:**
* `preInfusionSwitch` (word112 hi) **gater** hele pre-infusionen.
* `preInfusionTime` (word113 hi) kopieres til word113 lo og indgår i timingen.
* `preInfusionPressure` (word112 lo) vælger en **fast profil** (kun 3/4 håndteres).
* `breakTime` (word113 lo) **overskrives** af MCU'en med preInfusionTime — dvs. den separate breakTime-værdi har **ingen effekt** i standard-bryg (den erstattes). Dette er en firmware-quirk; dokumentér men forvent ikke at breakTime ændrer bryggen.
* `word13` hardcodes til 60 ved boot, men overskrives af 0x1E10 (340/440) når preInfusionSwitch er til.

---

## 5. PARITY-SCOPE — anbefaling til ourui v6

### TIER 1 (skal expose — kerne-bryg, fuld parity)
Grind-vægt, bryg-vægt, hotwater-vægt, bryg-temp, group-head-temp, steam/mælk-target, steam-gear, pre-infusion (tryk/tid/switch), breakTime, grind-mode, hotwater-mode.
**Begrundelse:** Dette er de parametre brugeren reelt justerer for at brygge. Alle har config+MCU-write og er live/statisk verificeret. Pre-infusion-felterne skal byte-pakkes korrekt i word112/113 (§3.7) — ellers forkert adfærd.

### TIER 2 (bør expose — komfort, display-lokal, lav risiko)
Backlight, volume, temperatur-enhed (°C/°F), tema, sprog, hotWaterShortcut, sumtime, clearTip.
**Begrundelse:** Rene config-skrivninger (ingen MCU-write), lav risiko. °C/°F skal også påvirke hvordan tier-1-temperaturer **vises** (men word11/25 er altid 0,1 °C). Sprog kræver de 11 sprog-strenge (eller lad ourui være dansk-only og dokumentér afvigelsen).

### TIER 3 (dokumentér, men UDELAD fra v1)
* **timeZone/timemode/Automatically/dato-tid:** kræver cmd8 tidssync + DST-logik (`time_sync_thread`, `calc_*_dst`) — kompleks, ikke kerne. Kan tilføjes senere.
* **sleepMode/sleepModeSetting:** original-UI'et har **ingen fungerende editor** (hardcodet timing). ourui kan enten (a) udelade, eller (b) implementere en *bedre* editor der faktisk skriver word1 — men det er en **udvidelse**, ikke parity. Anbefal: udelad i v1, dokumentér.
* **brewMode:** vestigial (0 kaldere). Udelad.
* **wifi/ssid/password:** **MÅ IKKE røres** (opgave-krav). Udelad.
* **OTA/fabrikstest/kalibrering (word26/27)/reboot (word30)/drain (word28/29)/self-test (word8):** farlige eller service-only. Udelad fra normal UI; kalibrering/reboot kan være skjult service-menu. **Særligt: word30=1 rebootter MCU'en, word26/27≠0 udløser kalibrering — disse skal ALTID være 0 i normale write-blokke.**
* **presetStore/NFC:** nice-to-have. Preset-apply (sub4 len22) er ligetil; NFC kræver MF522-driver + kortformat. Anbefal: preset i v2, NFC udelad.

### Persistens-sikring (procedure for ourui)
Jf. `WRITETEST §7` + nærværende §1:
1. **Backup + md5 før hver write:** kopiér `/customer/config.json` → backup, log md5 (baseline `<config-md5-redacted>`).
2. **Generér byte-eksakt cJSON** (`"key":\tvalue`, tab-indrykning) — kun ønskede felter ændres.
3. **Atomisk skrivning:** tmp-fil + `mv` (ALDRIG in-place — demo_v9's persistente write-fd ville ellers overskrive/korrupte).
4. **Rækkefølge:** **config først, så MCU-write.** Hvis ourui skriver direkte til MCU (holder ttyS1), send mirror-blokken med korrekt pakkede word112/113 og 0 i trigger-ord. Hvis ourui kun ændrer config og genstarter demo_v9, klarer **boot-push** (§1.4) MCU-synken automatisk.
5. **Reboot-fri reload:** `kill $(pidof demo_v9)` → watchdog `test.sh` genstarter på **<4 s** og læser nye værdier + boot-pusher dem. Effektiv nedetid ~3–5 s. (Ingen fuld reboot nødvendig.)
6. **Verificér:** md5 efter write + relevant sysfs/UI-signal (fx PWM duty for backlight: `duty = 100 − 7·config_v`).
7. **md5-log:** log hver config-ændring (før/efter-md5, felt, værdi, tidsstempel) jf. WRITETEST.
8. **MCU-write først eller config først?** For *øjeblikkelig* MCU-effekt uden genstart: **MCU-write først** (så handlingen sker), derefter config (så den persisteres). For *kun* persistens: config + kill. ourui bør gøre **begge** (MCU-write + config) for fuld parity med original-UI'ets onClick-handlers (som gør begge dele).

---

## 6. SIKKERHEDS-NOTER & QUIRKS

1. **Ingen MCU range-checks:** cmd6 = ren `memcpy`+ACK (REGISTERMAP §3). Alle 16-bit-værdier accepteres. **ourui's min/max/step er eneste værn.** Håndhæv grænserne i §2 før write.
2. **Trigger-ord skal være 0:** word 3/6/9 (start/tare), 26/27 (kalibrering), 30 (reboot) i enhver blok der ikke eksplicit skal trigge dem. Især **sub24 len8 (steam)** og **sub18 len14** inkluderer word26/27/30 — sæt dem til 0.
3. **word30=1 → MCU reboot** (AIRCR reset). Kun i page_reset. Aldrig i normale blokke.
4. **word26/27 ≠0 → kalibrering** (grind_k/extract_k skrives til MCU-flash). Kun i page_test_scale.
5. **Ekstreme temperaturer:** MCU-fejl-detektor sætter E1/E2/E3 (head>110 °C→E1, e-boiler>120 °C→E3, s-boiler>160 °C→E3). Men cmd6 accepterer stadig værdien. ourui bør cap'e til UI-grænserne (85–96 °C bryg, 45–70 °C mælk, 30–80 °C head).
6. **Pre-infusion picker-anomali:** page_time_zone's rækker (tryk/tid/break/head-temp) pusher ved klik `nav+0x6c` — som `app_pages_init` registrer som **wifi-siden** [0xf1c40]. Dette er enten en firmware-quirk eller en delt picker-side. **Værdi-formlerne er ikke desto mindre bevist** fra CLICKED-pathen (§2.1.1). Picker'ens option-antal (max-værdier) er derfor ikke endeligt kortlagt — verificér når enheden er online. Implementér formlen + fornuftige grænser.
7. **breakTime overskrives af MCU** med preInfusionTime (§4) — separat breakTime har ingen effekt i standard-bryg.
8. **brewMode vestigial**, **sleepMode/sleepModeSetting uden editor** (§0.5/0.6) — lad være med at implementere en editor som "parity" (det findes ikke i originalen).
9. **Hot water-temp er read-only** (e-boiler sensor ord40). Ingen setpoint-write findes.
10. **Steam kræver fysisk wand-kontakt** (PE.2, ord44 bit0) — en ren write starter ikke steam uden wand'en (SESSION §5.2). ourui kan ikke erstatte wand'en med en write alene.

---

## 7. VERIFIKATIONS-STATUS

| Lag | Metode | Sikkerhed |
|---|---|---|
| Frame-format/CRC/sub+len-bånd | PROTOCOL + live (BREW/HOTWATER/SESSION) | **Høj** |
| Config-accessors (key+default) | alle 86 resolved i parity_accessors.json | **Høj** |
| Alle 82 usart_send-kaldsteder (sub,len) | parity_calls.json (automatisk resolve) | **Høj** |
| Enkelt-parameter-writes (grind/bryg/vand/temp/gear) | kode + live-frames | **Høj** |
| Pre-infusion byte-pakning (word112/113) | onClick + model_data_init + MCU 0x1E10 | **Høj** (pakning), **Middel** (picker-max) |
| Boot-push (sub1 len30 + sub112 len3) | fuart_thread disassembly | **Høj** (statisk; korrigerer WRITETEST) |
| Hot water-temp = read-only | hotWaterTemperUpdate læser ord40 | **Høj** |
| brewMode/sleep uden editor | 0 kaldere til setters | **Høj** |
| MCU pre-infusion-profil (tryk 3/4, 120/220V) | MCU 0x1E10 disassembly | **Høj** (statisk) |
| Picker option-antal (max for tryk/tid/break/head) | anomal navigation (nav+0x6c) | **Lav** — kræver online-verifikation |
| Volume diskret vælger-mapping | setting_com High/Medium/Low/Mute | **Middel** |

**Reproduktion:**
```bash
cd .
.venv/bin/python artifacts/ourui/parity_analysis.py     # bygger accessors/calls/funcs JSON
.venv/bin/python artifacts/ourui/sdump.py <start_hex> <slut_hex>   # annoteret disassembly
```

---

## 8. APPENDIKS — nøgle-adresser (demo_v9)

| Symbol | Adresse | Rolle |
|---|---|---|
| `configfileOpen` / `configFileClose` | 0xd0ac4 / 0xd0c40 | config-læs / øjeblikkelig genskriv |
| `getNumber` / `setNumber` | 0xd0eec / 0xd0fa4 | cJSON get/set (+persist) |
| `model_data_init` | 0xcef40 | config→model/mirror ved boot |
| `app_init` | 0xd9080 | kalder model_data_init, PowerOn |
| `fuart_thread` | 0x14671c | UART-loop; boot-push sub1 len30 [0x14685c] + sub112 len3 [0x146b0c] |
| `usart_send` | 0x146d5c | cmd6 write fra mirror |
| `usart_ckeck` | 0x146e70 | cmd3 query |
| home `onClick` | 0x1088a4 | grind/bryg/steam/hotwater start-blokke |
| `pagePresetStoreApply` | 0x127c84 | preset sub4 len22 |
| `app_nfc_thread` | 0x14291c | NFC sub4 len21 |
| `SleepRefresh` / `powerOff` / `recodeTime` | 0x149268 / 0x149074 / 0x148164 | hardcodet sleep (1800/600) |
| MCU `brew-ctl` / opskrift / `head_heat` | 0x1e9c / 0x1e10 / 0x399e | pre-infusion + head-temp ×10 |

*— research sub-agent, 2026-11-17. Ren STATISK analyse; enheden ikke berørt. Alle adresser verificerbare i demo_v9.dis / MEM03_V514_260923.bin.*
