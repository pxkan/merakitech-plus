# POWER-VERIFY — v6.14 tænd/sluk-knap: on-device-bevis + hardware-fund (MerakiTech Gen 1)

> **Note (offentlig udgave):** Udviklingsnoter skrevet under udviklingen. Stier som `artifacts/…`, `boot_install.py`-trin og md5-tabeller refererer til forfatterens arbejdsmiljø; som slutbruger skal du bruge `./install.sh` og `./revert.sh` (se [README](../README.md)). Producentens filer er ikke inkluderet; IP-adresser og andre personlige data er fjernet.

**Dato:** 2026-11-18 · **Enhed:** <display-ip> (Sanbon SAT62091M, kernel 4.9.84) · **Binær:** ourui v6.14
`1dd63f42e4b0d649ef851660fc123a10` (LIVE i slot B, se `INSTALL-v614-B.md`)
**Metode:** passiv strace (`-f -tt -yy -z -e trace=read,write`) på den LIVE proces + tabsfri persistent
stream (`tail -f | nc -l`) til Mac'en; evdev write-injection (`keyinj.c`) til syntetisk knap;
fb-captures via `fbpage`; DIAGNOSE-aflæsning. **Ingen reboot, ingen config-/flag-/slot-/OTA-writes.**

**Konklusion:**
1. ✅ **SLUK-sekvensen bevist byte-for-byte on-device** med den ægte MCU: cmd8 tid/alarm-frame
   (alarm 0/0/0, gyldig CRC) → MCU'ens 0x88-kvittering → **cmd6 sub0 len2 [0,0] =
   `aa 55 06 00 02 00 00 00 00 56 94`** (stock's præcise PowerOff-frame).
2. ✅ **TÆND-sekvensen bevist byte-for-byte on-device**: PowerOn `aa 55 06 00 01 01 00 c5 5f` +
   vågen `aa 55 06 01 01 00 00 c5 33` + boot-push (`sub1 len30` + `sub112 len3`), alle ACK'et (0x86 m.
   echo), ord 0 = 1 bagefter, og controllerens ord 11/14/25/114 = config'ens værdier (870/3/650/70).
3. ⚠️ **MEN: den fysiske knap kan ikke udløse ourui's software-vej på denne enhed.** To hardware-mekanismer
   skærer display-strømmen: (a) kernel/PMIC-cut ved hold ≥ ~0,8 s på k62091-noden (event1) — **før**
   ourui's 1,5 s-tærskel; (b) MCU'ens dvale skærer display-strømmen ~8 ms efter PowerOff-framen.
   §19's forudsætning *"ourui suspenderer ikke displayet, så netværk/telnet bliver oppe"* og den levende
   SLUKKET-side/TÆNDER…-overgang er derfor **ikke opnåelige på denne hardware** — sluk = strømsvigt,
   tænd = kold boot. Beviserne nedenfor er fanget omkring disse cuts.

---

## 1. Hardware-fundet — beviskæde

| # | Forsøg (UTC) | Trigger | Resultat | Slutning |
|---|---|---|---|---|
| 1 | 20:09–20:10 | fysisk hold ~2 s (live 3963) | skærm sort/død; telnet væk; strace-log tabt i efterfølgende boot | hold dræber displayet; log kræver stream der overlever død |
| 2 | 20:40:37→39.44 | fysisk minimal-hold (~1,6 s) | død 20:40:39.44; chunk-spool-stream var tabsbehæftet (mistede chunks) | tabsfri stream nødvendig |
| 3 | 20:53:48 | **fysisk kort tryk 0,52 s** | **events leveres**: event1 kode 28 ned 20:53:48.103 / op .620; DIAGNOSE TRYK·SLUK·TÆND = 1·0·0, SIDSTE KNAP `28 = 0` | knap-noden virker for korte tryk; kernel-cut tærskel > 0,52 s |
| 4 | 21:00:00→02.106 | fysisk minimal-hold | død 21:00:02.106; **tabsfri log: ingen knap-events, ingen cmd8/cmd6, kun polls + keep-awake** indtil død | hold ≥ ~0,8 s leverer ingen events til ourui → ourui fyrer aldrig |
| 5 | 21:06:46.835→47.664 | **injiceret** hold 1,6 s, kode 28, **event1** | key-down leveret 21:06:46.835228; død 21:06:47.664409 = **+829 ms**; ingen key-up, ingen sluk-frames | kernel-cut ≈ 0,8 s efter key-down, uafhængigt af fysisk/injiceret → handler i input-laget på k62091 |
| 6 | 21:12:4x | injiceret hold 1,6 s, kode 28, **event0** | overlevde (men events blev kasseret: keybit) | — |
| 7 | 21:17:56 | injiceret kode **17**, event0 (deklareret i keybit) | leveret til begge fds (touch + kbd) | evdev-injection kasserer koder noden ikke deklarerer (event0 keybit lav-ord = 0x20000 → kode 17; 28/10 kasseres) |
| 8 | 21:18:13.695→15.282 | injiceret hold 1,6 s, kode 17, **event0** (test-mode `OURUI_KBD=event0 OURUI_NOGRAB=1`) | **ourui fyrer ved +1,5 s**: cmd8 → 0x88 → PowerOff; død **+7,8 ms efter PowerOff** | kernel-handleren er device-specifik (k62091); MCU-dvale skærer display-strømmen næsten øjeblikkeligt efter PowerOff |
| 9 | 21:21 / 21:24 | fysisk **kort tryk** efter død | **kold boot** (uptime nulstillet, ny pid, /tmp tømt) → ourui opstart = PowerOn + vågen + boot-push | TÆND på denne hardware = kold boot, ikke levende resume |

