# Meraki+ — UI-guide

Denne guide er uddraget af den fulde UI-specifikation ([dev/UI_v6.md](dev/UI_v6.md)) og beskriver
siderne, touch-zonerne og indstillingerne i **Meraki+ v6.14**. Kort brugervejledning: se
[README → Brug](../README.md#brug). Adresser i firkantede parenteser (fx `[0x12bcfc]`) henviser til
stedet i det originale UI-program, hvor grænsen er aflæst (se [dev/PARITY.md](dev/PARITY.md)).

## 3. Side-kort og touch

Fem sider i en ring; **swipe vandret** skifter (v6.11: gestussen skal *starte* vandret — akse-lås ved de første 14 px — være ≥ 60 px lang og højst ~30° fra vandret, |dx| ≥ 1,7·|dy|; v6.10: |dx| ≥ 60, |dy| ≤ 50). Side-prikker nederst; på HJEM desuden et **tandhjul** (v6.11), der åbner BRYG.

| # | Side | Indhold |
|---|---|---|
| 0 | **HJEM** | **v6.3:** titellinjen *MERAKI+* øverst inden for ringen · gauge, fase-chip, hero-tal, auto-stop-linje, chart (40 s), 3 stats, 4 aktivitets-knapper, side-prikker. **v6.8:** ingen REVERT-knap |
| 1 | **BRYG** | 9 rækker — espresso-kerne (spec tier 1) |
| 2 | **DAMP · VAND** | 5 rækker — mælk, damp, varmt vand + vand-temp-**sensor** (spec tier 1) |
| 3 | **SYSTEM** | 11 rækker — **v6.3: UI-SPROG øverst** (ourui's egen indstilling), derefter skærm, lyd, stock-sprog, tid (spec tier 2 + de tier 3-felter der er rene config-flag) + **v6.2:** *OPSTART · UI-VALG* med **BOOT-UI** og **SKIFT TIL ORIGINAL NU** |
| 4 | **DIAGNOSE** | link, RX OK / CRC-fejl, work_sta, E1–E3, cmd6 OK/retx/fejl/**blokeret**, vågen, net, frame-ms, spejl 112–114, config-md5 + **v6.2:** boot-blok (flag, næste boot, exe-slot, slot B, switcher, stock-proces, PID'er, seneste skift) + **v6.3:** *OM*-blok (UI-sprog og hvor det kommer fra, ordmærke m. version, tagline) — siden scroller lodret |

Modale visninger: **splash** (boot), **stepper-detalje** (fra en liste-række) og — fra v6.2 — **system-handling** (BOOT-UI, skift-bekræftelse, exit-påmindelse; samme geometri og knap-zoner som stepper-detaljen) samt den sidste frame før et runtime-skift.

**HJEM pr. fase** — hero-tal og gauge skifter mening, så det vigtigste altid er størst:

| Fase | Hero | Gauge | Stats |
|---|---|---|---|
| KLAR / VARMER OP / SOVER / FEJL | brygkedel-temp (ord 40) | 20–120 °C, markør = bryg-setpoint | gruppe (42), damp (47), vægt (39) |
| KVÆRNER | dosis-vægt (ord 38) | vægt / mål, markør = mål | tid, strøm (58), kværn-trin (37) |
| PRE-INFUSION / EKSTRAKTION | vægt i kop (ord 39) | **tryk rå** (ord 51, 0–1600) + indre ring = vægt/mål | tid, ≈bar · est., flow (52) |
| VARMT VAND | vægt (39) | vægt / mål | tid, flow, kedel |
| DAMP | mælk-temp (ord 48) | 20–80 °C, markør = mål | tid, kedel (ord 47), niveau |
| FÆRDIG (8 s) | slutværdi | som aktiviteten | "FÆRDIG PÅ x S" |

**Tryk-mærkning (v6.10; v6.11: kortere tekster):** gaugen viser fortsat rå-skala. Bar-tallet er mærket `≈BAR EST.` (v6.10: `≈BAR · EST.`) og chartet `TRYK` (v6.10: `TRYK ≈BAR · EST.`; estimatet står i feltet lige under). Omregningen er ét lineært anker: pre-infusion-plateauet i optagelsen (358 rå) = config `preInfusionPressure` (4 bar), dvs. k = 4/358 = 0,01117 bar pr. rå enhed. Over 20 bar vises intet tal (*PULS*). Grundlag og forbehold: [§16](#16-tryk-estimat-v610). (Til og med v6.9: `≈BAR*` / `TRYK* UKALIBRERET` med hypotesen "1346 rå ≈ 9 bar".)

**Touch-zoner** (til test; pixel-koordinater):

| Element | Zone | Gestus |
|---|---|---|
| KVÆRN / BRYG / VAND / DAMP | cirkler r=30 om (150 / 210 / 270 / 330, 372) | **hold ≥ 650 ms**; kort tryk giver kun en hjælpe-toast |
| STOP DAMP (kun under damp) | 150..330 × 344..400 | tap |
| ~~REVERT~~ | — | **fjernet i v6.8** (CHANGES-v6.8): ingen tryk-zone tilbage i 150..330 × 404..480. Samme vej ud: SYSTEM → *SKIFT TIL ORIGINAL NU* (i run.sh-tilstand en ren exit) |
| Kant-tap (HJEM + DIAGNOSE; **v6.11 også listerne**: x<64 / x≥416) | x<70 / x>410, y 150..330 | tap = forrige / næste side |
| **v6.11** tandhjul (kun HJEM) | 208..271 × 426..471 (64 × 46 px) | tap = BRYG-listen (ingen frame) |
| **v6.11** luk-chevron (detalje + system-visning) | 196..283 × 34..79 (88 × 46 px) | tap = ANNULLER / TILBAGE |
| **v6.11** stryg ned (detalje + system-visning) | hvor som helst, ikke startet på −/+ | ≥ 70 px lodret, ≤ ~30° skævt = ANNULLER / TILBAGE |
| Liste-række k | x 64..416, y-center 121 + 58·k − scroll | tap åbner detalje; lodret træk scroller |
| **v6.2** BOOT-UI / SKIFT TIL ORIGINAL NU | SYSTEM række 12 / 13 (v6.3: én række længere nede pga. UI-SPROG): y-center 121 + 58·k + 30 − scroll (k = 11 / 12; max scroll 488 → y 301 / 359 — samme skærm-position som i v6.2) | tap åbner system-handlingen |
| **v6.2** DIAGNOSE | hele feltet y 86..394 | lodret træk scroller (v6.3: max 431; boot-blokken står øverst i feltet ved scroll 280, OM-blokken ved max; v6.10: TRYK · ESTIMAT mellem GRAF og OM) |
| **v6.10** chartet / BAR-feltet | chartet 118..362 × 240..294, når det viser tryk; BAR-feltet 193..287 × 298..338 under bryg | tap = toast med grundlaget (*PRÆ-INF. 4 BAR = RÅ 358 · LINEÆR*) — ingen frames |
| − / + | cirkler r=42 om (84,184) / (396,184) | tap = ét trin; hold = gentag (170 → 90 → 45 ms, ×5-trin efter ~28 gentagelser) |
| ANNULLER / GEM | 100..236 / 244..380 × 284..348 | tap |
| TILBAGE (låst række / sensor) | 170..310 × 284..348 | tap; −/+ findes ikke |

---

## 4. Settings-tabel (det der faktisk er implementeret)

Én tabel (`SET[]`) er eneste sandhed for label, config-felt, ord, grænser, trin og status. Alle tal er **rå config-enheder** og inklusive. Kolonnen **Status**: **BEVIST** = interval og trin er adresse-bevist i PARITY.md §2 · **SKØN** = min og trin er bevist, max er et skøn · **LÅST** = vises uden editor · **SENSOR** = read-only måleværdi.

### 4.1 Side BRYG (spec tier 1)
| Label | config-felt | Ord | Write ved GEM | Min..max / trin | Vises som | Status |
|---|---|---|---|---|---|---|
| KVÆRN-DOSIS | `autoGrindWeight` | 4 | sub4 len1 | 10..309 / 1 | 1,0–30,9 g | **BEVIST** [0xf4984] |
| KVÆRN-TILSTAND | `grindMode` | 2 | sub2 len1 | 1..2 | 1 VÆGT / 2 TID | **BEVIST** [0x122180] |
| BRYG-VÆGT | `autobrewWeight` | 23 | sub23 len1 | 100..990 / 10 | 10–99 g | **BEVIST** [0x12be5c] |
| BRYG-TEMP | `brewTemper` | 11 | sub11 len1 | 850..960 / 10 | 85,0–96,0 °C | **BEVIST** [0x12bcfc] |
| PRE-INFUSION | `preInfusionSwitch` | 112 hi-byte | sub112 len2 | 0..1 | FRA / TIL | **BEVIST** [0x103858] |
| PRE-INF. TID | `preInfusionTime` | 113 hi-byte | sub113 len1 | 5..15 / 1 (loft 60) | 5–15 s | **SKØN** (max) |
| PRE-INF. TRYK | `preInfusionPressure` | 112 lo-byte | sub112 len2 | 3..4 / 1 | 3–4 bar | **BEVIST** (MCU 0x1E10 kender kun 3 og 4) |
| PAUSE-TID | `breakTime` | 113 lo-byte | — (ingen editor) | — | 5 s | **LÅST** (uden effekt, §4.5) |
| GRUPPEHOVED | `headTemper` | 114 | sub114 len1 | 30..80 / 10 (loft 100) | 30–80 °C | **SKØN** (max) |

### 4.2 Side DAMP · VAND (spec tier 1)
| Label | config-felt | Ord | Write ved GEM | Min..max / trin | Vises som | Status |
|---|---|---|---|---|---|---|
| MÆLKETEMP | `AtuoSteamTarge` | 25 | sub25 len1 | 450..700 / 10 | 45,0–70,0 °C | **BEVIST** [0x12bf80] |
| DAMP-NIVEAU | `steamGears` | 14 | sub14 len1 | 1..4 / 1 | BLID / MILD / MEDIUM / STÆRK | **BEVIST** [0xdd028] |
| VARMT VAND | `autoHotWaterTarget` | 17 | sub17 len1 | 300..5000 / 100 | 30–500 g i trin à 10 g | **BEVIST** [0xfbf0c] |
| VAND-TILSTAND | `hotWaterMode` | (16/19 i start-blok) | — kun config | 1..2 | 1 AUTO / 2 MANUEL | **BEVIST** [0x11344c] |
| VAND-SENSOR | *(ingen)* | 40 (read-only) | — ingen write findes | — | live, fx 99,9 °C | **SENSOR** [0x112c9c] |

### 4.3 Side SYSTEM (kun config — ingen MCU-write)
| Label | config-felt | Min..max | Vises som | Spec-tier | Status |
|---|---|---|---|---|---|
| **UI-SPROG** (v6.3) | *(ingen — egen fil `/new_customer/ourui/lang`)* | 0..1 | ENGLISH / DANSK (endonymer) | — | ourui's egen indstilling; **ikke** en spec-parameter og ikke i `--dump-table` (CHANGES-v6.3 pkt. 2) |
| LYSSTYRKE | `backlight` | 1..10 | live preview (PWM `duty = 100 − 7·v`) | 2 | **BEVIST** 0..10; **0 bevidst udeladt** (duty 100 = mørk skærm, som brugeren ikke kan finde tilbage fra) |
| LYDSTYRKE | `volume` | 0..100 | 0–100 | 2 | **BEVIST** for slideren; UI-hint om den diskrete vælger (§4.4) |
| TEMP-ENHED | `Temperature` | 0..1 | 0 °F / 1 °C — styrer også ourui's visning | 2 | **BEVIST** |
| STOCK-SPROG (før v6.3: SPROG) | `language` | 0..10 | 11 sprog, uden dansk (navnene vises på UI-sproget) | 2 | **BEVIST** — oplåst i v6.1; styrer kun stock-UI'et |
| VAND-GENVEJ | `hotWaterShortcut` | 0..1 | FRA / TIL | 2 | **BEVIST** |
| SOMMERTID | `sumtime` | 0..1 | FRA / TIL | 2 | **BEVIST** |
| RENSE-PÅMINDELSE | `clearTip` | 0..1 | FRA / TIL | 2 | **BEVIST** |
| TIDSFORMAT | `timemode` | 0..1 | **0 = 24-TIMERS / 1 = 12-TIMERS** | 3 | **BEVIST** [`update_time` 0xd5978] |
| AUTO-TID | `Automatically` | 0..1 | FRA / TIL | 3 | **BEVIST** |
| TIDSZONE | `timeZone` | — | 48 | 3 | **LÅST** (spec har ingen indeks-tabel) |

### 4.4 Hvad der stadig er skøn eller låst
| Række | Hvad er usikkert | Hvordan UI'et viser det | Hvorfor det er forsvarligt |
|---|---|---|---|
| PRE-INF. TID | pickerens option-antal er ikke kortlagt (PARITY §6.6); max 15 = stock's egen default | **`MAX = SKØN`** i rav ved intervallet + `≈` foran værdien i listen | 5–15 s er harmløst; feltet er én byte |
| GRUPPEHOVED | samme picker-anomali; spec'ens max 80 er selv et forsigtigt skøn | **`MAX = SKØN`** + `≈` | E1 udløses først ved > 110 °C; 80 ligger langt under |
| LYDSTYRKE | stock har to kontroller: slider 0–100 (det ourui redigerer) og en diskret vælger High/Medium/Low/Mute, hvis mapping ikke er kortlagt | **`TRIN = SKØN`** + `≈` (noten siger *"… · SLIDER 0-100"*) | ren config, ingen MCU-write |
| PAUSE-TID | max ukendt — og værdien har ingen effekt | låst, rav note *"INGEN EFFEKT · MCU BRUGER PRE-INF. TID"* | §4.5 |
| TIDSZONE | indeks-tabellens størrelse og betydning | låst, *"SPEC HAR INGEN INDEKS-TABEL"* | et ugyldigt indeks kunne vælte stock's tids-/DST-logik |

**Skøn-reglen:** har stock-UI'et selv gemt en værdi over mit skønnede max (fx `headTemper=90`), er det evidens for at pickeren når dertil, og intervallet udvides til den værdi — dog aldrig over loftet (60 s / 100 °C). **Beviste** grænser udvides aldrig.

**Verificér online** (§11 M): tæl pickerens options for pre-inf. tid/tryk/pause og gruppe-temp i stock-UI'et, og ret `max` i de to SF_EST-rækker.

### 4.5 Bevidst fjernet, låst eller ikke eksponeret
| Felt | Beslutning | Begrundelse (spec) |
|---|---|---|
| varmt-vand-**temperatur** | **sensor-række**, ikke en indstilling | Stock har intet setpoint: den viste temperatur er e-boilerens målte (ord 40, `hotWaterTemperUpdate` 0x112c9c). Rækken står på DAMP · VAND, hvor man ville lede efter indstillingen, og siger *"INTET SETPOINT FINDES"* |
| `breakTime` | **låst**, værdien vises | MCU-opskriften 0x1E10 kopierer `preInfusionTime` ind i ord113 lo-byte, før timingen `ord13 + ord113_lo` beregnes — den separate pause-værdi bliver altid erstattet. En editor ville være en knap uden virkning. Værdien pakkes fortsat i ord113 lo, så frames er identiske med stock's |
| `brewMode` | **fjernet** | vestigial: `get/setExtractModeValue` har 0 kaldere |
| `sleepMode`, `sleepModeSetting` | **fjernet** | ingen editor i stock (setterne har 0 kaldere); sleep-timing er hardcodet (~600 s screensaver / ~1800 s power-off). ourui holder selv maskinen vågen (§7.1) |
| `theme` | ikke eksponeret | feltet findes ikke i `config.json`, og `getTheme` har **0 kaldere** i `demo_v9` (`grep -c '<getTheme>' demo_v9.dis` = 1, kun definitionen) — en gemt værdi ville aldrig blive læst. ourui tilføjer desuden aldrig nøgler |
| `presetStore` / NFC | udskudt | §9 |
| wifi (`ssid`, `password`, `wifionoff`), `init`, `patch`, versionsfelter | ikke eksponeret | må ikke røres / ikke brugerindstillinger |
| kalibrering (ord 26/27), reboot (ord 30), drain (28/29), back-flush (20), self-test (8) | ikke eksponeret **og** umulige at sende | §7a |

### 4.6 Range-vagt (tre lag)
1. **Stepperen** clamper til intervallet og snapper til trin-gitteret.
2. **GEM** afviser alt uden for intervallet; aktivitets-start spærres, hvis en config-værdi der indgår i en cmd6-frame er uden for spec eller mangler (*"CONFIG UDEN FOR SPEC · RET FØRST"*).
3. **Frame-politikken** (§7a) afviser enhver frame med et ord uden for sit interval — også hvis lag 1–2 skulle have en fejl.

En config-værdi uden for et bevist interval kan kun opstå, hvis filen er ændret uden om stock-UI'et. Den vises som den er, i rødt (liste-række med rød tone, detalje med *"UDEN FOR SPEC · VÆLG NY VÆRDI"*), og første tryk på −/+ fører den ind i intervallet.

---

## 4a. Quirks fra spec'en — og hvad ourui gør ved dem

| # | Quirk (PARITY) | Konsekvens i ourui |
|---|---|---|
| 1 | Hot water har ingen temperatur-indstilling; visningen er ord 40 | sensor-række (§4.5) |
| 2 | `brewMode` vestigial | fjernet |
| 3 | `sleepMode`/`sleepModeSetting` uden editor, timing hardcodet | fjernet |
| 4 | ord112 = `(switch<<8)\|tryk`, ord113 = `(tid<<8)\|pause`, ord114 = `headTemper` i hele °C (MCU ×10) | `pack112`/`pack113` matcher præcist — testet for alle kombinationer + 5 byte-vektorer (§10) |
| 5 | MCU 0x1E10 håndterer kun tryk 3 og 4; andre værdier ignoreres | interval 3..4; frame-politikken afviser ord112 med andet tryk |
| 6 | ord13 = 60 ved boot, overskrives af 0x1E10 (340/440 ved 120 V, ord15 = 300/350 ved 220 V) | ourui sender ord13/15 = 60 som observeret og rører dem ellers ikke; politikken afviser andre værdier |
| 7 | `breakTime` overskrives af MCU'en | låst (§4.5). Før BRYG sammenlignes ord113 kun på **hi-byten**, ellers ville spejlet (fx `0606` efter et bryg) udløse en unødig re-sync hver gang |
| 8 | Boot-push findes: `fuart_thread` sender `sub1 len30` + `sub112 len3` | reload-semantikken i §5 er rettet |
| 9 | Trigger-ord 3/6/9/26/27/30 skal være 0 i normale blokke; ord30 = 1 rebooter MCU'en | §7a |
| 10 | ~~Damp kræver fysisk wand-kontakt~~ — **afkræftet live** (STEAMLIVE §6.3): display-damp starter 0,22 s efter writen med wand-kontakten = 0. Wand-kontakten alene giver en *flush* uden auto-stop | **v6.6:** *"STARTER DAMP…"*, derefter *DAMP·AUTO*; wand alene = *DAMP·WAND* uden STOP-knap ([CHANGES-v6.6](#changes-v66) pkt. 2) |
| 11 | Picker-navigation anomal → option-antal ukendt | SF_EST + synlig hint (§4.4) |
| 12 | Volume: slider + diskret vælger | hint (§4.4) |

**To fund ud over spec'en** (begge verificerbare i `demo_v9.dis`):
* **`timemode` var vendt om i v6.** `model_data_init` gemmer `timemode != 0` i model+0x59, og `update_time` [0xd5978] formaterer med `"%02d:%02d"` når den er 0 og `A.M./P.M.` ellers. Altså **0 = 24 t, 1 = 12 t**. Nuværende config har 1, så stock viser 12-timers ur.
* **`sumtime` er sommertid**, ikke "timer-visning" som spec'ens label gætter: accessoren hedder `getSumerTimeSwitchValue` [0xd2804]. v6's label SOMMERTID er beholdt.

---

## 18. Manuel STOP (v6.13)

**Fejlen (brugerrapport):** varmt vand kunne ikke stoppes fra Meraki+-skærmen. Kun damp havde en STOP-knap; kværn, bryg og vand stoppede kun ved vægt-målet. v6.13 giver alle fire aktiviteter en STOP-knap. Auto-stop er uændret — STOP er et tillæg.

### 18.1 Stop-frames — byte for byte, med bevis

Alle tre stop-ord er fundet i MCU-firmwaren (`MEM03_V514_260923.bin`, md5 `e96155da…`, disassembly i `mcu_analysis/`) og krydstjekket mod stock-UI'ets egne stop-kald (`demo_v9`, symboler bevaret, `demo_v9.dis`). Mønstret er det samme for alle tre: **MCU'ens egen auto-stop skriver netop dét ord til 0**, og en manuel write af samme ord rammer derfor præcis samme kodevej.

| Aktivitet | Stop-frame (cmd6, CRC-16/MODBUS LE) | MCU-firmware: hvad styrer aktiviteten | MCU'ens egen auto-stop | Stock-UI'ets stop-kald | Bits der ryddes |
|---|---|---|---|---|---|
| **VAND** (auto, ourui's start) | `aa 55 06 10 01 00 00 c0 0f` — sub16 len1, ord 16 = 0 | hotwater-ctl `0x1B8C`: `aktiv = (ord19 \| ord16) != 0`, genberegnet hver omgang (`0x1C44..0x1C4E`); aktiv → `work_sta \|= 0x20A` (`0x1C8A`) | vægt-stop `0x1C80`: `strh r4,[r6,#0x20]` (ord 16 = 0) | `hotWaterStartStop` `0x11468C`: spejl-ord 16 = 0, `usart_send(6,16,1)` | bit 3 altid (`0x1C9A`); bits 1+9, når kedel-påfyldningen er ledig (`[0x2000002B] == 9`, `0x1CA2`) |
| VAND (manuel tilstand) | `aa 55 06 13 01 00 00 c0 4b` — sub19 len1, ord 19 = 0 | samme (`ord19 \| ord16`) | — | `hotWaterStartStop` `0x114720`: `usart_send(6,19,1)` | som ovenfor |
| **BRYG** | `aa 55 06 16 01 00 00 c0 87` — sub22 len1, ord 22 = 0 | brew-ctl `0x1E9C`: ved hver **ændring** af ord 22 bliver `running = (ord22 != 0)` (`0x1F94..0x1FC6`) | vægt-stop `0x1F92`: `strh r5,[r6,#0x2c]` (ord 22 = 0) | home-`onClick` `0x109694`: spejl-ord 22 vendes til 0, `usart_send(6,5,18)` (hele blokken) | bits 2+12 i 1. omgang (`0x20C4`, `& 0xEFFB`), ventil-puls på bit 2 efter 10 omgange (`0x20D2`); bits 1+9, når påfyldningen er ledig (`0x20F0`) |
| **KVÆRN** | `aa 55 06 06 01 00 00 c4 47` — sub6 len1, ord 6 = 0 | grind-ctl `0x19C6`: kværnen kører, mens ord 6 ≠ 0 (`0x1AA2`: `work_sta \|= 0x40`, ellers `&= ~0x40`) | vægt `0x1A68`, timeout `0x1A72`, fejl `0x1A3E`/`0x1A8C`: alle `ord 6 = 0` | home-`onClick` `0x10928C`: spejl-ord 6 = 0, `usart_send(6,6,1)` (start-grenen `0x109278` sender `sub3 len4`) | bit 6 |
| DAMP (uændret fra v6) | `aa 55 06 18 08 00 00 8a 02 00 … 00 0c f7` — sub24 len8, ord 24 = 0 | steam-ctl `0x210E` | ord 48 ≥ ord 25 | (SESSION.md §10.3) | bits 5+14 |

**Bryg — hvorfor kun ord 22 og ikke stock's hele blok?** Stock sender `sub5 len18` med spejlets ord 5..22, hvor kun ord 22 er ændret; de øvrige 17 ord er MCU'ens egne, pollede værdier. cmd6 er `memcpy` + ACK (`0x99C6`, ingen sub-specifik logik), så for MCU'en er det eneste, der sker, at ord 22 går 1 → 0 — præcis det, ourui's `sub22 len1` gør. Den smalle frame kan ikke overskrive en opskrifts-værdi med et forældet spejl.

**Ord 9 og ord 3 er ikke stop-ord.** Ord 9 (bryg/vand) og ord 3 (kværn) er tare-triggere, som MCU'en konsumerer og nulstiller inden for 10 ms efter starten (`0x1ED2`, `0x19F2`). At skrive dem til 0 under en aktivitet gør intet. *(REGISTERMAP §2.2 kalder ord 6 "tare" og ord 7 "run". Byte-offsets `[r6,#6]` = ord 3 og `[r6,#0xc]` = ord 6 viser, at ord 3 er tare-triggeren og ord 6 run-flaget.)*

**Sikkert midt i et forløb?** Ja, for alle tre. Stock-UI'et tilbyder samme stop (vand-knappen, bryg-knappen og kværn-knappen er toggles), og MCU'en kører samme stop-vej som ved vægt-målet. For bryg betyder det også ventil-pulsen bagefter (trykaflastning, BREW.md §4).

**Kendt undtagelse (ikke observeret):** et bryg startet af den fysiske "e"-kontakt (PA.15, `0x1F52`-grenen) kører med ord 22 = 0. Dér kan ord 22 = 0 ikke stoppe brygget. ourui melder det ærligt som *"STOP IKKE BEKRÆFTET · PRØV IGEN"* efter 5 s. Denne vej er aldrig set på maskinen.

### 18.2 UI'et

* **Knappen** står samme sted og har samme stil som STOP DAMP: den røde pille i aktions-rækken (tryk-felt 180 × 56 px, pille 160 × 44 px i F_S; v6.6–v6.12's pille var 150 px). Teksterne er *STOP WATER / STOP BREW / STOP GRIND* og *STOP VAND / STOP BRYG / STOP KVÆRN*. Knappen vises, mens aktivitetens work_sta-bits står (`phase_main`). Den er væk i FÆRDIG-visningen, og ventil-pulsen efter et bryg (`0105`) giver ingen knap.
* **To knapper:** kører en hoved-aktivitet samtidig med latchet damp (fx `5327` + ord 24 = 1), står der to knapper side om side: venstre = hoved-aktiviteten, højre = STOP DAMP. Hver har et tryk-felt på 148 × 56 px og en pille på 136 × 44 px i F_XS. Wand/flush har stadig ingen STOP-knap.
* **Feedback:** et tryk (aktiveres ved slip, som alle knapper) viser straks toasten *"STOPPER VAND…"*. Når MCU'en har ryddet bits'ene, kommer *"VAND STOPPET"* (grøn). Kommer der intet ACK, heller ikke efter retransmit, vises *"STOP: INTET SVAR · PRØV IGEN"* (rød). Er bits'ene ikke ryddet efter 5 s, vises *"STOP IKKE BEKRÆFTET · PRØV IGEN"* (gul). Et ACK alene tæller ikke som bekræftelse. Et nyt tryk, mens stop-framen venter på ACK, køer ikke en frame mere.
* **Underlinjen** beholder auto-stop-forventningen: *"AUTO-STOP VED 40.0 G · ELLER STOP"*.
* **Frame-politik:** stop-frames indeholder kun nuller og går gennem den samme port og write-kø som alt andet (CRC + ACK-vent + én retransmit). De kræver ingen intent og kan ikke starte noget. `OURUI_READONLY=1` sender intet, og toasten siger hvorfor.
* **SIM:** state-JSON har `stop: {btns, pending, last, req, ok, fail}`. ctrl-handlingerne `grind_stop` / `brew_stop` / `water_stop` (+ `steam_stop`, som før) og layout-navnene `grind_stop` / `brew_stop` / `water_stop` / `steam_stop` er nye.

### 18.3 Verifikation

Lokalt (alt grønt, tal i [CHANGES-v6.13](#changes-v613) pkt. 6): 26 nye selftest-checks, previews og spec-afstemning, mutations-prøver, SIM og E2E (hurtig).

**On-device:** ikke kørt. Maskinen var offline under hele runden: `<display-ip>` svarede ikke på ping, ARP-opslaget var ufuldstændigt, og telnet-porten var lukket (2026-11-18 ca. 15:45). `verify_stop_v613.py` er klar til kørsel, når maskinen er tændt (§11, testplan S).

## 19. Tænd/sluk-knappen (v6.14)

**Fejlen:** den fysiske tænd/sluk-knap gjorde intet, mens Meraki+ kørte. ourui åbnede kun touch-noden (`/dev/input/event0`, TECHWIN_TS). Knappen sidder på en anden input-node: `/dev/input/event1` = **k62091** (GPIO-key-driveren `k62091.ko`, "Handlers=kbd event1", EV=3 = SYN + KEY, én tast). Den node læste ourui aldrig.

### 19.1 Stock-semantikken — reverset, med bevis

| Hvad | Stock gør | Bevis (adresse) | ourui v6.14 |
|---|---|---|---|
| Hvilken node | `open("/dev/input/event1", O_RDWR)` i en egen tråd — **ingen EVIOCGRAB**. For hver `EV_KEY` (koden tjekkes ikke) gemmes `value` (1 = nede, 0 = sluppet) | `app_powerKey_thread` 0x140AB4 (streng 0x17BCEC); gemt i app+0x15A8 | k62091-noden, read-only, **uden grab**. Valg: `OURUI_KBD` → node hvis navn indeholder `k62091` → første node med EV_KEY og uden EV_ABS → `/dev/input/event1`. Alle EV_KEY-koder tæller som knappen |
| Hvornår der slukkes | `SleepRefresh` (lv_timer 100 ms): kun når sekundet skifter, tælles e60 op, mens knappen er nede. `dealPowerState(1)` slukker, når e60 > 1 — dvs. knappen holdt hen over **to sekund-skift = 1,0–2,0 s**. Et kort tryk vækker kun stock's pauseskærm | `SleepRefresh` 0x149268 (timer 100 ms ved 0x1354A8), e60 = 0x1E0E60, `dealPowerState` 0x14911C | **HOLD 1,5 s** (midt i stock's vindue). Kort tryk: ingenting (talt på DIAGNOSE) |
| Spærring | Mens `isExtractionOrSteamSwitchOn()` er sand, tælles der ikke, og der slukkes ikke: work_sta bit 12 (bryg) eller bit 14 (damp), spejl-ord 22 (bryg aktiv), 24 (damp) eller 6 (kværn). **Varmt vand spærrer ikke.** Stock stopper ALDRIG en aktivitet ved sluk | `isExtractionOrSteamSwitchOn` 0x1481E0 (`GetMaichinState(0x1000/0x4000)`, ord 22/24/6) | Samme prædikat (+ ourui's egen start på vej for de tre). Toast *STOP AKTIVITETEN FØRST · SÅ SLUK*. Når aktiviteten er slut, slukker et **nyt fuldt hold** (tælleren står stille, som e60). Varmt vand: sluk sendes, ingen stop-frame |
| Ingen forbindelse | Slukker kun, hvis spejl-ord 0 ≠ 0 | 0x149210 | Kun hvis linket er oppe og controlleren har svaret med ord 0 ≠ 0. Ellers toast *INGEN FORBINDELSE · SLUKKER IKKE* — knappen vækker måske netop en sovende controller. READ-ONLY: intet |
| Sluk-frames | `powerOff`: spejl-ord 0 = 0 → `performSleepActions` → `powerOffSend(0,0,0)` = **cmd8** (15 bytes) → `usleep(80000)` → `usart_send(6,0,2)` = **cmd6 sub0 len2 [ord 0 = 0, ord 1 = spejlet]** | `powerOff` 0x149074, `performSleepActions` 0x148324 (0x148F50, 0x14905C), `powerOffSend` 0x14700C, buffer `powerSendBuf` 0x1A6030 | Præcis det: `aa 55 08 <år lo> <år hi> <md 1-12> <dag> <t> <m> <s> 00 00 00 <crc>` (eksempel 2026-11-18 16:35:28: `aa 55 08 ea 07 0b 12 10 23 1c 00 00 00 cc df`), **≥ 80 ms** senere `aa 55 06 00 02 00 00 00 00 56 94` (ord 1 = 0 = vågen, som ourui's keep-awake holder det). cmd6-framen går gennem write-køen (CRC, ACK, én retransmit) og frame-politikken med den nye intent `I_POWEROFF` |
| cmd8's indhold | år (u16), måned (+1), dag, time, minut, sekund, alarm dag/time/minut. Uden ugeplan = alarm 0/0/0 | 0x14703C–0x147148 (CRC_GetModbus16 over 13 bytes) | Systemets lokaltid + alarm 0/0/0 (ourui har ingen ugeplan). MCU'ens 0x88-kvittering tælles kun til DIAGNOSE — stock venter heller ikke på den |
| MCU'en | Status-tasken ser ord 0 = 0 (hvert 500 ms) og starter `sleep`. Den sletter `boiler_heat`, `head_heat`, **`status`** (RX-parser + svar) og `run`, sætter work_sta = 0 og nulstiller udgangene. Den vågner kun ved et **hold på knappen** (egen GPIO PC.8, 8 × 100 ms lav efter et slip) eller alarmen → `reset muc` (CPU-reset) | MEM03 0x29D4/0x29D8, sleep-task 0x2F52 (0x2E1E sletter tasks; 0x2F0C udgange 0), alarm-sammenligning 0x2FE2–0x3036, reset 0x326 (SYS IPRST) | ourui sender intet mere: keep-awake står stille, og polls besvares ikke (linket går ned — forventet). cmd8-parseren 0x9C88 gemmer de 10 bytes i 0x20000E18 |
| Skærm | Baggrundslys af (`blpwm_set_enable(2,0)` + duty 0), sluk-side. 1,5 s efter: fb lukkes, `killall dispinit`, GPIO'er frigives, **`echo mem > /sys/power/state`** (SoC-suspend). Efter resume: spejl-ord 0 = 1, `killall demo_v9` → watchdog'en genstarter stock → `PowerOn` | 0x1490C0–0x1490FC, 0x149494–0x14952C, `uikit_os_back_light_off` 0xC2278 | SLUKKER… straks, SLUKKET efter ACK. Lyset slukkes 1,5 s efter sluk-trykket: duty 99 (svageste) og så `pwm2/enable = 0` (ourui skriver ikke stock's duty 0 — med "polarity inversed" er det det kraftigste lys, hvis enable ikke slog igennem). **Ingen SoC-suspend og ingen killall** (bevidst afvigelse: net-vagt og telnet lever videre; ourui er ikke en genstart-bar app bag en watchdog i run.sh-tilstand) |
| Tænd | SoC'en vågner af samme knap-tryk; stock genstarter og sender `PowerOn` (ord 0 = 1) ved opstart. MCU'en vågner af sig selv (reset; ord 0 starter som 1) | `app_init` 0xD9170 → `PowerOn` 0x146EF8 | Knap-tryk i SLUKKET → lyset tændes, *TÆNDER… · HOLD KNAPPEN INDE ET ØJEBLIK*. Når controlleren har svaret med ord 0 = 1 i 0,6 s → PowerOn `aa 55 06 00 01 01 00 c5 5f` + vågen `aa 55 06 01 01 00 00 c5 33` (samme to frames som ved opstart) → HJEM, toast *MASKINEN ER TÆNDT*, keep-awake kører igen. Uden svar i 10 s: tilbage til SLUKKET. Svarer controlleren af sig selv med ord 0 = 1, mens ourui står i SLUKKET (alarm, eller knappen blev holdt uden at ourui så det), tændes der på samme måde. Efter MCU-resettet står controlleren på sine startværdier — **ourui's re-push** (v6.6, 0x89-runden) lægger config'ens værdier tilbage |
| Touch, mens der er slukket | (SoC'en sover — touch gør intet) | — | Tryk sluges (ingen navigation, ingen frames). SLUKKET-siden vises i 8 s; så slukkes lyset igen. Touch kan ikke tænde maskinen: controlleren kan ikke modtage noget, mens den sover |

### 19.2 UI'et
* **SLUKKER… / SLUKKET / TÆNDER…** er en sort side med et tænd/sluk-symbol (ring + streg; crema under SLUKKER og TÆNDER, nedtonet under SLUKKET). Under SLUKKET: *HOLD KNAPPEN INDE FOR AT TÆNDE* og *SKÆRMEN SLUKKER · RØR FOR AT VISE*. En åben detalje lukkes uden GEM (lys- og sprog-preview fortrydes); efter TÆND står Meraki+ på HJEM, som stock (`navigator_popToHome`).
* **DIAGNOSE → TÆND/SLUK-KNAP** (ny blok mellem RÅ TÆLLERE og OM): tilstand, *STRØM · ORD 0* (controllerens svar eller *INTET SVAR*), *TRYK · SLUK · TÆND*, *SIDSTE KNAP* (`kode = værdi · alder`), node (`/dev/input/event1 · k62091` eller *INGEN KNAP-NODE FUNDET*).
* Ark: `power_states_en.png` · `power_states_da.png`.

### 19.3 Grænser
* Kun baggrundslyset slukkes; SoC'en kører videre (strømforbrug: displayet tegner stadig 10 billeder/s i sort). Det er den eneste bevidste afvigelse fra stock.
* Tryk på knappen, mens stock-UI'et kører (slot A), håndteres af stock som før. I run.sh-tilstand er stock SIGSTOP'et; dets egen kø på event1 får de samme tryk og læser dem efter SIGCONT — sidste værdi er "sluppet", så stock slukker ikke af den grund.
* `mcu_sim.py` modellerer endnu ikke MCU'ens dvale (tavshed på ord 0 = 0, reset ved knap). SIM'en har en ctrl-handling `{"cmd":"key","value":1|0}` og `power` i state-JSON; se `SIM_HANDOFF`.

### 19.4 Verifikation
Lokalt alt grønt (tal i [CHANGES-v6.14](#changes-v614) pkt. 5). On-device: **ikke kørt** i denne runde (installationen af v6.13 i slot B var i gang). `verify_power_v614.py` er klar (§11, testplan T).
