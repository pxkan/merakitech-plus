# REGISTERMAP — MerakiTech Gen 1 powerMcu (Nuvoton M451, RT-Thread 5.1.4)

> **Note (offentlig udgave):** Dette dokument er udviklingsnoter fra reverse engineering af en MerakiTech Gen 1, som forfatteren ejer. Producentens firmware, binærer og scripts, som der henvises til (fx `demo_v9`, `MEM03_V514_260923.bin`, `demo.sh`, `test.sh`), er **ikke** inkluderet i repoet. Adresser og korte uddrag er medtaget alene for interoperabilitet. IP-adresser og andre personlige data er fjernet. Se [README → Ansvarsfraskrivelse](../README.md).

**Dato:** 2026-11-16 · **Metode:** udelukkende STATISK reverse-engineering af `artifacts/MEM03_V514_260923.bin` (63.460 bytes, MD5 `e96155dad2443063acb9ecea959854a3`, flash-base `0x0`, RAM 32 KB `0x20000000`). Ingen netværksenheder berørt.
**Værktøj:** capstone 5.0.7 (rekursiv Thumb-2-disassembly + TBB/TBH-hop-tabeller + konstant-propagation), pyelftools. Alle adresser nedenfor er flash-offsets i MCU-binæren og kan verificeres med `artifacts/mcu_analysis/dump.py <start> <slut>`.
**Forudsætning:** læs `PROTOCOL.md` først (frame-format `AA 55`, CRC-16/MODBUS, chip-ID M451). Nærværende rapport **kortlægger register-filen** og **korrigerer** to fejl i PROTOCOL.md (se §7).

---

## 0. Resumé (TL;DR)

* Register-filen ("APP Param") = **129 × 16-bit-ord i RAM `0x20000F96`** (index 0..128). Initialiseret @ `0x9966` (`memset` 256 bytes + defaults). MCU'en bruger derudover **interne ord 128..131** (temperatur-array) lige efter tabellen.
* **Arkitektur:** display skriver *mål/opskrifter* (cmd 6, ord 0..31 + 112..114). MCU'ens kontrol-tasks læser disse og sætter **`work_sta` = ord 33** (aktuator-bitfelt). En separat **"run"-task (`0x2782`) spejler ord 33's bits til fysiske GPIO-relæer**. Sensor-tasks skriver temperaturer/vægte/fejl tilbage til ord 32..111, som broadcastes (cmd `0x83`, sub 32, len 80).
* **cmd 6-write = ren `memcpy` + ACK** (`0x99C6`): ingen værdi-range-checks; skriver ordene og echo'er. Side-effekter er *indirekte*: kontrol-tasks poller trigger-ord (tare, kalibrering, reboot) og reagerer asynkront.
* **UART3 er IKKE `@C/@R/@W`-ASCII** (PROTOCOL-fejl): det er en **binær `AA BB CMD LEN payload CRC16`**-protokol (parser `0x43CC`, payload-ord **big-endian**). Bruges til et vægt-/kontrol-modul (grind/brew/steam/hotwater-start/stop + kalibrering + identitet).
* **Ord 112..114 er IKKE OTA-styring** (PROTOCOL-fejl): de er **head-temp/produkt-setpoints** (`word114=head_set_temp`, `word112=pro_sw`). OTA kører **udelukkende** via `CC DD`-protokollen (se §5).
* **OTA (`CC DD`):** request → validér version/størrelse → slet staging → ACK `0x81` → start "updata"-task → sekventielle 256-byte datapakker til fragmenteret flash-staging → checksum → skriv magic `0x87654321`+size+check @ `0x19800` → reboot. Bootloader kopierer staging→app.

---

## 1. Arkitektur & task-kort (bevis: task-skaber `0x2A40`, streng-sammenligninger)

RT-Thread-tasks oprettet via `0x2A40` (navn→entry, prioritet, tick, stack). Navne-strenge @ `0x2D14..0x2D70`:

| Task | Entry | Prio | Tick | Rolle (bevis) |
|---|---|---|---|---|
| `init` | `0x2C00` | 6 | 10 ms | boot-init; kalder config-load `0x92E` (`0x2C0E`) |
| `status` | `0x286E` | 8 | 10 ms | RX-parse + sensor-loop + broadcast (`0x29F6`→`0x9AAC`) |
| `run` | `0x2782` | 5 | 10 ms | **ord 33 → GPIO-aktuatorer** (`0x27E6..0x2866`) |
| `updata` | `0x2768` | 5 | 1000 ms | **OTA-processor** (`0x2774`→`0xBFD6`, loop `0xBAA8`) |
| `boiler_heat` | `0x3502` | 8 | 10 ms | extract/steam-boiler PID (`0xA5EE`), skriver ord 62/63 |
| `head_heat` | `0x399E` | 8 | 10 ms | bryghoved-PID; feedback ord 128, setpoint ord 114 |
| `sleep` | `0x2F52` | 7 | 10 ms | dvale (startes når ord 0=0, `0x29D8`) |
| `drain` | `0x30E8` | 7 | 10 ms | udtømning/rens (startes når ord 1=1, `0x29E4`) |
| `product` | `0x4E12` | 7 | 10 ms | **bryg/produkt-statemaskine**; kalder UART3-parser `0x43CC` (`0x4F16`) |
| `pcb_product` | `0x5872` | 5 | 10 ms | fabriks-test/PCB-produkt |
| `self_inspection` | `0x2494` | 8 | 10 ms | **selvtest + status-broadcast** (`0x2744`) |

Derudover "box"-threads (`box_grind/extract/steam/execute/control`, strenge `0x6010..0x6040`, skabt @ `0x5EB2..0x5EDA`) og hjælpe-tasks.

**Sensor-/aktuator-kæder (status-task `0x286E`, modulo-tællere):**
* hver 2. tick: `0xB2DE`→**ord 70** (e-vandniveau), `0xB306`→**ord 68** (s-vandniveau) (`0x2928/0x2930`).
* hver 4. tick: `0x933C`→**ord 52** (flow-hastighed), `0xA7D8` (SPI2-ADC: vol/hz/cur) (`0x2950`).
* hver 5. tick: `0x8A62` (EADC-scan), `0x5A2` (`0x2940`).
* hver 10. tick: `0xB268`, `0x226E` (add_water), **ord 44** = input-bits fra 5 GPIO (`0x2970..0x299C`).
* hver 20./30. tick: `0x1CB6`/`0xA1B2`/`0xA406` (fejl), `0xA42` (grind-kalibrering).
* `0xA336` (ADC-temp) skriver **ord 40/41/42/47/48 + 128..131**; `0xA406` (fejl-detektor) skriver **ord 34/35/36**.
* **ord 30** = reboot-flag: `0x2A06` læser ord 30; hvis ≠0 → nulstil, delay 20, `SCB->AIRCR=0x05FA0004` (`0x2A18`).

---

## 2. REGISTERKORT — alle 129 indeks

Register-filen @ `0x20000F96`, word-index `n` = adresse `0x20000F96 + 2n`.
**Retning:** `setup` = display→MCU mål/opskrift (skrivbar via cmd 6); `trigger` = display skriver, MCU konsumerer og nulstiller; `sensor` = MCU→display status; `aktuator` = MCU-udgang (spejles til GPIO/PWM); `fejl` = fejlkode; `intern` = bruges kun internt / ikke individuelt.
**Konfidens:** høj = entydigt bevis (streng + kode); mel = stærk kode-indikation; lav = udledt/reserveret.