**Kernel-cut-mekanismen:** k62091.ko er stripped (ingen parametre, `/sys/module/k62091/parameters/` tom);
cutten sidder i input-laget bundet til k62091-enheden (forsøg 5 vs. 8). Den kan ikke slås fra uden
modul-/kernel-indgreb (ikke gjort). **MCU-dvale-cutten** er maskinens reelle sluk-adfærd: display-modulet
mister strømmen når controlleren sover — derfor kan hverken stock's eller ourui's "levende SLUKKET"
vises på denne enhed, og telnet/fb er ikke tilgængelige i slukket tilstand.

---

## 2. SLUK-frames byte-for-byte (capture: `power_v614_live/attempt5/uart_sluk_window.log`)

Test-mode: B launched stop-first med `OURUI_KBD=/dev/input/event0 OURUI_NOGRAB=1` (watchdog-sikret;
ingen filer ændret). Knap injiceret på event0 med kode 17 (ourui tæller alle EV_KEY-koder, §19).
Hold 1,6 s: key-down læst af kfd **21:18:13.694905**.

| Tid (UTC) | Retning | Frame (byte-for-byte) | Bemærkning |
|---|---|---|---|
| 21:18:15.213663 | TX | `aa 55 08 ea 07 0b 12 15 12 0f 00 00 00 b5 ca` | **cmd8** tid/alarm: år 0x07EA=2026, md 11, dag 18, 21:18:15, alarm 0/0/0; CRC ok; = key-down + **1518,8 ms** (ourui's HOLD 1,5 s) |
| 21:18:15.227870 | RX | `aa 55 88 ea 07 0b 12 15 12 0f 00 00 00 1c 08` | **0x88** kvittering for cmd8 (echo), +14,2 ms |
| 21:18:15.273833 | TX | `aa 55 06 00 02 00 00 00 00 56 94` | **PowerOff = cmd6 sub0 len2 [0,0]** — præcis stock's frame; +60,2 ms efter cmd8-write (= fire+80 ms, `PW_GAP_MS`; cmd8-write lå 19 ms efter fire pga. loop-flush) |
| 21:18:15.281584 | — | sidste syscall i loggen | **display-strøm dør +7,8 ms efter PowerOff** (MCU-dvale cut) |

**0 CRC-fejl i TX og RX** i vinduet. Ingen start-/stop-frames; ingen andre cmd6-writes i vinduet
(keep-awake stod stille fra fire, jf. `proto_keepawake`: `pw.state != PW_ON → return`).

**Ikke observerbart pga. strømsvigtet (dokumenteret, ikke fortiet):**
* PowerOff-**ACK** (0x86 sub0 len2): MCU'en nåede ikke at svare før displayet døde (cut +7,8 ms).
* **ord 0 = 0**-svar og den tavse dvale-periode: displayet dør før MCU'ens sleep-task svarer; dvale
  er i stedet bevist indirekte ved selve strøm-cutten + kold-boot-ved-tænd (MCU-reset-signatur).
* **pwm2/enable = 0** og **fb SLUKKET**: ourui slukker lyset 1,5 s efter fire og tegner SLUKKET efter
  ACK — begge dele lå efter strøm-cutten. (I attempt 1 så brugeren kortvarigt en sort side; det kan
  ikke fb-fanges på denne hardware.)
* Den levende **TÆNDER… → PowerOn → re-push**-overgang: kræver at MCU vågner mens displayet lever —
  umuligt her (MCU vågner kun af egen GPIO/alarm; GPIO-hold ≥ 0,8 s skærer display-strømmen).

---

## 3. TÆND-frames byte-for-byte (capture: `power_v614_live/attempt7/uart_boot_window.log`)

Tænd på denne hardware = brugerens tryk → kold boot → ourui's opstart (samme frames som stock's
opstart: PowerOn + vågen + boot-push). Captured fra første write efter watchdog-reload 21:24:42
(strace armeret før reload; pid 3374):

