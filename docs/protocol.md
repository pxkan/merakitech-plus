# PROTOCOL — MerakiTech Gen 1: UART-protokol display ↔ powerMcu + chip-ID

> **Note (offentlig udgave):** Dette dokument er udviklingsnoter fra reverse engineering af en MerakiTech Gen 1, som forfatteren ejer. Producentens firmware, binærer og scripts, som der henvises til (fx `demo_v9`, `MEM03_V514_260923.bin`, `demo.sh`, `test.sh`), er **ikke** inkluderet i repoet. Adresser og korte uddrag er medtaget alene for interoperabilitet. IP-adresser og andre personlige data er fjernet. Se [README → Ansvarsfraskrivelse](../README.md).

**Dato:** 2026-11-16 · **Metode:** udelukkende STATISK reverse-engineering (binærer + datasheets/web). Ingen kontakt med enheden.
**Artefakter:** `artifacts/demo_v9` (display-app, ARMv7 ELF, not stripped, MD5 `d44c2414…`) og `artifacts/MEM03_V514_260923.bin` (powerMcu fw 5.1.4, raw Cortex-M, MD5 `e96155da…`).
**Værktøj:** Apple LLVM `objdump` (ARM/Thumb), `capstone` (Thumb-2 rekursiv disassembly), `pyelftools`. Alle adresser nedenfor er verificeret i binærerne.

---

## 0. Resumé (TL;DR)