Defaults fra init `0x9966` (kun ikke-nul): ord 0=1, 2=1, 4=180, 5=60, 11=920, 13=60, 14=4, 15=60, 17=360, 23=360, 25=550, 114=50. `memset` @ `0x996E` dækker ord 0..127 (256 bytes); ord 128 sættes af ADC.

### 2.1 Strøm / tilstand (0..1)
| n | Navn | Retning | Bevis (adresse + uddrag) | Konf. |
|---|---|---|---|---|
| 0 | **power** (1=on, 0=off) | setup | init `0x9978` `strh r1=1,[r0]`; status `0x29D4` `ldrh [r5]`→hvis 0 start "sleep"; PROTOCOL `PowerOn/Off` cmd6 sub0 | høj |
| 1 | **sleep/drain-tilstand** (1=sleep/drain, 0=awake/normal) | setup | status `0x29DE` `ldrh [r5,#2]` cmp 1→start "drain" (`0x29E4`); PROTOCOL `MachSleep/Awaken` cmd6 sub1. *(Live 2026-11-17: v2/v3 aktive faser=0; sleep-session=1. ourui v3/v4 keep-awake=1 er en BUG (sender MCU i sleep); v5 skriver 0.)* | høj |

### 2.2 Grinder / kværn (2..8)
| n | Navn | Retning | Bevis | Konf. |
|---|---|---|---|---|
| 2 | **grind mode** (1=vægt-baseret auto) | setup | debug `0x9930` "grind: mode=%d" læser `[r4,#4]`; grind-ctl `0x1A30` cmp mode==1; init `0x997A`=1 | høj |
| 3 | **grind-start/tare-trigger** (via UART3/produkt) | trigger | produkt `0x5664` `strh r6=1,[r4,#6]`; `0x5678` hvis ord3≠0 → tare grind-vægt `0xAF90(0x200010C0)`, ord3=0 | mel |
| 4 | **grind auto_weight** (0,1 g) | setup | debug `0x992E` "auto_weight=%d" `[r4,#8]`; init `0x997E`=180; grind-ctl `0x1A2E` | høj |
| 5 | **grind over_time** (timeout) | setup | debug `0x992C` "over_tiome=%d" `[r4,#0xa]`; init `0x9982`=60; grind-ctl `0x1A46` sammenligner elapsed | høj |
| 6 | **grind tare/zero-trigger** | trigger | grind-ctl `0x19F2` `ldrh [r6,#6]`; hvis ≠0 → `0xAF90` tare grind-vægt, ord6=0 (`0x1A00`) | høj |
| 7 | **grind run/kør-flag** | trigger | grind-ctl `0x1A2A` `ldrh [r6,#0xc]`; styrer kværn-start/stop ift. vægt/timeout | mel |
| 8 | **self_inspection mode / drain-tilstand** | setup/intern | debug `0x488` "self:mode=%d" læser `[r4,#0x10]`; drain `0x30E8` læser 20×; self-insp skriver | mel |

### 2.3 Bryg / extract (9, 11, 13, 22, 23)
| n | Navn | Retning | Bevis | Konf. |
|---|---|---|---|---|
| 9 | **brew-start/tare-trigger** (via UART3/produkt) | trigger | produkt `0x5668` `strh r6=1,[r4,#0x12]`; `0x5686` hvis ord9≠0 → tare brew-vægt `0xAF90(0x200010DC)`, ord9=0 | mel |
| 10 | *(reserveret opskrift-felt)* | intern | **ingen individuel MCU-adgang** fundet (hverken læs/skriv); i display write-bånd. Del af opskrift-blok sub4/sub5 | lav |
| 11 | **bryg set_temp** (0,1 °C) | setup | debug `0x9942` "extract: set_temp=%d" `ldrsh [r4,#0x16]`; init `0x998C`=920; boiler_heat `0x3502` læser 9× | høj |
| 12 | *(reserveret opskrift-felt)* | intern | ingen individuel MCU-adgang; i write-bånd | lav |
| 13 | **bryg opskrift-param** (pre-infusion/tid; 0,1) | setup | init `0x999A`=60; brew-ctl `0x1E9C` læser; NFC-opskrift `0x1E6E/0x1E7E` skriver 440/340 | mel |
| 22 | **bryg opskrift-param** (tid/mængde) | setup | brew-ctl `0x1E9C` RW (`0x1EF2` læser word20-relateret); NFC `0x1E10` skriver | mel |
| 23 | **bryg auto_weight** (0,1 g) | setup | debug `0x993C` "auto_weight=%d" `[r4,#0x2e]`; init `0x9998`=360; brew-ctl læser | høj |

### 2.4 Steam / damp & mælk (14, 15, 18, 24, 25)
| n | Navn | Retning | Bevis | Konf. |
|---|---|---|---|---|
| 14 | **steam gear** (MCU-debug-navn; NFC skriver temp-lignende værdier) | setup | debug `0x995A` "steam: gear=%d" `[r4,#0x1c]`; init `0x999E`=4; boiler_heat læser 4×; NFC `0x1E74` skriver 460/360/320 | mel |
| 15 | **steam opskrift-param** (tid/mængde) | setup | init `0x99A0`=60; steam-ctl `0x210E` læser; NFC `0x1E52/0x1E8E` skriver 350/300 | mel |
| 18 | **steam set_temp / opskrift** | setup | steam-ctl `0x210E` RW; NFC `0x1E4C/0x1E88` skriver 870 | mel |
| 24 | **steam-switch** (ord24=1 start, 0 stop) | trigger | steam-ctl `0x210E` RW (9 skrivninger); **start-write = cmd6 sub24 len8 ord24=1** (`AA 55 06 18 08 01 00 8A 02 … CD 67`); manuel stop = sub24 len8 ord24=0; **AUTO-STOP: ord48 ≥ ord25 (mælk-temp ≥ setpoint) → nulstiller ord24 + rydder work_sta bits 5+14 (0x4020)** — ingen stop-write nødvendig. *(Statisk kode-evidens [SESSION.md §10]; live-verifikation afventer session med faktisk steam. Første gæt "steam opskrift-param/sub18 len14" var forkert: sub18 len14 = mirror-sync.)* | høj (statisk) |
| 25 | **steam milk_set_temp** (0,1 °C) | setup | debug `0x9958` "milk_set_temp=%d" `[r4,#0x32]`; init `0x99A6`=550; steam-ctl læser | høj |

### 2.5 Varmt vand / hot water (16, 17, 19, 20, 21)
| n | Navn | Retning | Bevis | Konf. |
|---|---|---|---|---|
| 16 | **hot water auto** (flag) | setup | debug `0x9950` "hot: auto=%d" `[r4,#0x20]`; hotwater-ctl `0x1B8C` RW; NFC skriver 370/320 | høj |
| 17 | **hot water weight** (0,1 g) | setup | debug `0x994E` "weight=%d" `[r4,#0x22]`; init `0x99A8`=360; hotwater-ctl læser | høj |
| 19 | **hot water sw** (start/stop) | setup/trigger | debug `0x994C` "sw=%d" `[r4,#0x26]`; hotwater-ctl `0x1B8C` RW | høj |
| 20 | **vand-mål / target-mængde** (add_water) | setup | add_water `0x22AC` `ldrh [r4,#0x28]` cmp 0; brew/steam/hotwater læser alle; flowmeter-styring | høj |
| 21 | *(reserveret opskrift-felt)* | intern | ingen individuel MCU-adgang; i write-bånd | lav |