| Tid (UTC) | Retning | Frame (byte-for-byte) | Bemærkning |
|---|---|---|---|
| 21:24:42.948401 | TX | `aa 55 06 00 01 01 00 c5 5f` | **PowerOn** (ord 0 = 1) |
| 21:24:42.995537 | RX | `aa 55 86 00 01 01 00 c4 81` | ACK 0x86 m. echo, +47,1 ms; samme RX-runde: ord 0 = **1** |
| 21:24:43.004228 | TX | `aa 55 06 01 01 00 00 c5 33` | **vågen** (ord 1 = 0), +55,8 ms efter PowerOn |
| 21:24:43.012338 | RX | `aa 55 86 01 01 00 00 c4 ed` | ACK, +8,1 ms |
| 21:24:43.015686 | TX | `aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 00 00 00 00 00 00 00 00 00 00 66 03 00 00 3c 00 03 00 3c 00 00 00 90 01 00 00 00 00 00 00 00 00 00 00 68 01 00 00 8a 02 00 00 00 00 00 00 00 00 00 00 62 3e` | **boot-push sub1 len30** (ord 1..30 fra config) |
| 21:24:43.046006 | RX | `aa 55 86 01 1e … e3 de` | ACK m. echo af hele blokken, +30,3 ms |
| 21:24:43.051800 | TX | `aa 55 06 70 03 04 01 00 06 46 00 c7 ce` | **boot-push sub112 len3** (ord 112=0x0104, 113=0x0600, 114=70) |
| 21:24:43.064110 | RX | `aa 55 86 70 03 04 01 00 06 46 00 a6 08` | ACK, +12,3 ms |

**Config-gendannelse bevist:** boot-push'ens ord 11 = **870** (87,0 °C), ord 14 = **3**, ord 25 = **650**,
ord 114 = **70** — og controllerens efterfølgende svar (sub0/sub112-polls) viser samme værdier;
fb DIAGNOSE: **BOOT-PUSH 1-30 OK · 112-114 OK**, CONFIG MD5 **B1F33E23**, VÅGEN (ORD 1) **JA**.
Efter MCU-reset (kold boot) står controlleren altså på config'ens værdier igen — det er den
gendannelse, §19's re-push skal sikre ved levende wake; her leveret af boot-push ved opstart.
**0 CRC-fejl.** Ingen start-/stop-frames; ingen writes ud over PowerOn/vågen/boot-push/keep-awake/polls.