* **Fysisk:** UART **57600 baud, 8N1**, ingen flow-control. Display = SigmaStar SSD202 `/dev/ttyS1`; MCU = **Nuvoton M451 UART1** på **PE.8 (TXD) / PE.9 (RXD)**.
* **Protokol:** binær register-sync med header `AA 55`, kommando-byte, sub/register-offset, længde i 16-bit-ord og **CRC-16/MODBUS**. MCU svarer med kommando `| 0x80`.
* **MCU-OTA:** separat header `CC DD`, længde i bytes, sekventielle 256-byte datapakker, flash-staging + reboot.
* **Chip:** **Nuvoton NuMicro M451** (ARM **Cortex-M4F @ 72 MHz**, 32 KB SRAM, 128/256 KB flash). **Ikke** M031/M032 (de er M0/M0+ uden FPU/Thumb-2), **ikke** GD32/HC32. Beviskæden i §8 er entydig (SystemInit er byte-for-byte M451-BSP'ens, inkl. PDID-check `== 0x945`).
* **Begge sider initierer:** displayet poller med read-queries (cmd 3) hvert ~100 ms; MCU'en broadcaster status (cmd 0x83) hvert ~100–200 ms og beder om displayets version (cmd 0x89) hvert ~20 ms indtil svar. *(Live-note 2026-11-16: i to captures — idle [UARTLIVE.md] og Hot Water-aktivitet [HOTWATER.md] — blev **ingen uopfordrede 0x83-broadcasts** observeret; al status kom via displayets poll. Broadcast-koden findes i binærken, men trigger-betingelsen er ikke identificeret. Versions-håndtrykket sker kun ved boot.)*

---

## 1. Fysisk lag

| Parameter | Værdi | Kilde (display) | Kilde (MCU) |
|---|---|---|---|
| Baud rate | **57600** | `set_opt(fd, 57600, 8, 'N', 1)` @ `0x146790` | `UART_Open(UART1, 57600)` @ `0x2C2A` (`mov.w r1,#0xE100`) |
| Databits | 8 | `set_opt` CS8 | UART_Open WLS=8 |
| Paritet | None (`'N'`=78) | `set_opt` clear PARENB | UART_Open |
| Stopbits | 1 | `set_opt` clear CSTOPB | UART_Open |
| Flow-control | ingen | raw termios, IXON/RTS fra | ingen |
| Port (display) | `/dev/ttyS1`, `open(...,0x902)` = `O_RDWR\|O_NOCTTY\|O_NONBLOCK` | `open_port(1)` @ `0x144D60` | — |
| Port (MCU) | **UART1** @ `0x40071000`, IRQ37 | — | literal @ `0x2E00`, vector `[37]` |
| MCU-pins | **PE.8 = UART1_TXD, PE.9 = UART1_RXD** (MFP=1) | — | `GPE_MFPH` (SYS+0x54) `=0x11` @ `0x8944` |
| MCU-clock | HCLK **72 MHz** (PLL fra 12 MHz HXT); UART-clk fra PLL | — | `SystemCoreClock=72000000` @ `0x748` |
| Læse-mode | non-blocking (`VMIN=0,VTIME=0`), poll-loop | `set_opt` | RX-FIFO + IRQ → ringbuffer |

**Baud-verifikation på begge sider:** displayet kalder `cfsetispeed/cfsetospeed(B57600=0x1001)`; MCU'en beregner divisor i `UART_Open`/`0x86C2` ud fra UART-clock (PLL) og `CLK_CLKDIV0`. Begge ender på 57600. Der er **ingen** 921600/115200 i selve linket (strengen "921600" i demo_v9 tilhører en anden UART-kontekst).

> MCU'ens anden UART, **UART3** (`0x40073000`, IRQ49), kører et *separat binært* `AA BB CMD LEN payload + CRC16`-protokol (payload-ord **big-endian**, parser @ `0x43CC`, kaldes af product-task `0x4F16`) — et vægt-/kontrol-modul med grind/brew/steam/hotwater-start/stop + kalibrering + identitet. Den er **ikke** display-linket. *(Korrektion 2026-11-16: oprindeligt angivet som `@C`/`@R`/`@W`-ASCII @ `0x2BC` — det var Thumb-instruktionsbytes forvekslet med ASCII. Fuld kortlægning i [REGISTERMAP.md](REGISTERMAP.md) §4.)*

---

## 2. Frame-format

### 2.1 Normal frame (begge retninger, header `AA 55`)

```
Offset:  0    1    2    3    4    5 … 5+2L-1      5+2L   6+2L
Byte:    AA   55   CMD  SUB  LEN  PAYLOAD (L ord)  CRClo  CRChi
```

* `[0..1]` = header **`AA 55`**.
* `[2]` = **CMD** (kommando-byte).
* `[3]` = **SUB** = register-word-offset / subkommando.
* `[4]` = **LEN** = payload-længde i **16-bit-ord** (little-endian words).
* `[5 … 5+2·LEN−1]` = payload = `LEN` stk. 16-bit little-endian-ord.
* `[5+2·LEN]`, `[6+2·LEN]` = **CRC-16/MODBUS**, little-endian (lo først).
* **Total længde = `2·LEN + 7` bytes.** CRC dækker bytes `[0 … 4+2·LEN]` (dvs. `2·LEN+5` bytes).

**Hex-eksempel — display tænder maskinen (cmd 6, write register 0 = 1):**
```
AA 55 06 00 01 01 00 C5 5F
│header│cmd│sub│len│word0=1│CRC=0x5FC5 (lo C5, hi 5F)
```

**Hex-eksempel — display read-query (cmd 3, sub 0, len 56 ord, ingen payload):**
```
AA 55 03 00 38 DC 06        (7 bytes; LEN=0x38=56 ord forventes i svaret)
```

### 2.2 Undtagelse: versions-frames (cmd 9 / 0x89) — LEN er *bytes*

Versions-strengene er ASCII, så her er `LEN` et **byte**-antal (ikke ord), og payload er en streng:
```
AA 55 09 LENbyte 00 <ASCII-streng, LENbyte bytes> CRClo CRChi   (total LEN+7)
```
MCU'ens faste versions-forespørgsel (cmd 0x89) ligger som konstant @ `0x9D70`:
```
AA 55 89 01 00 00 6F 81      (LEN=1 byte, sub=0, payload=0x00, CRC=0x816F)
```
Displayet svarer med cmd 9 + sin `softversion` ("1.1.1-69"). CRC over `AA 55 89 01 00 00` = **0x816F** → wire `6F 81` (verificeret mod binærken).

### 2.3 Undtagelse: power/tid-frame (cmd 8 / 0x88) — fast 15 bytes

```
AA 55 08 <10 bytes payload: u32, u32, u16> CRClo CRChi   (15 bytes)
```
Payload = displayets maskin-tid/status-felter (år/måned/dag + time/min/sek + flag). MCU'en ACK'er med cmd 0x88 og echo'er felterne (parser @ `0x9C88`, log "new time: …").

### 2.4 OTA-frame (header `CC DD`) — se §7

```
CC DD CMD LEN16lo LEN16hi PAYLOAD (LEN16 bytes) CRClo CRChi   (total LEN16+7)
```
Her er længden et **16-bit byte-antal** i `[3..4]` (ikke ord, ikke i `[4]`).

---

## 3. Checksum-algoritme (CRC-16/MODBUS)

Verificeret i **begge** binærer:

* Display: `crc16_modbus` @ `0x144798` (bitvis) og `CRC_GetModbus16` @ `0x144914` → `GetCrc_16` @ `0x144878` med tabel `g_McRctable_16` @ `0x18BFA0`. Tabellen er **identisk** med standard CRC-16/MODBUS-tabellen (alle 256 entries verificeret).
* MCU: `0x98E8` (bitvis, init `0xFFFF`, polynomium `0xA001`).

**Algoritme:**
```
crc = 0xFFFF
for hver byte b i payload (fra byte 0 til og med sidste payload-byte, FØR CRC-feltet):
    crc ^= b
    gentag 8 gange:
        hvis crc & 1:  crc = (crc >> 1) ^ 0xA001
        ellers:        crc = crc >> 1
return crc            # 16-bit, ingen final-XOR
```
* Polynomium `0x8005` reflekteret = **`0xA001`**, init **`0xFFFF`**, refin/refout = ja, xorout = 0.
* Wire-rækkefølge: **little-endian** (low-byte først, high-byte sidst).
* MCU'ens CRC-funktion returnerer internt byte-swappet (`(crc<<8)|(crc>>8)` @ `0x9916`), og RX-parseren gør `rev16` på det modtagne CRC-felt (`0x9F34`) — så **begge ender sender/læser little-endian på wire**. Displayet gemmer med `strh` (LE). Konsistent.

**Python:**
```python
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc
```

> Bemærk: OTA-requestens *firmware*-checksum (`calculateChecksum` @ `0x1474C8`) er **ikke** CRC, men en simpel **32-bit sum af alle bytes** i .bin-filen. Den sendes i cmd-1-payloaden. Selve OTA-frames bruger stadig CRC-16/MODBUS.

---

## 4. Kommandotabel

Mønster: **MCU'ens svar = display-kommando `| 0x80`**. Register-data udveksles som 16-bit-ord i en fælles register-file (§5).

### 4.1 Display → MCU

| CMD | Navn | SUB | LEN | Payload | Betydning |
|---|---|---|---|---|---|
| `0x03` | read-query | start-word | antal ord | *ingen* (7-byte frame) | Læs `LEN` register-ord fra `SUB`. MCU svarer cmd `0x83`. |
| `0x06` | write-registers | start-word | antal ord | `LEN` ord | Skriv register-ord. MCU skriver + ACK'er cmd `0x86`. Retransmit indtil ACK. |
| `0x08` | power/tid-sync | — | — | 10 bytes (u32,u32,u16) | Maskin-tid/status. MCU ACK'er cmd `0x88`. Fast 15-byte frame. |
| `0x09` | versions-svar | 0 | byte-antal | ASCII-streng | Svar på MCU'ens `0x89`-forespørgsel (displayets `softversion`). |
| `0x01`* | OTA-request | — | 15 | size,check,version | **CC DD** — se §7. |
| `0x02`* | OTA-data | — | n+2 | seq,data | **CC DD** — se §7. |

\* OTA-kommandoer bruger `CC DD`-headeren, ikke `AA 55`.

### 4.2 MCU → display

| CMD | Navn | SUB | LEN | Payload | Betydning |
|---|---|---|---|---|---|
| `0x83` (131) | register-data | start-word | antal ord | `LEN` ord | Svar på cmd-3-query **eller** uopfordret status-broadcast (sub=32, len=80). |
| `0x86` (134) | write-ACK | start-word | antal ord | echo af skrevne ord | Kvittering for cmd-6. Frigiver displayets retransmit (`_fuart_revice_mark`). |
| `0x88` (136) | power/tid-ACK | — | — | echo | Kvittering for cmd-8. |
| `0x89` (137) | versions-request | 0 | 1 | `0x00` | Fast frame `AA 55 89 01 00 00 6F 81`. Display skal svare cmd-9. |
| `0x01`* | OTA-request-ACK | — | 9 | size,check,**status** | **CC DD**. `status=0x81` = acceptér. |
| `0x02`* | OTA-data-ACK | — | 4 | seq,status,pct | **CC DD**. `status=1`=OK, `pct`=fremskridt %. |
| `0x03`* | OTA flash/verify færdig | — | — | status | **CC DD**. `buf[9]==1` = success. |
| `0x04`* | OTA reboot-notifikation | — | 2 | bar,flag | **CC DD**. `buf[6]==1`, `buf[5]`=bar. |

### 4.3 Display-side afsender-funktioner (demo_v9)

| Funktion | Adresse | Rolle |
|---|---|---|
| `open_port` | `0x144D60` | åbner `/dev/ttyS{index}` |
| `set_opt` | `0x144950` | termios: baud/paritet/stopbits/raw |
| `usart_send` | `0x146D5C` | bygger cmd-6 write-frame i `buf_tx`, sætter retransmit |
| `usart_ckeck` | `0x146E70` | bygger cmd-3 query-frame (7 bytes) |
| `PowerOn/PowerOff` | `0x146EF8/0x146F34` | cmd 6 sub 0 = 1/0 |
| `MachSleep/MachAwaken` | `0x146F70/0x146FAC` | cmd 6 sub 1 = 0/1 |
| `powerOffSend` | `0x14700C` | cmd-8 (15-byte) frame |
| `verSend` | `0x1471A0` | cmd-9 versions-svar |
| `check` | `0x146164` | **RX-parser** for `AA 55` (normal) |
| `ota_check` | `0x147AA4` | **RX-parser** for `CC DD` (OTA-ACKs) |
| `fuart_thread` | `0x14671C` | 10 ms TX/RX-loop |
| `uartRxProcess` | `0x144E20` | 60 ms LVGL-timer, UI-opdatering fra register-mirror |

---

## 5. Register-file (det fælles "register-bank")

Protokollen er en **spejlet register-file**: display og MCU holder hver en kopi af 16-bit-ord, og cmd 3/6/0x83 synkroniserer dem.

* **MCU:** register-file = "APP Param"-struct i RAM **`0x20000F96`**, **129 ord** (index 0..128). Initialiseret @ `0x9966`.
* **Display:** mirror i `machine_struct + 0xFE8` (word-offset `sub` → `machine + 0xFE8 + 2·sub`). `usart_send` læser herfra; `uartRxProcess` skriver hertil (`memcpy` src = RX-payload `0x1E0525`).
* **Validering:** MCU afviser cmd-3 hvis `sub+len ≥ 0x81` (129). For cmd-6: `sub+len < 0x21` (33) → normal write+ACK; dvs. **display kan skrive word 0..31**. `sub+len ∈ {113,114,115}` → udvidet skrive-bånd for **head-temp/produkt-setpoints** (word 112..114; special-ACK). Alt andet afvises. **Word 32..111 er MCU-ejet status** (display læser dem, men skriver dem ikke). *(Korrektion 2026-11-16: ord 112..114 er IKKE OTA-styring — OTA kører udelukkende via `CC DD`. Se REGISTERMAP.md §3.)*

### 5.1 Kendte register-ord (word-index = SUB)

Understøttet af display-kaldsteder + MCU-defaults + `config.json`. Værdier er typisk **tiendedele** (870 = 87,0 °C, 185 = 18,5 g).

| Word | Felt | Default (MCU) | config.json | Kilde |
|---|---|---|---|---|
| 0 | **power** (1=on, 0=off) | 1 | — | `PowerOn/PowerOff` (cmd6 sub0) |
| 1 | **sleep/drain** (1=sleep/drain, 0=awake) | — | sleepMode | `MachSleep/MachAwaken` (cmd6 sub1). *(Live-korrektion 2026-11-17 [SESSION.md]: værdierne var byttet i første udgave — aktive captures (v2/v3) har word1=0, sleep-capturen har word1=1 + drain-task. **ourui v3/v4's keep-awake skriver fejlagtigt 1 og sender MCU'en i sleep; v5 skal skrive 0.**)* |
| 2 | **grind mode** | 1 | grindMode | `grinderScaleUpdate` (cmd6 sub2 len2) |
| 4 | **grind auto_weight** (0,1 g) | 180 | autoGrindWeight=185 | MCU-default `[8]=180`; "grind: mode,auto_weight" |
| 5 | grind over_time | 60 | — | MCU-default `[0xA]=60` |
| 11 | **bryg-temperatur** (0,1 °C) | 920 | brewTemper=870 | MCU-default `[0x16]=920` |
| 14 | **pre-infusion tryk** (bar) | 4 | preInfusionPressure=4 | MCU-default `[0x1C]=4` |
| 17 | **bryg-vægt** (0,1 g) | 360 | autobrewWeight=360 | MCU-default `[0x22]=360` |
| 23 | (bryg-relateret) | 360 | — | MCU-default `[0x2E]=360` |
| 25 | (steam/damp-relateret) | 550 | AtuoSteamTarge=650 | MCU-default `[0x32]=550` |
| 34 | **E1 fejlkode** | — | — | display `GetE1tate`; MCU "E1=%x,E2=%x,E3=%x" |
| 35 | **E2 fejlkode** | — | — | som ovenfor |
| 36 | **E3 fejlkode** | — | — | som ovenfor |
| 112 | **pro_sw** (produkt-switch; hi-byte) | — | — | debug `0x9936` "pro_sw=%d"; NFC-opskrift læser hi/lo-byte |
| 113 | **bryg/produkt-switch** | — | — | NFC-opskrift kopierer hi→lo; cmd6 sub113 |
| 114 | **head_set_temp** (bryghoved-setpoint, 0,1 °C) | 50 | — | debug `0x993E`; init `0x9990`; `head_heat`-PID læser 7× |

**Øvrige write-kaldsteder (display):** sub 3 (len1/4), sub 4 (len1/3/21/22 = bryg-opskrift/NFC-profil), sub 5 (len18), sub 6, sub 8, sub 9 (len1/11 = varmt vand), sub 16/19 (varmt vand start/stop), sub 18 (len14 = steam), sub 20 (page_select), sub 21 (grindability), sub 23/25 (goto brew/steam), sub 24 (len8), sub 26–31. Disse er kontrol-/opskriftsfelter; den præcise semantik pr. ord kræver dybere UI-analyse, men de ligger alle i write-båndet 0..31. *(Live-verifikation [HOTWATER.md]: Hot Water-start = **én cmd-6 write `sub 9, len 11`** (ord 9..19) med word9=1 (start-trigger), word16=1, word17=400 (40,0 g mål), word19=0. ACK efter ~22-25 ms. **Ingen stop-write** — MCU'en stopper autonomt ved vægt-målet; "sub 16/19 start/stop"-gættet ses ikke i praksis.)* *(Steam-korrektion 2026-11-17 [SESSION.md]: **steam-start = cmd6 sub24 len8 med ord24=1**, manuel stop = ord24=0; MCU auto-stoppper ved ord48 ≥ ord25 og rydder work_sta **bits 5+14 (0x4020)** — ikke bit1+3 som gættet. "sub 18 len14 = steam" var forkert: sub18 len14 er mirror-sync.)*

### 5.2 Status-broadcast (MCU → display)

MCU'en sender uopfordret **cmd 0x83, SUB=32, LEN=80** (ord 32..111) — et status-blok med temperaturer, vægte, fejl og tilstandsflag. *(Live-note: ikke observeret i v1/v2-captures — se TL;DR. Indholdet er dog fuldt verificeret via poll-svar.)* Indhold (fra MCU-strings):
* `adc:milk=%d,s_boilert=%d,e_boilert=%d,e_head=%d` → mælk-, steam-boiler-, extract-boiler-, extract-head-temperaturer.
* `E1/E2/E3` (ord 34/35/36), `work_sta`, `e_w_flag`, `s_w_flag`, `T_Coun`, `In:bean,e,s,power,c1,c2`.
* Displayet læser fejl-ordene og viser fejl-UI (`uartRxProcess` → `GetE1tate(1)/(32)`).

Displayet poller desuden selv hele register-filen med cmd-3 queries: `(sub0,len56)`, `(sub33,len7)`, `(sub56,len56)` — tilsammen ord 0..111.

---

## 6. Timing / hvem initierer

| Aktør | Interval | Handling | Kilde |
|---|---|---|---|
| Display `fuart_thread` | **10 ms** (`usleep(10000)`) | læs ≤32 bytes → ringbuffer → parse; TX-arbitrering | `0x146D2C` |
| Display `uartRxProcess` | **60 ms** (LVGL-timer) | opdatér UI/mirror fra parsede frames | `lv_timer_create(...,60)` @ `0x1467F0` |
| Display read-queries | **100 ms** (hver 10. loop) | cmd-3, roterer (sub0,len56)/(sub33,len7)/(sub56,len56)/(sub33,len7) | `0x146A94` (cmp #9) + switch @ `0x146B14` |
| Display cmd-6 retransmit | 10 ms, op til **20 gange** (200 ms) | indtil cmd-0x86 ACK | retry=20 @ `0x146E30`; ACK-clear @ `0x146650` |
| MCU status-task | **10 ms** (`rt_thread_delay(10)`, tick=1 ms) | RX-parse + sensor-loop | `0x2A38` |
| MCU status-broadcast | **100 ms** (aktiv) / **200 ms** (idle) | cmd-0x83 sub32 len80 | tæller 10/20 @ `0x270E/0x2738`, send @ `0x2744` |
| MCU versions-request | **20 ms** (hver 2. loop) indtil svar | cmd-0x89 | `0x2916` (bit0) → `0x9A94` |
| MCU øvrige sensor-tasks | 40/50/100/200/300/500/1000 ms | ADC, vægt, varme | modulo-tæller @ `0x2934…0x29C8` |

**Konklusion:** *begge* sider initierer. Displayet er master for **writes** (cmd 6, med ACK/retransmit) og poller med cmd 3; MCU'en er master for **status-push** (cmd 0x83) og versions-håndtryk (cmd 0x89). Der er ingen enkelt "request/response"-master — det er et asynkront register-sync-link. *(Parity-spec 2026-11-17: **boot-push** — `fuart_thread` sender **sub1 len30 + sub112 len3 ved opstart**, så config-værdier synkes til MCU ved hver app-genstart; korrigerer WRITETEST's "ingen fremsendelse ved boot".)*

**MCU RT-Thread tick:** SysTick reload = **71999** @ `0x750`, `SystemCoreClock=72 MHz` → **1 ms tick** (`RT_TICK_PER_SECOND=1000`). `rt_thread_delay(10)` = 10 ms.

---

## 7. MCU-OTA-protokol (header `CC DD`)

### 7.1 Frame-format
```
CC DD CMD LENlo LENhi PAYLOAD (LEN bytes) CRClo CRChi      (total LEN+7)
```
`[3..4]` = 16-bit **byte**-længde (LE). CRC-16/MODBUS over `[0 … 4+LEN]`.

### 7.2 Kommandoer

**Display → MCU:**
* **cmd 1 — OTA-request** (LEN=15):
  ```
  CC DD 01 0F 00 <size:u32 LE> <check:u32 LE> <version:7 bytes> CRC
  ```
  * `size` = firmware-størrelse (63460 for 5.1.4).
  * `check` = **32-bit sum af alle bytes** i .bin (fx 5.1.4 = `0x00662BE4`).
  * `version` = "V5.1.4" (6 tegn + null = 7 bytes).
* **cmd 2 — OTA-data** (LEN=n+2, n≤256):
  ```
  CC DD 02 <LENlo> <LENhi> <seq:u16 LE> <data[n]> CRC
  ```

**MCU → display (ACKs):**
* **cmd 1-ACK** (LEN=9): `CC DD 01 09 00 <size:u32> <check:u32> <status:u8> CRC`. **`status=0x81` = acceptér** (display læser `buf[13]==0x81`).
* **cmd 2-ACK** (LEN=4): `CC DD 02 04 00 <seq:u16> <status:u8> <pct:u8> CRC`. `status=1`=OK, `pct`=fremskridt % (display læser `buf[7]==1`, `buf[8]`=pct, `buf[5..6]`=seq).
* **cmd 3 — flash/verify færdig** (display: `buf[9]==1` → success).
* **cmd 4 — reboot-notifikation** (LEN=2, display: `buf[6]==1`, `buf[5]`=bar).

### 7.3 Flow (verificeret i begge ender)
1. Display henter `MEM03_V514_*.bin` fra OTA-server, MD5-check (`mcu_local_md5_check`).
2. `star_mcu_ota` → `mcu_ota_request("/customer/mcu.bin","V5.1.4")` (`0x14792C`).
3. `request_update` (`0x147314`) bygger **CC DD 01** (size, byte-sum, version) → `mcu_ota_container_tx`.
4. MCU validerer version (`0xB470`), **sletter flash-staging**, ACK'er `0x81` (`0xB5A2`).
5. Display `send_buf` (`0x14773C`): `fread` 256 bytes → `sendData` (**CC DD 02**, seq, data). Venter på ACK pr. pakke.
6. MCU skriver til flash-staging (`0x9008` FMC-write), tracker seq/pct, ACK'er (`0xB604`).
7. Efter sidste pakke: MCU verificerer checksum, sender **cmd 3** (færdig) + **cmd 4** (reboot), resetter via `SCB->AIRCR` (`0x2A18`).
8. Display `mcu_update_status`: **2**=klar, **3**=venter, **4**=success, **5**=reboot, **6**=fejl.

### 7.4 Flash-staging-layout (MCU)
* **128 KB flash:** app `0x0…0xF800`; staging `0xF800…0x18000` + `0x18800…0x19800` + `0x1A000…0x20000` (fragmenteret, 2 KB-huller).
* **256 KB flash:** staging `0x20000…0x38000`.
* Parametre/config i FMC-config-området **`0x300000`** (ISP-write @ `0x711C`).
* MCU'en sondrer layoutet ved at læse flash-størrelsen (probe `0x20000`/`0x40000`, §8).

---

## 8. Chip-identifikation: **Nuvoton NuMicro M451 (Cortex-M4F)**

**Konklusion:** powerMcu er en **Nuvoton M451-series** MCU — ARM **Cortex-M4F @ 72 MHz**, **32 KB SRAM**, **128 KB eller 256 KB flash** (M451**xE** / M451**xG**). Præcis package/flash-variant kræver fysisk aflæsning, men **familien er entydig**. Hypotesen M031/M032/GD32/HC32 er **afvist**.

### 8.1 Beviskæde (alle adresser i `MEM03_V514_260923.bin`)

1. **Reset-handler = M451-BSP'ens `startup_M451Series.s`, byte-for-byte:**
   * Unlock `REGWPCTL` (**`0x40000100`**) med nøgler **`0x59, 0x16, 0x88`** (`0xB3B8`).
   * Skriv **`0x5AA5`** til `PORCTL` (**`0x40000024`**) — "Init POR" (`0xB3CC`).
   * Clear bit12 (`HXTSELTYP`) i `CLK_PWRCTL` (**`0x40000200`**) — "Select INV Type" (`0xB3D4`).
   * Dette er *præcis* M451-startup'en (bekræftet mod OpenNuvoton/M451BSP + offentliggjort kilde).
2. **`SystemInit` @ `0x6AB4` = M451 `system_M451Series.c`:**
   * `if ((SYS->PDID >> 12) == 0x945) M32(GCR_BASE+0x14) |= BIT7;` — "One-time POR18"-workaround (`0x6AD2…0x6AE4`). **`0x945` er M451's PDID-serienummer.**
   * `CLK->PWRCTL &= ~CLK_PWRCTL_HXTSELTYP_Msk;`
   * `SCB->CPACR |= 0xF00000` (CP10/CP11 full access) → **FPU enable** (`0x6AF8`, literal `0xE000ED88`).
3. **"FMC PID = 0x%x"** printes ved at læse **`0x40000000`** (= `SYS_PDID`) @ `0x87C`. (Strengen er en vendor-fejlbenævnelse — det er *SYS* PDID, ikke FMC. Selve FMC sidder i `0x4000C000`.)
4. **Periferie-baser matcher M451 memory-map** (fra disassembly-literaler):
   | Base | M451-periferi |
   |---|---|
   | `0x40000000` | SYS/GCR (PDID, REGWPCTL@0x100) |
   | `0x40000200` | CLK (CLKSEL1@0x214, CLKDIV0@0x220, PLLCTL@0x240) |
   | `0x40004000` | GPIO (MFP: GPA_MFPL@0x30 … GPE_MFPH@0x54) |
   | `0x4000C000` | **FMC** (flash-controller, ISP) |
   | `0x40071000` | **UART1** (display-link) |
   | `0x40073000` | UART3 (scale) |
   | `0x40051000` | TIMER2 (100 kHz control-tick) |
   | `0x40081000` | I2C1 |
5. **Vector-table: 64 IRQs**, layout = M451 (`UART1_IRQn=37`, `TMR2=34`, `RTC=6`, `GPD=19`, `GPE=20`, `I2C1=39`, `ADC00=42`, `UART3=49`). Aktive handlers i binærken matcher.
6. **Thumb-2 + FPU** → Cortex-M4/M7/M33-klasse. `MOVW`/`MOV.W` (32-bit) bruges overalt; hardfault-handler printer **"FPU active!"** (`0xBE50`). **Udelukker M031/M032** (Cortex-M0/M0+ har hverken Thumb-2 `MOVW` eller FPU/CPACR).
7. **Clock-tabel** @ `0xF2B8` = `{HXT=12 000 000, PLL=0(beregnes), LXT=32 768, HIRC=22 118 400}`. **HIRC=22,1184 MHz er M451-signatur** (M031 har 48 MHz HIRC, GD32/HC32 andre).
8. **`SystemCoreClock=72 000 000`** @ `0x748`; **SysTick reload=71999** @ `0x750` → 72 MHz, 1 ms tick. M451 max = 72 MHz. ✓
9. **SRAM:** strengen **"Flash = %dk,Sram = 32k"** (`0xBE8`); RAM-referencer op til `0x20003DE0` (SRAM0) + initial **SP=`0x200045E0`** (i SRAM1). M451 32 KB SRAM = SRAM0 `0x20000000–0x20003FFF` + SRAM1 `0x20004000–0x20007FFF`. ✓ (SP ligger i SRAM1 — stack er placeret dér af linker-scriptet; data i SRAM0.)
10. **Flash:** størrelses-probe læser `0x20000` (128 K) / `0x40000` (256 K) (`0x914E`); binærken er 63 460 bytes. M451 flash = 40/72/128/256 KB; firmware antager ≥128 K → **M451xE (128 K) eller M451xG (256 K)**.
11. **RT-Thread** ("2006–2020 Copyright by rt-thread team"), build **"Sep 23 2026"**, board **"MEM03"**, fw **5.1.4** — konsistent med OTA-manifestet.

### 8.2 Hvorfor ikke de andre hypoteser?
* **M031/M032:** samme SYS/CLK/GPIO/FMC-baser og samme `0x59/0x16/0x88`-unlock, **men** Cortex-M0/M0+ → ingen `MOVW`, ingen FPU/CPACR, HIRC=48 MHz (ikke 22,1184). Binærken kræver Thumb-2+FPU → afvist.
* **GD32F10x / HC32:** anden periferie-map (GD32 FMC@`0x40022000`, USART@`0x40013800` osv.), anden unlock-sekvens, ingen `0x945`-PDID. Afvist.
* **"FMC PID"** er Nuvoton-terminologi, men registeret der læses er `SYS_PDID` — og PDID-check `0x945` + hele SystemInit peger entydigt på **M451**.

**Mest sandsynlig part:** `M451LE6AE` (128 K flash / 32 K SRAM, LQFP64) eller `M451LG6AE` (256 K / 32 K, LQFP64) — eller tilsvarende M451/M452/M453-variant. Eksakt flash + package kan bekræftes ved at læse `SYS_PDID` (via SWD/ISP) eller chipmærkningen på boardet.

---

## 9. Fremtidig brug — Python-recept (pyserial)

Når man har board-tilgang (eller via displayets `/dev/ttyS1`), kan man tale protokollen direkte. **Bemærk:** display-appen `demo_v9` holder selv `/dev/ttyS1` åben og poller — for at undgå kollision bør man enten stoppe `demo_v9` eller kun lytte/passivt sende på et tidspunkt hvor linket er idle. Nedenstående er en minimal, korrekt klient.

```python
import serial, struct, time

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

def frame(cmd, sub, words):
    """AA 55 CMD SUB LEN payload(16-bit LE ord) CRC16 LE."""
    payload = b''.join(struct.pack('<H', w & 0xFFFF) for w in words)
    body = bytes([0xAA, 0x55, cmd, sub, len(words)]) + payload
    c = crc16_modbus(body)
    return body + struct.pack('<H', c)

def query(sub, nwords):
    """7-byte cmd-3 read-query (ingen payload)."""
    body = bytes([0xAA, 0x55, 0x03, sub, nwords])
    return body + struct.pack('<H', crc16_modbus(body))

ser = serial.Serial('/dev/ttyS1', 57600, bytesize=8, parity='N',
                    stopbits=1, timeout=0.2)   # på MCU-board: tilsvarende UART1

# Eksempel 1: læs status-blok (ord 32..111)
ser.write(query(32, 80))
# ... læs og parse cmd-0x83-svar (AA 55 83 20 50 <160 bytes> CRC)

# Eksempel 2: tænd maskinen (write register 0 = 1)
ser.write(frame(0x06, 0, [1]))
# ... vent på cmd-0x86 ACK

# Eksempel 3: sæt bryg-temperatur til 87,0 °C (register 11 = 870)
ser.write(frame(0x06, 11, [870]))

def parse_frames(buf):
    """Yield (cmd, sub, words) for komplette AA 55-frames i buf."""
    i = 0
    while i + 7 <= len(buf):
        if buf[i] != 0xAA or buf[i+1] != 0x55:
            i += 1; continue
        cmd, sub, L = buf[i+2], buf[i+3], buf[i+4]
        total = 2*L + 7
        if i + total > len(buf): break
        body = buf[i:i+total-2]
        crc_rx = struct.unpack_from('<H', buf, i+total-2)[0]
        if crc16_modbus(body) == crc_rx:
            words = [struct.unpack_from('<H', body, 5+2*k)[0] for k in range(L)]
            yield cmd, sub, words
            i += total
        else:
            i += 1

# Lyt på MCU'ens status-broadcasts (cmd 0x83) og versions-requests (0x89):
buf = b''
while True:
    buf += ser.read(256)
    for cmd, sub, words in parse_frames(buf):
        if cmd == 0x83:
            print("status regs", sub, words)
        elif cmd == 0x89:
            # MCU beder om version — svar cmd 9 med ASCII-streng
            ver = b'1.1.1-69'
            body = bytes([0xAA,0x55,0x09,len(ver),0x00]) + ver
            ser.write(body + struct.pack('<H', crc16_modbus(body)))
    buf = buf[-512:]
```

**Vigtige forbehold:**
* Register-semantikken i §5 er delvist udledt; valider mod en logicanalyzer/PCAP før man skriver til ukendte ord.
* MCU'en forventer versions-håndtryk (cmd 0x89 → cmd 9) og periodiske queries; en ren "lytter" får status-broadcasts uden at skulle sende noget.
* OTA (§7) bør **ikke** afprøves mod en kørende maskine uden backup — den sletter flash-staging og rebootter.

---

## 10. Appendiks — nøgle-adresser og disassembly-uddrag

### 10.1 demo_v9 (display, ARMv7, VMA = fil-offset + 0x10000 for .text/.rodata)

| Symbol | Adresse | Indhold |
|---|---|---|
| `fuart_thread` | `0x14671C` | `open_port(1)`; `set_opt(fd,57600,8,'N',1)`; `lv_timer_create(uartRxProcess,60)`; loop `usleep(10000)` |
| `set_opt` | `0x144950` | `cfsetispeed/ospeed(0x1001=B57600)`; CS8; clear PARENB/CSTOPB; VMIN=VTIME=0 |
| `usart_send` | `0x146D5C` | `buf_tx[2]=cmd,[3]=sub,[4]=len`; `memcpy(buf+5, machine+0xFE8+2·sub, 2·len)`; `CRC_GetModbus16(buf,2·len+5)` → `buf[5+2·len]` |
| `usart_ckeck` | `0x146E70` | 7-byte query: `buf[2..4]=cmd,sub,len`; CRC over 5 bytes → `buf[5]` |
| `check` (RX-parser) | `0x146164` | header-tabel `AA 55` @ `0x18C378`; cmd-0x86 clear retransmit; cmd-0x89 → `verSend` |
| `ota_check` (OTA-parser) | `0x147AA4` | header-tabel `CC DD` @ `0x18C37C`; dispatch cmd 1/2/3/4 |
| `crc16_modbus` | `0x144798` | init 0xFFFF, poly 0xA001 (bitvis) |
| `CRC_GetModbus16` | `0x144914` | tabel @ `0x18BFA0` (= standard MODBUS-tabel) |
| `calculateChecksum` | `0x1474C8` | 32-bit byte-sum af fil (OTA-check) |
| `request_update` | `0x147314` | `CC DD 01 0F 00 size check version`; CRC @ offset 20 |
| `sendData` | `0x147560` | `CC DD 02 LEN seq data`; CRC |
| `buf_tx` | `0x1A5F28` | initial `AA 55 …` |
| `powerSendBuf` | `0x1A6030` | `AA 55 08 …` (15 bytes) |
| `verSendBuf` | `0x1A6040` | `AA 55 09 …` |
| `mcu_ota_container` | `0x1A61D0` | initial `CC DD …` |

**`usart_send` frame-opbygning (uddrag):**
```
146d78: strh r2,[r3,#2]      ; buf_tx[2..3] = cmd (16-bit, men [3] overskrives af sub)
146d9c: strb r2,[r3,#3]      ; buf_tx[3] = sub
146dac: strb r2,[r3,#4]      ; buf_tx[4] = len (ord)
146de8: bl   memcpy          ; buf_tx+5 ← machine+0xFE8+2·sub, 2·len bytes
146e04: bl   CRC_GetModbus16 ; crc over buf_tx[0 .. 4+2·len]
146e2c: strh r2,[r3]         ; buf_tx[5+2·len] = crc (LE)
```

### 10.2 MEM03_V514_260923.bin (MCU, raw Cortex-M, flash-base 0x0)

| Funktion / data | Adresse | Indhold |
|---|---|---|
| Vector-table | `0x000` | SP=`0x200045E0`, Reset=`0xB3B9`, 64 IRQs |
| Reset_Handler | `0xB3B8` | M451 unlock/POR/INV-type (se §8.1) |
| SystemInit | `0x6AB4` | PDID>>12==0x945 → POR18; CPACR FPU |
| chip-info print | `0x868` | "FMC PID"=`[0x40000000]`; "Flash=%dk,Sram=32k"; UID |
| flash-size probe | `0x914E` | ISP-læs `0x20000`/`0x40000` → 128/256 K |
| UART_Open | `0x8908` | UART1=`0x40071000`, UART3=`0x40073000`; MFP GPE_MFPH=0x11 |
| baud-beregning | `0x86C2` | clk/(div+1) / baud; DIV16/DIV8 |
| board-init (UART) | `0x2C2A` | `mov.w r1,#0xE100`(57600); `UART_Open(UART1,…)` |
| RX-parser (normal+OTA) | `0x9AAC` | 19-state TBB-maskine @ `0x9AD8`; header AA/CC |
| cmd-dispatch | `0x9C28` | cmd 3→`0x9A3A`, 6→`0x99C6`, 8→`0x9C88`, 9→`0x9E12` |
| status-builder (cmd 0x83) | `0x9A3A` | `AA 55 83 sub len` + register-ord + CRC |
| write-ACK (cmd 0x86) | `0x99C6` | skriver register-file + ACK |
| versions-request (0x89) | `0x9A94` | konstant frame @ `0x9D70` = `AA 55 89 01 00 00 6F 81` |
| MCU CRC | `0x98E8` | init 0xFFFF poly 0xA001; return byte-swappet |
| register-file / APP Param | `0x20000F96` | 129 ord; init @ `0x9966` |
| status-task loop | `0x286E…0x2A3E` | `rt_thread_delay(10)`; broadcast sub32 len80 @ `0x2744` |
| OTA-request-handler | `0x9F00` / `0xB700` | cmd 1: size/check/version; flash-erase; ACK 0x81 |
| OTA-data-handler | `0xB90C` | cmd 2: seq-check, FMC-write, pct, ACK |
| OTA-ACK builders | `0xB5A2`(cmd1) / `0xB604`(cmd2) | CC DD templates |
| OTA-templates (const) | `0xF304`(cmd1), `0xF314`(cmd2), `0xF2F8`(cmd4) | `CC DD …` |
| SystemCoreClock | `0x748` | 72 000 000 |
| SysTick reload | `0x750` | 71999 (1 ms) |
| clock-tabel | `0xF2B8` | {12 MHz, PLL, 32768, 22,1184 MHz} |
| RT-Thread banner | `0xCD70` | build "Sep 23 2026" (`0xCD94`) |

**MCU cmd-6 write + ACK (uddrag @ `0x9C54`):**
```
9c54: ldrb r1,[r3,#3]      ; sub
9c56: ldrb r2,[r3,#4]      ; len
9c58: adds r0,r1,r2
9c5a: cmp  r0,#0x21        ; sub+len < 33 ?
9c5c: bhs  9c68            ; ellers udvidet skrive-bånd (ord 112..114) special-case
9c5e: adds r3,r4,#5        ; payload
9c62: bl   99c6            ; skriv register-file + send cmd-0x86 ACK
```

**MCU status-broadcast (uddrag @ `0x2744`):**
```
2744: movs r2,#0x50        ; len = 80 ord
2746: movs r1,#0x20        ; sub = 32
2748: ldr  r0,[pc,#0x160]  ; UART1 handle
274a: bl   9a3a            ; send AA 55 83 20 50 <ord 32..111> CRC
```

---

## 11. Verifikations-status

| Lag | Verificeret mod | Sikkerhed |
|---|---|---|
| Baud/paritet/stopbits | begge binærer (display `set_opt`, MCU `UART_Open`) | **Høj** |
| Frame-header/længde/CRC | begge binærer + CRC-match mod konstant-frame (`0x816F`) | **Høj** |
| Kommando-dispatch (3/6/8/9 ↔ 0x83/0x86/0x88/0x89) | begge binærer | **Høj** |
| OTA-frames + flow | begge binærer (request/data/ACK/reboot) | **Høj** |
| Register-file base + størrelse | MCU (`0x20000F96`, 129 ord) + display mirror (`+0xFE8`) | **Høj** |
| Register-semantik pr. ord | delvist (defaults + kaldsteder + config.json) | **Middel** — valider med PCAP |
| Chip = M451 | SystemInit byte-match + PDID 0x945 + periferie-map + FPU + HIRC | **Høj** |
| Eksakt flash/package | flash-probe (128/256 K), SP i SRAM1 | **Middel** — kræver fysisk aflæsning |

*— research sub-agent, 2026-11-16. Statisk analyse; enheden ikke berørt.*