### 2.6 Kalibrering & system (26..32)
| n | Navn | Retning | Bevis | Konf. |
|---|---|---|---|---|
| 26 | **grind_k kalibrering** (kommando/værdi) | trigger | grind-kalib `0xA4C` `ldrh [r4,#0x34]`; >10 → kalibrér grind_k (vægt×10, `0xB108`), gem config[2]; 1..6 → set gears (cmd3..9 `0x7A2`); ord26=0 | høj |
| 27 | **extract_k kalibrering** (kommando/værdi) | trigger | grind-kalib `0xA98` `ldrh [r4,#0x36]`; ≠0 → kalibrér extract_k, gem config[0], cmd2 `0x7A2`; ord27=0 | høj |
| 28 | **drain-param** (tid/tæller) | intern | drain `0x30E8` læser `[r5,#0x38]` (`0x31D4`) | lav |
| 29 | **drain-param** (tid/tæller) | intern | drain `0x30E8` læser `[r5,#0x3a]` | lav |
| 30 | **reboot-request** (skriv ≠0 → MCU resetter) | trigger | status `0x2A06` `ldrh [r5,#0x3c]`; ≠0 → ord30=0, delay, `AIRCR` reset `0x2A18` | høj |
| 31 | *(reserveret)* | intern | ingen individuel MCU-adgang; i write-bånd | lav |
| 32 | *(reserveret — første ord i status-blok)* | intern | **ingen MCU-skrivning** fundet; broadcastes (sub32) men altid 0 | lav |

### 2.7 Status-blok (33..75) — broadcastes cmd 0x83 sub32 len80
| n | Navn | Retning | Bevis | Konf. |
|---|---|---|---|---|
| 33 | **work_sta** (aktuator-bitfelt, se §2.9) | aktuator | debug `0x448` "work_sta=%x" `[r4,#0x42]`; run-task `0x27E6..` spejler bits→GPIO; alle kontrol-tasks skriver | høj |
| 34 | **E1 fejlkode** | fejl | debug `0x3DA` "E1=%x" `[r4,#0x44]`; fejl-detektor `0xA5C2` `orr #0x20` (head>110 °C); spændings-fejl `0xA94E` `orr #1` | høj |
| 35 | **E2 fejlkode** (sensor-fejl) | fejl | debug `0x3D6` "E2=%x" `[r4,#0x46]`; fejl-detektor: bit0=e-boiler NTC (`0xA4B8`), bit1=head NTC (`0xA5E4`), bit3=s-boiler NTC (`0xA57E`), bit4=mælk NTC (`0xA44E`) | høj |
| 36 | **E3 fejlkode** (overophedning) | fejl | debug `0x3D2` "E3=%x" `[r4,#0x48]`; fejl-detektor: bit4=e-boiler>120 °C (`0xA49A`), bit5=s-boiler>160 °C (`0xA4FE`); `0xB268` sætter/rydder bits | høj |
| 37 | **grinder gear** (1..30, beregnet) | sensor | grind-gear `0x181E` returnerer gear→`0x19EE`/`0x4EFC` `strh [r4,#0x4a]`; piecewise-interpolation af ord 69 mod gears-config | høj |
| 38 | **grind-vægt kalibreret** (0,1 g) | sensor | vægt-konv `0xAebc`→grind-box `0x65EA`/produkt `0x56A4` `strh [r4,#0x4c]`; tare `0xAFBE` nulstiller | høj |
| 39 | **brew-vægt kalibreret** (0,1 g) | sensor | vægt-konv→`0x56B2` `strh [r4,#0x4e]`; tare `0xAFE4` nulstiller; brew-ctl læser | høj |
| 40 | **e-boiler temp** (0,1 °C) | sensor | ADC-temp `0xA3A4` `strh [r1,#0x50]` (fra ord 129=e_boilert); fejl-detektor læser | høj |
| 41 | **e-boiler temp** (spejl af 40) | sensor | ADC-temp `0xA3A0` `strh [r1,#0x52]`; fejl-detektor `0xA458` range-check + >120 °C→E3 | høj |
| 42 | **e-head temp** (0,1 °C) | sensor | ADC-temp `0xA3AC` `strh [r1,#4]`→word42 (fra ord 128); fejl-detektor `0xA588` >110 °C→E1 | høj |
| 43 | **self_inspection sta** (tilstands-bits) | sensor/intern | debug `0x484` "self:...sta=%x" `[r4,#0x56]`; self-insp `0x2494` skriver 21× (state-maskine 0→1→3→7→0xF…) | høj |
| 44 | **input-bits** (e,s,power,c1,c2; se §2.9) | sensor | status `0x299C` `strh [r5,#0x58]` = 5 GPIO-bits (`0x2970..0x299A`) | høj |
| 45 | **flowmeter brew** (puls-tæller) | sensor | brew-ctl `0x1E9C` skriver; drain læser; add_water flowmeter | mel |
| 46 | **flowmeter steam** (puls-tæller) | sensor | steam-ctl `0x2134` `strh [r2,#0x5c]` = flowmeter PE.2 (`0x17D0`) | mel |
| 47 | **s-boiler temp** (0,1 °C) | sensor | ADC-temp `0xA3AA` `strh [r1,#0xe]`→word47 (fra ord 130=s_boilert); fejl-detektor >160 °C→E3 | høj |
| 48 | **mælk temp** (0,1 °C) | sensor | ADC-temp `0xA39A` `strh [r1,#0x60]`→word48 (fra ord 131=milk); fejl-detektor range-check→E2 bit4 | høj |
| 49 | **netspænding** (V, målt) | sensor | SPI2-ADC `0xA89E` `strh [r0,#0x62]` = ADC-reg8/3178; debug `0xF694` "vol=%d"; spændings-check `0xA8F6` | høj |
| 50 | **strøm kanal 1** (cur_ch1) | sensor | SPI2-ADC `0xA8A4` `strh [r0,#2]` = ADC-reg6/87; debug "cur_ch1=%d" | høj |
| 51 | **SPI2-ADC ch0xA — tryk-kandidat ELLER pumpe-drive-niveau** | sensor | SPI2-ADC `0xA8EE`/`0xA8AC` `strh [r1,#0x66]`; self-insp sammenligner mod 8000 (`0x26E4`); grind-box læser. **Live-evidens:** v2 [HOTWATER.md]: ~0-1 idle, ~1300 ved fri dispensering, spikes 16670 ved pumpe mod lukket ventil (flow=0). v3 [BREW.md]: to skarpe plateauer under bryg — pre-infusion **358 (σ9) i 6,06 s ≈ preInfusionTime=6 ✓**, ekstraktion **1346 (σ15) i 21,35 s**; forhold 3,76. **Kalibrering uafklaret:** anker pre=4 bar → ext≈15 bar (fejler 9-bar-sanity, men muligt for vibrationspumpe uden OPV); anker ext=9 bar → pre≈2,4 bar (under setpoint). Ekstraktions-niveau (~1346) ≈ hot-water-niveau (~1300) trods meget forskelligt modtryk → **alternativ fortolkning: pumpe-drive-niveau (PWM/fase-vinkel-feedback), ikke tryk**. Plateauer + 6 s-timing + forhold er robuste; absolut bar-skala kræver fysisk verifikation (sensor-ID på board eller blind-basket-test med manometer). | mel |
| 52 | **flow-hastighed** (flow_velocity) | sensor | `0x933C` beregner 125000/periode→`0x2950` `strh [r5,#0x68]`; debug "flow_velocity=%d" | høj |
| 53 | **grind-vægt rå** (raw/10000, g) | sensor | vægt-konv `0xAEE8` `strh [r2,#0x6a]` (ch0); UART3 cmd2 rapport læser | høj |
| 54 | **grind_k** (kalibrerings-faktor) | setup/intern | config-load `0x9C4`/save `0x80C` skriver (config[2]); UART3 læser; debug "grind_k=%d" | høj |
| 55 | **brew-vægt rå** (raw/10000, g) | sensor | vægt-konv `0xAEE2` `strh [r2,#0x6e]` (ch1); UART3 læser | høj |
| 56 | **extract_k** (kalibrerings-faktor) | setup/intern | config-load/save (config[0]); UART3 læser; debug "extract_k=%d" | høj |
| 57 | **flowmeter/puls-tæller** (PD.5) | sensor | grind-ctl `0x19E6`/produkt `0x4F0E` `strh [r4,#0x72]` = `0x17D0`(PD.5) | mel |
| 58 | **strøm kanal 2** (cur_ch2) | sensor | SPI2-ADC `0xA8B0` `strh [r0,#0x12]` = ADC-reg7/87; debug "cur_ch2=%d"; grind-box læser | høj |
| 59 | **gears_min** (kværn-kalibrering) | setup/intern | config-load/save (config[4]); debug "min=%d"; grind-gear interpolation | høj |
| 60 | **gears_5** (kværn-kalibrering) | setup/intern | config (config[6]); debug "5=%d" | høj |
| 61 | **netfrekvens** (50/60 Hz) | sensor | SPI2-ADC `0xA8AC`→`0xA880` range-check 48..52/58..62 → snap til 50/60; debug "hz=%d" | høj |
| 62 | **e-boiler varme-effekt** (PWM/power) | aktuator | boiler_heat `0x35EE/0x365E` `strh [r7,#0x7c]`; debug "extract_heat:...power=%d" | mel |
| 63 | **s-boiler varme-effekt** (PWM/power) | aktuator | boiler_heat `0x358E` `strh [fp,#0x7e]`; debug "steam_heat:...power=%d" | mel |
| 64 | **gears_25** (kværn-kalibrering) | setup/intern | config (config[8]); debug "25=%d" | høj |
| 65 | **gears_max** (kværn-kalibrering) | setup/intern | config (config[0xA]); debug "max=%d" | høj |
| 66 | **bryg-tæller/timer** | sensor/intern | brew-ctl `0x1F9E/0x2086` `strh [r6,#0x84]` (RW mange gange) | mel |
| 67 | **set_voltage** (120/220) | setup/intern | config-load/save (config[0x3C]); head_heat `0x3ACA`/PID læser; debug "set_voltage=%d" | høj |
| 68 | **s-vandniveau** (ADC) | sensor | `0xB306` (EADC ch2)→`0x2930` `strh [r5,#0x88]`; debug "%s:adc=%d" (e_water/s_water) | mel |
| 69 | **grinder position ADC** (rå) | sensor | grind-gear `0x1836` `strh [r1,#0x8a]` = EADC-raw konverteret (`0xFB0`); UART3 læser | høj |
| 70 | **e-vandniveau** (ADC) | sensor | `0xB2DE` (EADC ch1)→`0x2928` `strh [r5,#0x8c]`; >2000→flag | mel |
| 71 | **Mboard temp** (I2C-sensor 0x48) | sensor | `0xA166` `0xA11E(0x48)`→`strh [r5,#0x8e]`; debug "temp:...Mboard=%d" | høj |
| 72 | **Dboard temp** (I2C-sensor 0x4E) | sensor | `0xA18E` `0xA11E(0x4E)`→`strh [r5,#0x90]`; debug "Dboard=%d" | høj |
| 73 | **self_inspection result** (flag-bits) | sensor/intern | debug `0x480` "result=%x" `[r4,#0x92]`; self-insp `0x24A6` init=0xFFFF, sætter/rydder bits | høj |
| 74 | **gears_fu4** (kværn-kalibrering) | setup/intern | config (config[0x40]); debug "-4=%d" | høj |
| 75 | **gears_40** (kværn-kalibrering) | setup/intern | config (config[0x42]); debug "40=%d" | høj |