*Re-push-vejen (v6.6, 0x89-runden mens ourui kører) kunne ikke afprøves levende på denne enhed:*
MCU-reset kræver strøm-cyklus eller ord 30 (forbudt af frame-politik), og strøm-cyklus dræber displayet.
Koden er token-identisk med v6.13 (build_v614.sh's FROZEN-liste: `repush_step()` uændret).

---

## 4. fb- og DIAGNOSE-beviser

| Bevis | Fil | Indhold |
|---|---|---|
| Knap-node + tællere før test | `v614P_diag_power2.png` | TÆND/SLUK-KNAP: TILSTAND TÆNDT · STRØM ORD 0 = 1 · TRYK·SLUK·TÆND 0·0·0 · SIDSTE KNAP INGEN TRYK · **KNAP-NODE /dev/input/event1 · k62091 · INTET GRAB** |
| Kort tryk registreret | `v614P_diag_efter_kort.png` | TRYK·SLUK·TÆND **1·0·0** · SIDSTE KNAP **28 = 0 · 26 s** (fysisk 0,52 s-tryk) |
| Slut-V6.14 + boot-push | `v614P_slut_diag.png` | **VERSION MERAKI+ V6.14** · LINK OK · FASE KLAR · BOOT-PUSH 1-30 **OK** / 112-114 **OK** · FLAG FINDES · NÆSTE BOOT MERAKI+ · STARTET FRA SLOT B · SLOT B MERAKI+ FUNDET |
| Slut-HJEM sund | `v614P_slut_home.png` | KLAR · 99,5 °C · MÅL 87,0 · 4 ikoner + tandhjul |
| Install/reload-fb | `v614B_diag_version.png` · `v614B_slut_home.png` | VERSION V6.14 efter watchdog-reload 20:04 |
| **SLUKKET-fb / lys-fra** | **ikke opnåelig** | display-strømmen skæres før ourui's lys-sluk (fire+1,5 s) og SLUKKET-rendering (efter ACK); se §2 |

Backlight før/efter hele runden: `pwm2/enable = 1`, `duty_cycle = 50` (målt 20:57 og 21:26 via telnet;
mellemværdier i slukket tilstand ikke målbare pga. strøm-cut).

---

## 5. Afvigelser fra §19 / verify_power_v614.py's forventninger (ærligt register)

| §19-forventning | On-device realitet | Konsekvens for beviset |
|---|---|---|
| HOLD 1,5 s → ourui slukker | kernel-cut ved ~0,8 s på event1 kommer først; ourui ser aldrig holdet | sluk bevist med injiceret hold på event0 (test-mode), ikke fysisk knap |
| SLUKKET vises, lys fra, telnet lever | display-strøm skæres ved PowerOff (+8 ms) | ingen fb SLUKKET, ingen enable=0-måling, ingen dvale-tavshed i log; dvale bevist ved strøm-cut + kold-boot-tænd |
| NYT HOLD vækker MCU levende → TÆNDER… → PowerOn + re-push | MCU-GPIO-hold ≥ 0,8 s skærer display-strømmen; wake = kold boot | tænd bevist som opstarts-sekvens (PowerOn + vågen + boot-push + ACK'er + config-ord), ikke levende re-push |
| cmd8 → ≥80 ms → cmd6 (stock usleep(80000)) | målt 60,2 ms mellem cmd8-**write** og cmd6-write (ourui's 80 ms regnes fra fire; cmd8-write lå 19 ms efter fire) | rækkefølge + indhold korrekt; gap-afvigelse forklaret og dokumenteret |
| PowerOff ACK'et (0x86) | ikke set — display døde +7,8 ms efter framen | ACK kan ikke observeres på denne hardware; 0x88 for cmd8 er set |
| run.sh-tilstand (stock SIGSTOP) brugbar til testen | B-boot: intet stock at pause (BOOT.md §9.3); B ejer touch-grab | passiv strace på live proces + tabsfri stream i stedet |

**Anbefaling til parent/Fable:** §19's semantik er korrekt spejlet i koden (frames byte-identiske med
stock), men **hardwaren på denne enhed understøtter ikke den levende sluk/tilstand**: (a) kernel-cut
~0,8 s på k62091 gør enhver software-hold-tærskel ≥ 0,8 s uudløselig fysisk; (b) MCU-dvale skærer
display-strømmen, så "kun baggrundslyset slukkes" ikke kan lade sig gøre. Mulige veje (parent-beslutning):
sænk hold-tærsklen under 0,8 s (fx 0,6 s) så ourui når at fyre før kernel-cut — men displayet dør stadig
ved PowerOff, så SLUKKET-siden forbliver usynlig; eller acceptér at knappen på denne revision er en
hardware-strømafbryder og dokumentér §19 som gældende for andre hardware-revisioner. **Ingen kode er
ændret i denne runde.**

---

## 6. Bevis-filer

| Fil | Indhold |
|---|---|
| `power_v614_live/power_frames.json` | byte-eksakte TX-frames + RX-ACK'er for sluk- og boot-vinduet, m. tidspunkter, 0 CRC-fejl |
| `power_v614_live/attempt5/uart_sluk_window.log` | strace-vindue 21:18:1x (key-down, cmd8, 0x88, PowerOff, død) |
| `power_v614_live/attempt5/uart_stream_final.log` | fuld tabsfri stream fra test-mode (inkl. opstart 21:15:35+) |
| `power_v614_live/attempt7/uart_boot_window.log` | strace-vindue 21:24:4x (PowerOn → boot-push → ACK'er) |
| `power_v614_live/attempt7/uart_stream.log` | fuld stream fra watchdog-reload 21:24 |
| `power_v614_live/attempt3/uart_stream3.log` | tabsfri stream der viser fysisk minimal-hold **uden** events/frames indtil død 21:00:02 |
| `power_v614_live/attempt4/uart_stream.log` | injiceret event1-hold: død +829 ms efter key-down |
| `power_v614_live/attempt2/uart_off.log` | (tabsbehæftet chunk-spool; kun til sporbarhed — erstattet af attempt 3/4/5) |
| `power_v614_live/attempt5/config_words.json` · `attempt7/config_words.json` | boot-push'ens config-ord |
| `keyinj.c` / `keyinj` | injektoren (evdev write-injection; KEYINJ_DEV/KEYINJ_CODE) |
| `power_live_v614.py` | live-power-driver (passiv strace; start/off/on/abort) |
| `boot/ondevice/INSTALL-v614-B.md` | install + reload + slut-tilstand + revert |

---

*Test-agent, 2026-11-18. Sluk- og tænd-frames er bevist byte-for-byte på hardwaren med den ægte MCU;
den fysiske knap er på denne enhed en hardware-strømafbryder (kernel-cut ~0,8 s + MCU-dvale-cut),
så §19's levende SLUKKET/TÆNDER…-tilstande ikke kan opnås her. v6.14 er LIVE i slot B, sund, med
intakte backups og dokumenteret revert; ingen config/flag/slot-metadata/vendor-OTA-writes, ingen
agent-reboots.*