### 2.8 Identitet / serienummer & øvrige (76..128)
| n | Navn | Retning | Bevis | Konf. |
|---|---|---|---|---|
| 76..79 | *(reserveret)* | intern | ingen MCU-skrivning/læsning fundet (mellem gears-config og UID-blok) | lav |
| 80..87 | **UID-blok A** (16 bytes) | intern | init `0x99B0` `memcpy(table+0xA0, 0x20000000, 16)` | høj |
| 88..95 | **UID-blok B** (16 bytes, kopi) | intern | init `0x99BC` `memcpy(table+0xB0, 0x20000000, 16)` | høj |
| 96..111 | **serienummer/produkt-blok** (32 bytes ASCII) | intern | config-load `0x9F4` `memcpy(table+0xC0, config+0x1C, 32)`; default "B000000-0000-0000" (`0xCC8`); chip-info `0x8B6` printer "\nSerial:%s" fra table+0xC0; cmd9/OTA-version parser ord 96/98 (`0x9E2A`, `0xB490`) | høj |
| 97,99..111 | *(del af 32-byte serienummer-blok)* | intern | kun bulk-kopieret (se 96..111); ingen individuel adgang | høj |
| 112 | **pro_sw** (produkt-switch; hi-byte bruges) | setup | debug `0x9936` "pro_sw=%d" `ldrb [r4,#0xe1]`; NFC-opskrift `0x1E14` læser hi-byte, `0x1E36` læser lo-byte (variant 3/4) | høj |
| 113 | **bryg/produkt-switch** | setup | NFC-opskrift `0x1E1A/0x1E1E` kopierer hi→lo-byte; brew-ctl `0x1FD6` `ldrb [r6,#0xe2]` | mel |
| 114 | **head_set_temp** (bryghoved-setpoint, 0,1 °C) | setup | debug `0x993E` "head_set_temp=%d" `[r4,#0xe4]`; init `0x9990`=50; head_heat `0x39D2/0x3A72` læser 7× | høj |
| 115..127 | *(reserveret / interne)* | intern | ingen individuel MCU-adgang fundet; mellem head-setpoint og temp-array | lav |
| 128 | **e-head temp** (0,1 °C, intern array[0]) | sensor | ADC-temp `0xA396` `strh [r4]` (r4=`0x20001096`=word128); head_heat feedback `0x39E2/0x3AA0`; debug "e_head=%d" | høj |

> **Bemærk:** ord 129/130/131 (lige efter tabellen, `0x20001098..`) = internt temperatur-array `[e_boilert, s_boilert, milk]` skrevet af ADC-temp `0xA362/0xA370/0xA382`. De er **uden for** de 129 protokol-ord, men kopieres til ord 40/41/47/48.

### 2.9 Aktuator- og input-bitkort (bevis: run-task `0x2782`, status-task `0x2970`)

**Ord 33 `work_sta` → GPIO** (run-task spejler hver bit til en relæ-pin):
| Bit | Maske | GPIO | Pin |
|---|---|---|---|
| 0 | 0x0001 | `0x40004930` | PE.12 |
| 1 | 0x0002 | `0x400048F4` | PD.13 |
| 2 | 0x0004 | `0x400048EC` | PD.11 |
| 3 | 0x0008 | `0x400048E8` | PD.10 |
| 4 | 0x0010 | `0x400048F0` | PD.12 |
| 5 | 0x0020 | `0x40004918` | PE.6 |
| 6 | 0x0040 | `0x40004818` | PA.6 (kværn) |
| 9 | 0x0200 | `0x40004884` | PC.1 |
| 10 | 0x0400 | `0x4000488C` | PC.3 |
| 11 | 0x0800 | `0x40004888` | PC.2 |
| 13 | 0x2000 | `0x4000481C` | PA.7 |

Kendte bit-tildelinger fra kontrol-tasks: bit6=grind aktiv (`0x5006`), bit8=grind kører (`0x1A22`), bit1+9=bryg (`0x505C` `orr #0x202`), bit4+9=varmt vand (`0x238C` `orr #0x210`), bit1+3=steam (`0x479E` rydder `#0x210`), bit0=OTA aktiv (`0x276E`).

> **Live-korrektion 2026-11-16 (`HOTWATER.md`):** Under faktisk Hot Water-dispensering sættes **bits 1+3+9 (`0x20A`)** — *ikke* bit4+9. **Bit 4 blev aldrig sat** i nogen frame (6305 frames, to komplette hændelser). "bit0=OTA" og "bit8=grind kører" er desuden **altid sat i idle** → de statiske betegnelser afvises som runtime-semantik. `0x20A` er præcis den maske UART3-cmd 7 rydder (se §4) → **cmd 7 er formodentlig hot water** (opgraderet fra lav konfidens); cmd 8's "bits 4,9 = hotwater"-kobling er dermed også usikker.

> **Parity-spec-korrektioner 2026-11-17 (`PARITY.md`):** (a) **ord 40** er også den viste "hot water-temp" i original-UI — **read-only sensor**, ingen temp-indstilling for vand findes. (b) **Word 112/113 byte-pakkede:** 112=(preInfSwitch<<8)|tryk, 113=(preInfTime<<8)|breakTime, 114=headTemper (hele °C, MCU ×10). (c) **breakTime-quirk:** opskrift-funktion `0x1E10` håndterer kun tryk 3/4, overskriver word13 og erstatter breakTime med preInfTime → separat breakTime uden effekt i standard-bryg. (d) **Sikkerhed:** trigger-ord **26/27/30 = 0** i normale blokke; **word30=1 rebootter MCU** (`0x2A06`). (e) brewMode vestigial; sleepMode/sleepModeSetting uden UI-editor. (f) boot-push: sub1 len30 + sub112 len3 ved app-opstart.

> **STEAMLIVE-korrektioner 2026-11-17 (`STEAMLIVE.md`, live-bevist):** **To steam-veje med latch-gating:** (1) UI-start = cmd6 sub24 len8 ord24=1 → MCU latch'er og **auto-stoppper ved ord48 ≥ ord25** (rydder bits 5+14 + nulstiller ord24 selv; ingen stop-write); (2) **fysisk wand/flush** = PE.2-kontakt → ord44 bit0=1 + ord46 (flowmeter steam) tæller → MCU sætter `work_sta |= 0x4020` **UDEN ord24=1** og **auto-stoppper IKKE** selv ved >650 (momentary; stopper ved wand-release). Flush sender **ingen UART-frames**. Samtidige faser: brew+steam = work_sta **0x5327** (bits 1+2+9+12 ∪ 5+14); bryg-vægt/flow uforstyrret. Mælk-ramp 1,40 °C/s; overshoot +3,9 °C (~11 s termisk inertion efter stop).

**Ord 44 input-bits ← GPIO** (status-task `0x2970..0x299C`; navne fra debug `0x5EF` "In:bean,e,s,power,c1,c2"):
| Bit | GPIO | Navn (debug) |
|---|---|---|
| 0 | PE.2 (`0x40004908`) | s (steam) |
| 1 | PA.15 (`0x4000483C`) | e (extract) |
| 2 | PC.8 (`0x400048A0`) | power |
| 3 | PE.7 (`0x4000491C`) | c2 |
| 4 | PC.15 (`0x400048BC`) | c1 |
| — | PD.5 (`0x400048D4`) | bean (ikke i ord 44; læses separat som flowmeter/kværn-sensor) |

---

## 3. SKRIVE-HANDLER (cmd 6) — semantik

**Dispatch** (`0x9C54`, i RX-parser `0x9AAC`):
```
9c54: ldrb r1,[r3,#3]      ; sub
9c56: ldrb r2,[r3,#4]      ; len (ord)
9c58: adds r0,r1,r2
9c5a: cmp  r0,#0x21        ; sub+len < 33 ?
9c5c: bhs  9c68
9c5e: adds r3,r4,#5        ; payload
9c62: bl   99c6            ; normal write + ACK (ord 0..31)
9c68: subs r0,#0x71        ; (sub+len)-113
9c6a: cmp  r0,#3           ; sub+len ∈ {113,114,115} ?
9c6c: bhs  9c80            ; ellers fejl "06 error,addr=%d,num=%d" (0x9DA0)
9c6e: bl   99c6            ; udvidet write (ord 112..114)
9c76: ldrb r0,[r7,#1]      ; [0x20000199]
9c78: cmp  r0,#2           ; hvis ==2 → nulstil flag
```
* **Tilladte skrive-bånd:** `sub+len < 33` (ord **0..31**) ELLER `sub+len ∈ {113,114,115}` (ord **112..114**, head-temp/produkt-setpoints). Alt andet afvises med fejl-log `0x9DA0` ("06 error,addr=%d,num=%d").
* **Selve write (`0x99C6`):** `memcpy(&table[sub], payload, 2·len)` (`0x99F6`→`0xDDA0`), derefter bygges ACK-frame `AA 55 86 sub len <echo af ordene> CRC` og sendes (`0x88B2`). **Ingen værdi-range-checks, ingen limits** — alle 16-bit-værdier accepteres.
* **ACK-undertrykkelse:** hvis `[0x20000198]` ≠ 0 dekrementeres den og ACK springes over (`0x9A1C..0x9A28`).
* **Side-effekter:** write'en driver **ikke** aktuatoren direkte. Den sætter kun mål/trigger-ord i register-filen. Kontrol-tasks reagerer asynkront:
  * *Mål-værdier* (temp, vægt, gear): læses løbende af PID/kontrol-tasks.
  * *Trigger-ord* (konsumeres + nulstilles): ord 6/3 (grind-tare), ord 9 (brew-tare), ord 26/27 (kalibrering), ord 30 (reboot).
  * *Opskrift-blokke* (sub 4 len 21/22, sub 5 len 18): gemmes; NFC/produkt-profil `0x1E10` kan overskrive dem.

---

## 4. UART3-PROTOKOL — binær `AA BB` (IKKE ASCII!)

**Korrektion til PROTOCOL.md §1/§10:** UART3 (`0x40073000`, IRQ49, RX-ringbuffer `0x200002EC`) kører en **binær protokol**, ikke `@C/@R/@W`. "ASCII-parser @ ~0x2BC" var en fejl-læsning af Thumb-instruktionsbytes (`40 57`=`@W`, `40 43`=`@C`, `40 52`=`@R` er tilfældige bytes i kode @ `0x789B/0x82A5/0xBD71`). Den rigtige parser er **`0x43CC`** (kaldes af product-task `0x4F16`).

**Frame-format (begge retninger):**
```
AA BB CMD LEN PAYLOAD(LEN bytes) CRClo CRChi      (total LEN+7)
```
* `[0..1]` = header **`AA BB`** (state-maskine venter på `0xAA`→`0xBB`, `0x440C/0x4422`).
* `[2]` = **CMD** (1..20 valides @ `0x4438`: `cmd-1 < 20`).
* `[3]` = **LEN** = payload-længde i **bytes** (1..21, `0x4450`).
* `[4..]` = payload; **16-bit-ord er BIG-ENDIAN** (`rev16` @ `0x45B4/0x46A2/0x47D8`).
* CRC-16/MODBUS over `[0..3+LEN]`, **little-endian** på wire (`0x44D6`→`0x98E8`, `rev16` af modtaget CRC @ `0x44E4`).
* Parser-state @ `[0x200000BC+7]` (0..6), frame-buffer `0x2000088C`, "frame klar"-flag `[0x200000BC+6]=0x11` (`0x44EC`).

**Kommandoer (TBB @ `0x44FC`, 22 cases):**
| CMD | Handler | Funktion |
|---|---|---|
| 1 | `0x4516` | **identitet/echo**: skriv byte til sub-device, svar via `0x4398`; identitets-rapport `0x3C50` (UID `0x20000000`, display-version `0x20000F82`, MCU-UID `0x20000288`, serienummer word96) |
| 2 | `0x4540` | **kalibrerings-/status-rapport** `0x3CD8`: læser ord 37,38,39,49,53,54,55,56,59,60,64,65,69,74,75 + set_voltage |
| 3 | `0x45AC` | sæt grind-parameter: BE-word payload → `[0x200000BC+0x14]` (bliver ord 27 via produkt-task) |
| 4 | `0x45BA` | sæt byte-param → `0x3E02` |
| 5 | `0x45C6` | **grind start/stop**: payload[4]→state-byte 5; =0 → ord33 &= ~0x40 (stop kværn, bit6); svar-builder `0x3E6C` (resp CMD5) |
| 6 | `0x4616` | **brew/extract start/stop**: payload[4]→state-byte 6; =0 → ord33 &= 0xFDF9 (stop, bits 1,2,9); bytes 5,6,7,8 ×10 + BE-words 9,11 ×10 → mål; svar `0x3ED4` (resp CMD6, hale `0x4704`) |
| 7 | `0x470A` | **kontrol-mode** (rydder ord33 bits 1,3,9 = 0x20A; state-byte 7; svar `0x3F9C` resp CMD7). *Live-opgradering [HOTWATER.md]: 0x20A = præcis den maske der sættes under Hot Water-dispensering → **formodentlig hot water** (mel konf.)* |
| 8 | `0x476E` | **hot water start/stop**: payload[4]→state-byte 8; =0 → ord33 &= ~0x210 (stop, bits 4,9 = add_water-bits); bytes/words ×10 → mål; svar `0x4004` (resp CMD8). *Live-note: bit4 ses aldrig sat under faktisk hot water → denne kobling er usikker; cmd7 (0x20A) er den bedre kandidat.* |
| 9 | `0x481A` | **steam/damp start/stop**: payload[4]→state-byte 9; =0 → ord33=1 (standby); 6 BE-words ×10 → mål; læser GPIO PE.2; svar `0x40C4` (resp CMD9) |
| 16 | `0x48F4` | kontrol (state-byte 0xA) |
| 17 | `0x49B0` | kontrol |
| 18 | `0x49D6` | kontrol |
| 19 | `0x4A00` | kontrol |
| 20 | `0x4AAA` | kontrol |
| 10..15 | `0x4536` | default (ingen handling) |

> **Mode-identifikation:** svar-templates @ `0xF158`-region (`0xF1A0`=CMD5, `0xF1B0`=CMD6, `0xF1CC`=CMD7, `0xF1DC`=CMD8, `0xF1EC`=CMD9) ekko'er request-CMD. Koblingen cmd→mode er fastlagt via (a) hvilke `work_sta`-bits der ryddes ved stop, og (b) state-bytes `[0x2000090B..0F]`. cmd8=hot water er entydig (rydder bits 4,9 = præcis add_water/hotwater-bits `0x238C`); cmd5=grind (bit6), cmd6=brew (bits 1,2,9) er entydige. cmd7/cmd9 er mindre entydige (se ovenfor).

* **×10-konvertering:** payload-værdier er i 0,1-enheder (g/°C) og ganges med 10 til interne enheder (`0x466A..0x46C2`: `add r1,r1,r1,lsl #2; lsls r1,#1` = ×10).
* **Responses:** bygges fra templates @ `0xF158` (+offsets) og sendes via UART3 (`0x88B2`). ACK-builder `0x4398`.
* **Fejlhåndtering:** CRC-fejl → frame forkastes (state reset `0x44FA`/`0x4AD2`); ugyldig CMD/LEN → reset. Ingen eksplicit NACK-frame.
* **Fortolkning:** UART3-modulet er en **ekstern vægt-/kontrol-enhed** (eller IoT/fjernbetjenings-modul) der kan starte/stoppe grind/brew/steam/hotwater med vægt-mål, læse kalibrering + identitet. Det er **ikke** display-linket (UART1).

---

## 5. MCU-OTA (header `CC DD`) — statemaskine

**To faser:** (a) hoved-RX-parser (`0x9AAC`, status-task) håndterer **første cmd 1-request** og starter "updata"-task; (b) "updata"-task (`0x2768`→`0xBAA8`→`0xB646`) kører sin **egen `CC DD`-parser** og håndterer cmd 1 (re-request) + cmd 2 (data). Begge læser UART1-ringbuffer `0x200002D8`; "updata" har højere prioritet (5) end "status" (8) og kører en tæt loop (`0x2778`: `0xBAA8`+WDT-reset `0x158`) der konsumerer OTA-bytes først. *Bemærk:* hoved-parseren afviser `CC DD` cmd 2 (`0x9EBA` kræver cmd 1), så arbitration mellem de to parsere er scheduling-afhængig — den præcise låsning er ikke fuldt kortlagt, men "updata"-tasks dedikerede parser er den der skriver staging.

**Frame-format:** `CC DD CMD LENlo LENhi PAYLOAD(LEN bytes) CRClo CRChi` (LEN = 16-bit byte-antal, LE). CRC-16/MODBUS.

### 5.1 cmd 1 — OTA-request (LEN=15)
`CC DD 01 0F 00 <size:u32 LE> <check:u32 LE> <version:7 bytes> CRC`
* Hoved-parser `0x9F44`: parser size→`[0x20001238]`, check→`[0x2000123C]`, version→`[0x2000122F]`; kalder **`0xB470`** (validering).
* **`0xB470` validering:**
  * Flash-størrelse `[0x20000018]` skal være `0x80` (128 K) eller `0x100` (256 K), ellers fejl.
  * Parser serienummer word96 (byte0='B'→offset 5, byte2 bogstav/tal) → versions-tal skal være ≥ 26 (`0xB4C6`), ellers fejl.
  * Læser boot-version fra flash `0x100DCC` (`0x911E`), sammenligner med **"V120"** (`0x30323156`, `0xB4F0`); fejl → "V120boot version error" (`0xB858`).
  * Size-check: 128 K → size ≤ `0xF800`; 256 K → size ≤ `0x18000` (`0xB500/0xB50C`), ellers "ota_e3/e4,size" (`0xB874/0xB88C`).
  * Sammenligner version med forventet (`0xB51C..`); returnerer **status `0x81`=accept** (`0xB526`).
* **Flash-sletning** ( staging, `0x9FC0`/`0xB7DA`):
  * 128 K: slet `0xF800` (len `0x8800`), `0x18800` (len `0x1000`), `0x1A000` (len `0x6000`). Staging-start `[0x20001240]=0xF800`.
  * 256 K: slet `0x20000` (len `0x18000`). Staging-start `0x20000`.
  * (`0x8EC8` = FMC page-erase, 2 KB-sider.)
* **ACK cmd 1** (`0xB5A2`): `CC DD 01 09 00 <size:u32 LE> <check:u32 LE> <status:u8> CRC`. `status=0x81`=accept.
* **Start "updata"-task** (`0xA01A`→`0x2A40("updata")`); task sætter ord33 bit0 (`0x276E`).

### 5.2 cmd 2 — OTA-data (LEN=n+2, n≤256)
`CC DD 02 <LENlo> <LENhi> <seq:u16 LE> <data[n]> CRC`
* Parser i "updata"-task **`0xB646`** (state-maskine `[0x200001BC+1]`, 0..7): venter `0xCC`→`0xDD`→CMD(1/2)→LENlo→LENhi→payload→CRClo→CRChi; CRC-check (`0x98E8`, `rev16`); dispatch cmd2→**`0xB90C`**.
* **`0xB90C` data-handler:**
  * **seq-check:** modtaget seq (`[r7+8]`) vs forventet seq (`[0x20001236]`). Mismatch → status 0, log "rx id error,0x%x 0x%x" (`0xBB54`), ACK med forventet seq (display retransmitter).
  * **Længde:** total byte-count < `0x108` (264) → status **2** (sidste/partiel pakke); ellers status **1** (fuld 256-byte pakke).
  * **FMC-write** (`0x9008`): dest = staging-pointer `[0x20001240]`, src = data (`0x2000112A`), len = LEN16−2. Write-fejl → status 0 ("addr=%x,write=%x,check_write=%x", `0x91FC`).
  * **Ved succes** (`0xB96E`): forventet seq++; staging-pointer += len; **fragmenteret staging-hop:** pointer==`0x18000`→`0x18800`, ==`0x19800`→`0x1A000` (`0xB990..0xB9A4`); akkumuleret += len; **pct = akkumuleret·100/size** (cappet 99 indtil færdig, `0xB9C4`).
  * **Når akkumuleret == size** (`0xB9CC`): **checksum-verifikation** — byte-sum af staging-fragmenter via `0xB414`, sammenlignes med modtaget check (`[0x2000123C]`). Match → status 1, pct=100; mismatch → "ota check sum error,cal=0x%x rx=0x%x" (`0xBB8C`).
  * **ACK cmd 2** (`0xB604`): `CC DD 02 04 00 <seq:u16 LE> <status:u8> <pct:u8> CRC`.

### 5.3 Fuldførelse + reboot (`0xBAA8`)
* Når pct==100 (`0xBAB4`): send sidste cmd2-ACK (seq−1, status 1).
* **Skriv OTA-metadata** til flash `0x19800` (slet `0xC` bytes først): **magic `0x87654321`** @ `0x19800`, **size** @ `0x19804`, **check** @ `0x19808` (`0xBAD6..0xBAEA`). Log "ota:addr=%x,size=%d,check=%x" (`0xBBFC`).
* Lås REGWPCTL op (`0xBB34`), enable FMC ISP (`FMC+0 |= 2`, `0xBAFE`), delay 1000 ms, **system-reset** `SCB->AIRCR=0x05FA0004` (`0xBB1E..0xBB2A`).
* **Ved reboot:** bootloader kopierer staging→app (magic-baseret). **Bemærk:** selve staging→app-kopien er **ikke i denne app-binær** (ingen læsning af `0xF800`/magic fundet i app-koden) — den udføres af bootloader (LDROM/ boot-region). App'en har en separat boot-flag-check @ `0x18C` der læser `0x19800` og sammenligner med **`0x12345678`** (andet magic, "ota_flag=%x,size=%d,check=0x%x" `0x218`) — dette er en separat handshake, ikke OTA-kopien.
* **Ved interruption:** staging er fragmenteret og sekvens-nummereret; en afbrudt OTA efterlader ufuldstændig staging + intet magic @ `0x19800` → bootloader starter den eksisterende app. Display retransmitter pr. pakke (seq-ACK). Ingen rollback-mekanisme i app-koden.

---

## 6. Appendiks — navngivende strenge (ordbog til registerkortet)

Fuldt udtræk: `artifacts/mcu_analysis/strings.json` (230 strenge) og `strings_appendix.json` (159 filtrerede). Nøglestrenge med adresse + xref:

| Adresse | Streng | Tilknyttet register/funktion |
|---|---|---|
| `0x5D7` | `E1=%x,E2=%x,E3=%x` | ord 34/35/36 (fejl) |
| `0x5EF` | `In:bean=%d,e=%d,s=%d,power=%d,c1=%d,c2=%d` | input-bits (ord 44 + PD.5) |
| `0x61F` | `work_sta=%x,e_w_flag=%d,s_w_flag=%d,T_Coun=%d` | ord 33 + vandniveau-flags |
| `0x657` | `self:mode=%d,sta=%x,result=%x,%x,%d` | ord 8/43/73/33/51 |
| `0x9CEB` | `grind: mode=%d,auto_weight=%d,over_tiome=%d` | ord 2/4/5 |
| `0x9D20` | `hot: auto=%d,weight=%d,sw=%d` | ord 16/17/19 |
| `0x9D44` | `steam: gear=%d,milk_set_temp=%d` | ord 14/25 |
| `0xF608` | `extract: set_temp=%d,head_set_temp=%d,auto_weight=%d,pro_sw=%d` | ord 11/114/23/112 |
| `0xF64C` | `temp:milk=%d,s_boilert=%d,e_boilert=%d,e_head=%d,Mboard=%d,Dboard=%d` | ord 131/130/129/128/71/72 |
| `0xA530` | `adc:milk=%d,s_boilert=%d,e_boilert=%d,e_head=%d` | rå ADC (0x20000E48) |
| `0xF694` | `vol=%d,hz=%d,cur_ch1=%d,power_p1=%d,cur_ch2=%d,power_p2=%d,work_sta=0x%x` | ord 49/61/50/58/33 |
| `0xF6E0` | `%s: adc=%d,fil_adc=%d,offer_adc=%d,weight=%d,offer_weight=%d,G_k=%d,...` | vægt/kalibrering (grind/extract) |
| `0xF4DC` | `add_water:step=%d,time=%d,old_time=%d,work_sta=%x,flowmeter=%d,%d,%d` | ord 20/33 + flowmeter |
| `0xF524` | `extract_heat:temp=%d,set_temp=%d,power=%d,work_sta=0x%x,...flow_velocity=%d,...` | ord 11/62/33/52 |
| `0xF598` | `steam_heat:temp=%d,set_temp=%d,power=%d,work=0x%x,...` | ord 25/63/33 |
| `0xAD8/0xAEC/0xB07` | `set.voltage/extract_k/grind_k = %d` | ord 67/56/54 (config) |
| `0xB1C/0xB34/0xB44/0xB58/0xB70/0xB88` | `set.gears_min/5/25/max/fu4/40 = %d` | ord 59/60/64/65/74/75 |
| `0xC28..0xCC0` | `APP Param / flag / set_voltage / extract_k / grind_k / grind: min/5/25/max / CRC16 / -4 / 40` | config-dump (chip-info `0x868`) |
| `0xC18` | `\nSerial:%s` | serienummer word96..111 |
| `0xCC8` | `B000000-0000-0000` | default serienummer |
| `0xAE1B/0xAE38` | `grind weight config %x / extract weight config %x` | vægt-kalibrering |
| `0xB1BF/0xB1D8` | `Extract calibration. / Grind calibration.` | tare/kalibrering (`0xAebc`) |
| `0xB398` | `%s:adc=%d,adc_fil=%d,flag=%d` | vandniveau (e_water/s_water) |
| `0x9D83/0x9DA0` | `03 error,addr=%d,num=%d / 06 error,...` | read/write range-fejl |
| `0xA047` | `new time: %d.%d.%d-%d:%d:%d %d.%d:%d` | cmd 8 tid-sync |
| `0xB844/0xB858` | `boot version:%s / V120boot version error` | OTA boot-version "V120" |
| `0xB874/0xB88C` | `ota_e3,size=0x%x / ota_e4,size=0x%x` | OTA size-fejl |
| `0xBB54/0xBB6F/0xBB8C/0xBBB4` | `rx id error / update percent / ota check sum error / ota crc err` | OTA seq/pct/checksum/CRC |
| `0xBBFC` | `ota:addr=%x,size=%d,check=%x` | OTA metadata @0x19800 |
| `0x218` | `ota_flag=%x,size=%d,check=0x%x` | boot-flag check @0x18C |
| `0x91D0/0x91EC/0x91FC` | `erase failure / erase success / addr=%x,write=%x,check_write=%x` | FMC-flash |
| `0xBE8` | `Flash = %dk,Sram = 32k` | chip-info |
| `0xBB7` | `MEM03` | board-navn |
| `0xCD94` | `Sep 23 2026` | build-dato |

---

## 7. Korrektioner til PROTOCOL.md

1. **UART3-protokol (§1, §10):** PROTOCOL angav "`@C`/`@R`/`@W` ASCII-parser @ ~0x2BC". **Forkert.** UART3 kører binær `AA BB CMD LEN payload CRC16` (parser `0x43CC`, payload-ord big-endian). "@C/@R/@W" var Thumb-instruktionsbytes forvekslet med ASCII. "formodentligt vægt/scale" bekræftes delvist: det er et vægt-/kontrol-modul med grind/brew/steam/hotwater-styring + kalibrering + identitet.
2. **Ord 112..114 (§5.1):** PROTOCOL angav "OTA-styring". **Forkert.** De er **head-temp/produkt-setpoints** (ord 114=`head_set_temp` læst af head_heat, ord 112=`pro_sw`, ord 113=bryg-switch). cmd-6 special-casen `sub+len∈{113,114,115}` er det **udvidede skrive-bånd** for disse, ikke OTA. OTA kører udelukkende via `CC DD` (§5).
3. **Ord 14/25 (§5.1):** PROTOCOL gættede "pre-infusion tryk" (ord 14) og "steam target" (ord 25). MCU'ens egen debug (`0x9920`) kalder ord 14 **"steam gear"** og ord 25 **"milk_set_temp"**. Ord 14 er dog multiplexet (NFC-opskrift `0x1E10` skriver temp-lignende værdier).
4. **Register-file størrelse:** 129 protokol-ord (0..128), men MCU bruger **interne ord 128..131** som temperatur-array (ord 128=e_head, 129=e_boiler, 130=s_boiler, 131=mælk) — ud over de 129.

---

## 8. Verifikations-status & artefakter

| Lag | Metode | Sikkerhed |
|---|---|---|
| Register-base + 129 ord | literal-refs + memset/init `0x9966` | **høj** |
| Ord 0..75, 96..114, 128 semantik | konstant-propagation + strenge + task-roller | **høj/mel** (se pr.-ord) |
| Ord 10/12/21/31/32/76..79/115..127 | ingen individuel adgang fundet → reserveret/intern | **lav** (negativt bevis) |
| cmd-6 write-semantik | fuld disassembly `0x99C6` + dispatch `0x9C54` | **høj** |
| UART3 `AA BB`-protokol | parser `0x43CC` + TBB + handlers | **høj** |
| OTA `CC DD` statemaskine | `0xB470/0xB646/0xB90C/0xBAA8` + FMC | **høj** |
| OTA staging→app-kopi | ikke i app-binær → bootloader (LDROM) | **mel** (kan ikke ses direkte) |
| Aktuator GPIO-map (ord 33) | run-task `0x2782` | **høj** |

**Mellemartefakter (i `artifacts/mcu_analysis/`):**
* `disasm.py` / `disasm.pkl` — rekursiv disassembly (25.117 instruktioner, 19.855 reachable, 21 TBB/TBH-tabeller, literal-pool + MOVW/MOVT).
* `regrefs.py` / `regrefs.pkl` / `byidx.json` — konstant-propagation, 1.763 register-adgange, 78 indeks opløst.
* `regrefs2.py` / `regrefs2.pkl` — aggressiv union-scan (verifikation; falske positiver fra writeback-baser noteret).
* `dump.py` — annoteret disassembly (`python dump.py <start_hex> <slut_hex>`).
* `register_evidence.txt` — pr.-indeks adgangs-opsummering.
* `strings.json` / `strings_appendix.json` — alle strenge + xrefs.
* `fn_*.txt`, `rx_parser.txt`, `product_task.txt`, `status_task.txt`, `tasks_init.txt`, `cal_writers.txt` — funktions-dumps.

**Reproduktion:**
```bash
cd artifacts/mcu_analysis
../../.venv/bin/python disasm.py        # bygger disasm.pkl
../../.venv/bin/python regrefs.py       # bygger byidx.json
../../.venv/bin/python dump.py 9966 9a3a  # register-init + write-handler
```

*— research sub-agent, 2026-11-16. Statisk analyse; ingen enheder berørt. Alle adresser verificerbare i `MEM03_V514_260923.bin` (MD5 e96155dad2443063acb9ecea959854a3).*