# BOOT — boot-persistens + on-device UI-skift (MerakiTech Gen 1, Sanbon SAT62091M)

> **Note (offentlig udgave):** Udviklingsnoter skrevet under udviklingen. Stier som `artifacts/…`, `boot_install.py`-trin og md5-tabeller refererer til forfatterens arbejdsmiljø; som slutbruger skal du bruge `./install.sh` og `./revert.sh` (se [README](../README.md)). Producentens filer er ikke inkluderet; IP-adresser og andre personlige data er fjernet.

**Dato:** 2026-11-17 · **Status:** FORBEREDELSE — alt bygget og verificeret LOKALT
(self-tests + mock-dry-run). Enheden er offline; device-kørsel sker senere via
`boot_install.py` når brugeren er ved maskinen. Ingen kontakt med enheden er sket
under forberedelsen.

**Ejerskab:** denne mappe (`artifacts/ourui/boot/`) ejes af boot-agenten.
Fable-agenten ejer `ourui_v6.c`, `UI_v6.md`, `REVERT.md`, `round_v6/` — de filer er
IKKE rørt. Fables påkrævede UI-ændringer er specificeret i **§9 (UI-krav)** nedenfor.

---

## 1. Mekanismen

### 1.1 Vendor A/B-boot (uændret, udnyttet)
`/customer/demo.sh` (vendor boot-script, kopi: `artifacts/demo.sh`):
1. PWM-backlight + GPIO panel-init + `pinit.sh`.
2. Mounter UBI-volumenet `ubi0:new_customer` → `/new_customer` (**slot B**).
   `MOUNT_SUCCESS=1` ved succes.
3. OTA-håndtering (§7): `/customer/temp.tar.gz` pakkes ud i det *modsatte* slot.
4. insmod-liste (touch `tw31xx.ko`, knapper `k62091.ko`, netværk, USB…).
5. **Slot-branch:** findes flag-filen `/customer/new` OG mount lykkedes →
   **boot B**: `cd /new_customer; export LD_LIBRARY_PATH=…:/new_customer/libs:/config/wifi;
   ./demo_v9 &; /customer/test.sh new_customer &`.
   Ellers → **boot A**: samme mønster fra `/customer` med `/customer/libs`.

**Flaget `/customer/new` er hele persistens-mekanismen:** `touch` = boot B,
`rm` = boot A. Ingen anden vedvarende tilstand kræves.

### 1.2 Watchdog — kun NAVNET checker
`/customer/test.sh` (kopi: `artifacts/test.sh`) kører 1 s-loop:
`ps -A | grep "demo_v9"` → tomt? → `resetdemo <slot>` (cd + LD_LIBRARY_PATH +
`./demo_v9 &`). Den checker **kun at et proces-navn matcher `demo_v9`** — ikke
heartbeat, ikke exe-sti, ikke slot. Konsekvenser (alle udnyttet/bevidst):
* Vores UI installeret som `/new_customer/demo_v9` tilfredsstiller watchdog'en
  (navnet er `demo_v9`).
* En SIGSTOP'et proces tæller som "i live" (PID findes) — fase-2-mønsteret.
* **Watchdog-race ved runtime-skift:** forsvinder PID'en helt, genstarter
  watchdog'en *sit eget slots* binary inden for ~1 s. Et naivt
  "kill A → vent → launch B" kan derfor ende med A oven i B (to UI'er om fb/UART).
  Løsningen er **SIGSTOP-først** (§3.2).
* Watchdog'ens slot er fastlagt ved boot (`test.sh customer` / `test.sh new_customer`).
  Efter et runtime-skift kan watchdog-slotten derfor afvige fra den kørende UI —
  harmløst: crash → watchdog genstarter *boot-slottets* binary (sikker fallback),
  og næste reboot følger flaget igen.

### 1.3 De to skift-retninger
| Retning | Hvem | Hvordan | Reboot? |
|---|---|---|---|
| A→B (stock→ourui) | **switcher-daemonen** (denne leverance) | hemmelig gestus (§2) → SIGSTOP-først-skift (§3.2) | nej |
| B→A (ourui→stock) | **ourui selv** (Fable, §9) | "Skift til original NU"-knap: launch A, exit | nej |
| Boot-valg A/B | brugeren / `boot_install.py persist` | `touch`/`rm /customer/new` | ja (næste boot) |

I B-mode griber ourui `/dev/input/event0` eksklusivt (`EVIOCGRAB`, jf. FINDINGS
§5.10) → switcher'en får ingen events og idler. Når ourui exit'er (B→A-knappen),
frigives grebet automatisk (proces-død, også ved `kill -9`), og switcher'en
observerer igen → gestus'en kan skifte tilbage til B. **Begge retninger virker
uden reboot, og switcher'en behøver aldrig selv at håndtere B→A.**

---

## 2. Gestus-specifikation

**v6.12 (GESTUS-FIX.md): default er nu ÉN sammenhængende CENTER-hold ≥5000 ms**
inden for radius 40 px om (240,240) — trigger ved release. Hjørne-gestus'en nedenfor
bevares som fallback (`SWITCHER_GESTURE=corner`). Center er verificeret baggrund på
stock-home; radius 40 (ikke ~60) fordi nærmeste knap-kant ligger ~45 px fra center
(målt: measure_stock_home.py). Env: `SWITCHER_HOLD_MS`, `SWITCHER_CENTER_X/Y/R`.
Grab-bypass (stock's EVIOCGRAB): multi-node read-only (a) + sysfs-fallback (b) +
LD_PRELOAD-shim i guard-linjen (c) — se GESTUS-FIX.md §3.

| Parameter (corner-fallback, legacy default før v6.12) | Værdi |
|---|---|
| Zone | øverste venstre hjørne: **x < 90 og y < 90** (480×480 skærmpixels) |
| Anbefalet punkt | **(75,75)** — inden for det runde aktive område (afstand til centrum ≈233 < 240) og baggrund på stock-home |
| Antal | **3** long-presses |
| Hold-tid pr. press | **≥ 1500 ms** (fingeren skal blive i zonen hele holdet) |
| Vindue | første press' down → tredje press' up **≤ 6000 ms** |
| Eksempel-timing | 3×1600 ms hold + 2×300 ms pause = 5400 ms (tinj_hold's `gesture corner`) |
| Cooldown | 10 s efter en trigger (ingen gentagne skift) |

Detaljer:
* En press tæller KUN hvis den startede i zonen, blev i zonen hele holdet og
  varede ≥1,5 s. Korte taps og presses uden for zonen ignoreres (de nulstiller
  ikke sekvensen). Udløber vinduet, startes tællingen forfra.
* Koordinater skaleres fra touch-raw med samme formel som tinj.c's invers:
  `screen = (raw−min)·480/(max−min+1)`. Switcher'en læser akse-range via `EVIOCGABS`
  ved åbning (v6.12: fallback-konstant 0..480 = enhedens faktiske range, ikke 4095;
  SIM's streams passer `SWITCHER_RAW_MAX=4095`).
* **Sikkerhed på stock-UI:** zonen er baggrund på home-skærmen — Grinder-cirklen
  (center ≈(155,155), radius ≈68) når ikke ind i x<90,y<90 (nærmeste punkt (89,89)
  har afstand ≈93 til cirklens center). Verificeret mod
  `fb_after_revert.png`. På sleep-skærmen er
  zonen også baggrund. Gestus'en bør udføres fra home/sleep; på andre stock-sider
  kan hjørnet indeholde navigation (harmløst — aktuatorer startes kun fra de fire
  aktivitets-knapper med bekræftelse).
* **Read-only:** switcher'en åbner noden `O_RDONLY|O_NONBLOCK` og skriver ALDRIG
  til den. Stock-UI'ets input-kø påvirkes ikke (evdev leverer til alle læsere;
  kun `EVIOCGRAB` ville stjæle events — det gør switcher'en aldrig).
* Fysisk rækkevidde-forbehold: om touch-controlleren rapporterer helt ud i de
  firkantede hjørner (uden for det runde glas) er ikke live-verificeret. Derfor
  anbefales (75,75), som sikkert er inden for det runde område. Injektion via
  tinj_hold virker uanset (evdev-write går uden om controlleren).

---

## 3. switcher-daemonen (`switcher.c`)

### 3.1 Adfærd
* Statisk ARM-binær (`zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static`,
  0 warnings med `-Wall -Wextra -Werror`). Ingen deps, ingen libc-krav ud over musl.
* Starter fra demo.sh's guard-linje (§4) — i **begge** boots. I B-mode idler den
  (ourui's grab sulter den for events, by design).
* Input-discovery (v6.12, grab-bypass a): ALLE touch-noder åbnes read-only samtidigt
  (event0..9 med `ABS_MT_POSITION_X`, navn "TECHWIN"/"tw31xx" foretrækkes) — en
  duplikat-node stock ikke grabber leverer events. Override: `SWITCHER_INPUT=<node>`.
  Sysfs-fallback (b): `lu_coordinate_x/y` + `point_num` polls hvis evdev er blind
  (`SWITCHER_SYSFS_DIR`, `SWITCHER_SOURCE`, `SWITCHER_SYSFS_SCREEN`).
* **Grab-konflikt/åbn-fejl = idle-loop:** kan noden ikke åbnes (eller forsvinder),
  retries hvert 500 ms uden crash. En grab'et node kan godt åbnes — den giver
  blot ingen events (det er B-mode-idle).
* Loop: `poll` med **50 ms** timeout; read-fejl (EAGAIN normal, ENODEV/EIO →
  genåbn). Ingen writes til UART/fb/config/input. Ingen reboot. Ingen grab.
* Single-instance: `flock` på `/tmp/switcher.lock` (ekstra instans exit'er straks).
* Log: `/tmp/switcher.log` (UTC-tidsstempler, rotation ved 64 KB). B's stdout/
  stderr ved runtime-skift → `/tmp/ourui_boot.log`.
* Signaler: SIGTERM/SIGINT → ren exit (bruges af revert/emergency).
* Gestus-timing følger **event-tidsstempler** (kernel `input_event.time`), ikke
  wall-clock — robust over for scheduling-forsinkelser og muliggør fil-mode-test.
* Test-hooks (env): `SWITCHER_INPUT_FILE` (rå evdev-stream fra fil/FIFO),
  `SWITCHER_RAW_MAX` (fil-mode-default nu 480 = enhedens range), `SWITCHER_LOG`,
  `SWITCHER_LOCK`, `SWITCHER_GESTURE`, `SWITCHER_HOLD_MS`, `SWITCHER_CENTER_X/Y/R`.

### 3.2 Mode-check + SIGSTOP-først-skift (A→B)
Ved trigger:
1. `pidof demo_v9` → for hver PID: `readlink /proc/<pid>/exe`.
   * `/customer/demo_v9` → **A**; `/new_customer/demo_v9` → **B**; andet/ingen → ignorér.
   * B kører (eller blandet A+B) → **ingen handling** (logges). Ingen demo_v9 → ingen handling.
2. **SIGSTOP A's PID** — watchdog'en ser stadig PID'en → ingen genstart-race.
3. `fork` + `setsid` + `chdir /new_customer` +
   `LD_LIBRARY_PATH=/new_customer/libs:/config/wifi` +
   `execl /new_customer/demo_v9` (argv0 `./demo_v9` så watchdog-grep matcher).
4. Verificér B: `/proc/<child>/exe == /new_customer/demo_v9` inden for 2 s.
5. **SIGKILL A** + vent exit (≤2 s). Log "skift A→B FÆRDIG".
6. **Fallback hvis B ikke kom op:** SIGCONT A + dræb B-fork'en → original UI
   tilbage, ingen skade. (A holdt fb/UART-fd'er under STOP; B kunne derfor ikke
   eje dem alene før A var væk — men B init'er fb/UART mens A er stoppet, præcis
   som fase-2-mønsteret, og MCU'en resynker på UART-header, jf. FINDINGS §5.7.)

Worst case ved strøm-tab midt i skift: flaget er uændret → næste boot følger
flaget (A hvis ikke sat). demo.sh's guard-linje er `[ -x … ] &&` → manglende
binary = ingen switcher, stock boot'er normalt.

### 3.3 Self-test (lokalt, uden enhed)
* `switcher_selftest` (host-build af samme C-fil, `-DSWITCHER_SELFTEST`):
  **29 deterministiske checks** — gestus happy-path, 2 holds, for korte, uden for
  zone, for langsomt vindue, finger-forlader-zone, grænser (89/90), raw-skalering,
  mode-klassifikation, beslutningslogik ([A]→skift, [B]→idle, [A,B]→idle,
  [A,A]→idle, []→idle), gentagen trigger, vindues-udløb, taps mellem holds,
  MT-only-stream. Alle grønne.
* `test_switcher.py`: end-to-end gennem **den rigtige daemon-kode**
  (`switcher_host`, fil-mode): syntetisk 16-byte evdev-stream (positiv + 4
  negative) → beviser trigger + at mode-check eksekveres og er sikker uden
  demo_v9 ("ingen demo_v9 → ingen handling"). Beviser: `test-logs/`.
* Kendt bug fundet og fixet af self-testen: tracking-id ankommer før position i
  samme batch → hjørne-position latch'es nu ved første `SYN_REPORT` efter down.

---

## 4. Installations-procedure (når enheden er online)

**Forudsætninger:** enhed i **A-mode** (stock kører, jf. FINDINGS §5.8-status),
`/new_customer` mountet, config på baseline (ellers restore via REVERT.md først),
og **OTA blokeret eller accepteret risiko** (§7 — anbefal: blokér
`oss.sanbontft.com` på firewall FØR install). Kør ét trin ad gangen; hvert trin
har gates og logger alt til `boot/install.log`. Fejl → automatisk emergency (§5.3).

```sh
cd artifacts/ourui/boot

# 0. (valgfrit, nu allerede gjort lokalt) dry-run mod mock-rod:
python3 boot_install.py mock-setup --mock-root mock_root
python3 boot_install.py backup     --mock-root mock_root
python3 boot_install.py install    --mock-root mock_root
python3 boot_install.py boot-test  --mock-root mock_root
python3 boot_install.py revert-test --mock-root mock_root
python3 boot_install.py persist    --mock-root mock_root --default-boot A

# 1. DEVICE — backup (md5+kopier, flag noteres, config-baseline-gate):
python3 boot_install.py backup
#    → /new_customer/ourui/boot/demo.sh.bak      (md5-verificeret kopi af demo.sh)
#    → /new_customer/ourui/boot/demo_v9.vendor   (md5-verificeret kopi af B's vendor-binary)
#    → state.json m. flag-tilstand + config-md5; lokale snapshots i boot/backups/

# 2. DEVICE — install (upload + ÉN linje i demo.sh):
python3 boot_install.py install
#    → switcher, tinj_hold, fbpage → /new_customer/ourui/boot/
#    → artifacts/ourui/ourui_v6 (md5 LÆSES VED KØRSEL → fanger Fables v6.1)
#      push'es som /new_customer/demo_v9 (md5-round-trip via push_nc)
#    → demo.sh: guard-linjen indsættes FØR slot-branch, EFTER mounts/insmod (v6.12:
#      shim-export kombineret ind i SAMME linje — præcis ÉN linje bevaret):
#        [ -x /new_customer/ourui/boot/switcher ] && { [ -f /new_customer/ourui/boot/grab_shim.so ] && export LD_PRELOAD=/new_customer/ourui/boot/grab_shim.so${LD_PRELOAD:+:$LD_PRELOAD}; /new_customer/ourui/boot/switcher & }
#      (atomisk: cp→/customer/.demo_sh.tmp, md5-gate, mv, chmod 755, sync;
#       legacy guard-linje fra v6.11 opgraderes automatisk til den nye)
#    → revert-linjen logges (REVERT_LINE i install.log + state.json)
#    Idempotent: køres igen → guard-linjen genkendes, intet dobbelt-indsættes.

# 3. DEVICE — boot-test (3 reboots + gestus; maskinen efterlades i A, flag væk):
python3 boot_install.py boot-test
#    a) touch flag + reboot → verificér B: exe-link=/new_customer/demo_v9,
#       pidof switcher, fb-capture (test-logs/boot_B_ourui.png)
#    b) rm flag + reboot → verificér A: exe-link=/customer/demo_v9 + fb-capture
#    c) gestus via tinj_hold (3×1,6 s i (75,75)) → verificér runtime-skift til B
#       UDEN reboot: exe-link + fb + /tmp/switcher.log gemmes
#    d) skift tilbage til A (SIGSTOP-først kill+launch; ourui's knap kan også bruges)

# 4. DEVICE — brugerens persistens-valg (gælder NÆSTE boot, ingen reboot nu):
python3 boot_install.py persist --default-boot B   # vores UI som default
python3 boot_install.py persist --default-boot A   # stock som default (gestus-skift stadig muligt)
```

`--default-boot A|B` kan også gives som afslutning på ethvert andet trin
(sættes/fjernes til sidst efter trinnet er lykkedes). `--force` overstyrer
config-baseline-gaten (logges; brug kun hvis config-drift er forstået).

**Manuel verifikation efter install (telnet):**
```sh
md5sum /customer/demo.sh /new_customer/demo_v9 /new_customer/ourui/boot/switcher
grep -n switcher /customer/demo.sh          # præcis ÉN linje, før "starting from B"-branchen
pidof switcher                               # kører (startet ved boot)
cat /tmp/switcher.log
```

---

## 5. Revert-procedurer

### 5.1 Fuld revert-test (ét kald, verificeret mønster)
```sh
python3 boot_install.py revert-test
```
Gør, i rækkefølge med md5-gates: genskaber `demo.sh` fra `.bak` (atomisk tmp+mv,
md5 == original) → dræber switcher → `rm /customer/new` + reboot → verificerer
stock A urørt (exe-link, ingen switcher, demo.sh-md5) → genskaber B's
`demo_v9` fra `demo_v9.vendor` (md5 == original) → rydder `/tmp` for egne filer →
verificerer config-md5 uændret. Backups bliver liggende i
`/new_customer/ourui/boot/` (harmløse; kan fjernes med `rm -rf` bagefter hvis
ønsket — men så er fuld revert ikke længere possible uden ny backup).

### 5.2 Manuel revert (telnet, hvis script ikke kan køre)
```sh
# 1. demo.sh tilbage (atomisk, md5-gate):
cp /new_customer/ourui/boot/demo.sh.bak /customer/.demo_sh.tmp
md5sum /customer/.demo_sh.tmp        # skal matche original (se §6)
mv /customer/.demo_sh.tmp /customer/demo.sh && sync
# 2. switcher væk + flag væk:
kill $(pidof switcher) 2>/dev/null
rm -f /customer/new && sync
# 3. reboot → stock A:
reboot
# 4. (valgfrit) B's vendor-binary tilbage:
cp /new_customer/ourui/boot/demo_v9.vendor /new_customer/.demo_v9.tmp
md5sum /new_customer/.demo_v9.tmp    # skal matche vendor-md5 (§6)
mv /new_customer/.demo_v9.tmp /new_customer/demo_v9 && sync
```
**Kun gestus'en disables (behold install):** fjern guard-linjen manuelt
(samme tmp+mv-mønster med en redigeret kopi) eller `rm /new_customer/ourui/boot/switcher`
— `[ -x … ]`-gaten gør derefter linjen harmløs.

### 5.3 Emergency (automatisk ved enhver fejl i device-trin)
`boot_install.py emergency` (køres også automatisk når et device-trin fejler):
genskaber `demo.sh` fra `.bak` hvis afvigende → dræber switcher → sikrer A
(SIGCONT hvis stoppet; SIGSTOP-først-skift hvis B kører; manuel launch hvis ingen
demo_v9; dræb B hvis blandet A+B) → `rm` flag → sync. Logger alt; uafklarede fejl
rapporteres med henvisning til §5.2. **Maskinen efterlades altid i A.**

---

## 6. MD5-tabel (skabelon + lokalt målte værdier 2026-11-17)

Device-værdierne verificeres af scriptets gates ved kørsel; afvigelser = stop.
Udfyld device-kolonnen under installationen.

| Fil | Lokal/artefakt-md5 | Device før install | Device efter install | Device efter revert |
|---|---|---|---|---|
| `/customer/demo.sh` (original) | `45b8f5d3f79b5eb5c5b405963426d9d4` | ______ | `53cfde05…` (m. guard) | `45b8f5d3…` ✓ |
| `/customer/demo.sh` (installeret, +1 linje) | `53cfde0510e4c2b8ad531516c64bf669` | — | ______ | — |
| `/customer/demo_v9` (vendor A) | `d44c2414b2ac6f0c9370ed6c9f549903` | ______ | uændret | uændret |
| `/new_customer/demo_v9` (vendor B) | `d44c2414b2ac6f0c9370ed6c9f549903` | ______ | ourui-md5 ↓ | `d44c2414…` ✓ |
| ourui-binær `artifacts/ourui/ourui_v6` (v6.2 ved skrivning; installeren læser md5 ved kørsel) | `0bb93da0ed0824c424f1e16530f07022` * | — | ______ | — |
| `/new_customer/ourui/boot/switcher` | `6222d8c2236d1b27092ed345134ac9bb` | — | ______ | — |
| `/new_customer/ourui/boot/tinj_hold` | `32d04919f700194df7e553776c38d385` | — | ______ | — |
| `/new_customer/ourui/boot/fbpage` | `a1eefe6934067c280c2b44c4582c899a` | — | ______ | — |
| `/customer/config.json` (baseline) | `<config-md5-redacted>` | ______ | uændret | uændret ✓ |

\* ourui-md5 læses **ved kørsel** af `boot_install.py install` — Fables v6.1
rebuild fanges automatisk (bevist under forberedelsen: md5 ændrede sig fra
`90bcf094…` til `4d927802…` mellem builds, og scriptet brugte den nye).
State + snapshots: `boot/backups/state_snapshot.json`, `boot/backups/md5_history.json`,
device: `/new_customer/ourui/boot/state.json`.

---

## 7. OTA-interaktion (VIGTIG RISIKO)

Vendor-OTA (`/customer/temp.tar.gz`, usigneret kanal fra `oss.sanbontft.com`,
jf. FINDINGS §4.3/§6.6) håndteres i demo.sh FØR slot-branchen:

| Flag ved OTA | OTA gør | Konsekvens for os |
|---|---|---|
| `/customer/new` findes (B aktiv) | pakker til **/customer (A)**, **fjerner flaget** | næste boot = stock A (ny version). Vores `demo.sh`-guard-linje **overskrives** af OTA'ens demo.sh → switcher starter ikke mere; gestus død indtil re-install. ourui i B urørt men inaktiv. |
| intet flag (A aktiv) | pakker til **/new_customer (B)**, **sætter flaget** | næste boot = **B = vendor's nye demo_v9 — vores ourui overskrives!** (`demo_v9.vendor`-backup og `ourui/boot/` overlever, da tar'en kun rammer egne stier). Maskinen boot'er vendor-B uden vores viden. |
| mount fejlet | pakker kun til A | som række 1 uden flag-ændring. |

Derudover stripper OTA `softversion` fra `config.json` → config-md5 ændres →
vores baseline-gate vil (korrekt) nægte install/revert-test til config er
forstået/genskabt.

**Anbefaling ved persistens:** blokér `oss.sanbontft.com` (og evt.
`pro.ip-api.com`) på LAN-firewall'en så længe vores UI ønskes persistent.
Uden blokering: acceptér at en vendor-OTA kan (a) fjerne switcher-linjen og
(b) erstatte ourui i B + tvinge B-boot. Detektion: `md5sum /customer/demo.sh
/new_customer/demo_v9` mod §6-tabellen + `ls /customer/new`. Genopretning:
kør `backup` (ny baseline) + `install` igen — backups i `ourui/boot/` overlever
OTA, men **vendor-OTA'ens nye A-demo.sh bliver da den nye "original"** (backup
tager den aktuelle som sandhed; dokumentér versions-skiftet i md5-tabellen).

---

## 8. Risici

1. **`/customer/demo.sh` (slot A's boot-script) røres** — den eneste ændring i
   slot A: ÉN guard-linje, atomisk installeret, med md5-verificeret backup
   (`.bak`) + revert-test + emergency. Risiko for korrupt demo.sh minimeret af
   tmp+mv på samme filsystem + md5-gate før mv. Strøm-tab mellem cp og mv:
   enten gammel eller ny fil, aldrig halv (rename er atomisk).
2. **Switcher observerer kun input** (read-only, ingen grab, ingen writes til
   UART/fb/config). Kan ikke stjæle touches eller påvirke stock-UI'ets kø.
   Worst case ved crash: ingen gestus-skift (idle); watchdog/stock upåvirket
   (navnet `switcher` matcher ikke watchdog-grep'en).
3. **Tilfældig gestus-udløsning:** kræver 3 stationære ≥1,5 s holds i et
   90×90 px-hjørne (≈3,5 % af det firkantede areal, overvejende uden for det
   runde aktive område) inden for 6 s. Ved almindelig brug (tap på de fire
   aktivitets-knapper, swipe) er sandsynligheden **forsvindende lille**;
   realistiske utilsigtede kilder er rengøring af skærmen med lange tryk i
   hjørnet eller et barn der holder fingeren i hjørnet gentagne gange.
   Konsekvensen er mild: UI skifter til ourui (ingen aktuator-start; ourui
   starter aldrig grind/brew uden eksplicit knap + 650 ms hold), og brugeren
   kan skifte tilbage med ourui's "Skift til original NU"-knap (§9) eller
   kill+launch-mønsteret (§5.2). Gestus'en virker kun A→B.
   **Disable:** fjern guard-linjen via revert (§5.1/§5.2)
   eller `rm …/boot/switcher` (gaten gør linjen harmløs).
4. **Watchdog-slot-mismatch efter runtime-skift** (§1.2): crash efter skift →
   watchdog genstarter boot-slottets binary. Sikker fallback, men overraskende
   hvis man forventer "bliv i vores UI". Næste reboot normaliserer.
5. **SIGSTOP midt i UART-write** (A stoppes før B startes): partial frame til
   MCU — MCU resynker på header (FINDINGS §5.7, lav risiko, accepteret i fase 2).
6. **ourui's egne writes** (v6 GEM → config.json + MCU-registre) er uændrede og
   dokumenteret i REVERT.md; boot-leverancen tilføjer ingen nye config-writes.
   Bemærk REVERT.md's OTA-forbehold ved demo_v9-genstart.
7. **B-boot med vores UI = ingen stock-sleep-styring:** ourui v6 har keep-awake
   (word1=0 hvert 30 s) og styrer selv dvale; ved B-boot er stock-UI'ets
   sleep-logik ikke aktiv. Forventet adfærd, men noteret.
8. **Telnet-root åben for LAN** (FINDINGS §6.6) — uændret af denne leverance;
   hardening anbefales fortsat.

---

## 9. UI-krav til Fable-agenten (ourui v6.1+, SYSTEM-side)

Boot-leverancen er klar; følgende to kontroller skal Fable tilføje i ourui
(ejer af `ourui_v6.c`/`UI_v6.md`). Specifikation — implementeres og testes af
Fable, ikke af boot-agenten:

### 9.1 Boot-UI-toggle (persistens-valg)
* Placering: SYSTEM-siden, ny række "BOOT-UI" med toggle/knap-par.
* Viser aktuel tilstand: læs om `/customer/new` findes (`access()`/`stat()`):
  findes → "Vores UI (B) ved boot"; findes ikke → "Original (A) ved boot".
* Ved skift til B: `open("/customer/new", O_CREAT|O_WRONLY, 0644); close(); sync();`
  Ved skift til A: `unlink("/customer/new"); sync();`
* Effekt ved NÆSTE boot (ingen reboot fra knappen). Vis hjælpetekst:
  "Gælder næste genstart. OTA kan overskrive dette valg — se BOOT.md §7."
* Må IKKE røre demo.sh eller andre filer. Fejl (skrivebeskyttet fs) → vis fejl,
  behold gammel tilstand i UI'et.

### 9.2 "Skift til original NU"-knap (runtime B→A, ingen reboot)
Rækkefølgen er kritisk pga. watchdog-racen (§1.2) og fb/UART-ejerskab:
1. **Stop egen aktivitet først:** stop UART-poll/timers og fb-rendering;
   **luk `/dev/ttyS1`-fd'en** (og fb-fd) så stock-A ikke deler UART med os.
2. **Launch stock-A:** `fork()`; i barnet: `setsid()`, `chdir("/customer")`,
   `setenv("LD_LIBRARY_PATH", "/customer/libs:/config/wifi", 1)`,
   stdio → `/dev/null` (eller `/tmp/a_relaunch.log`),
   `execl("/customer/demo_v9", "./demo_v9", NULL)`.
   (argv0 SKAL indeholde `demo_v9` så watchdog-grep'en matcher.)
3. **Vent ~300–500 ms** i parent (A init'er fb/UART), **exit derefter**
   (`_exit(0)`). Under overlappet må ourui IKKE læse UART eller rendere fb
   (fd'er lukket i trin 1). Watchdog'en ser nu A's PID → ingen genstart.
4. **Rør IKKE flaget** (`/customer/new`) — runtime-skift ≠ boot-valg.
   **Dræb ikke demo_v9** — launch + egen exit er nok (existing garanti:
   "ourui dræber aldrig demo_v9" overholdes).
5. Exit frigiver automatisk `EVIOCGRAB` på event0 → switcher'en observerer
   igen og gestus'en (A→B) er straks tilgængelig.
6. Log skiftet (ourui's egen log) med tid + årsag ("bruger-knap").
* Kendt konsekvens (dokumentér i UI_v6.md): boot'ede vi B, er watchdog'en
  `test.sh new_customer` → crash'er A senere, genstartes B. Næste reboot
  følger flaget. Acceptabelt; ingen handling påkrævet.

### 9.3 Generelle krav (uændrede garantier)
* ourui må aldrig skrive til `/customer/demo.sh`, `/customer/demo_v9`,
  `test.sh`, pinit eller libs.
* Config-writes kun via det etablerede v6-mønster (REVERT.md: tmp+rename,
  cfgbak, md5-log) — boot-leverancen læser kun config-md5 som gate.
* Ved B-boot (ourui som `/new_customer/demo_v9`) skal ourui køre uden
  `run.sh`-wrapper (den er fase-2-only; run.sh SIGCONT'er en stock-proces som
  ikke findes ved B-boot). Kør binær'en direkte; exit-koder logges til
  `/tmp/ourui_boot.log` hvis startet af switcher'en.

---

## 10. Lokal verifikation (leverance 4) — beviser

| Check | Resultat | Bevis |
|---|---|---|
| ARM-build, statisk, 0 warnings (`-Wall -Wextra -Werror`) | ✓ | `build.sh`-output; `file switcher tinj_hold` = "statically linked" ARM EABI5 |
| C-self-test (gestus/skalering/mode-logik) | **29/29** | `test-logs/selftest.log` |
| End-to-end fil-mode (positiv + 4 negative streams) | **5/5** | `test-logs/e2e_*.switcher.log` |
| Mock backup (md5+kopier, flag, config-gate) | ✓ | `install.log` + `backups/md5_history.json` |
| Mock install: præcis ÉN guard-linje, korrekt placering, atomisk, idempotent | ✓ | `diff mock_root/customer/demo.sh artifacts/demo.sh` = kun linje 136 tilføjet |
| Mock boot-test (B-boot/A-boot/gestus-runtime/tilbage, simuleret boot + ÆGTE gestus-kode) | ✓ | `install.log`, `test-logs/mock_*` |
| Mock revert-test: demo.sh + vendor-B + config byte-identiske med originaler; /tmp ren | ✓ | uafhængig `diff`/`md5` mod artifacts (kørt 2026-11-17) |
| Mock emergency (genskab demo.sh, kill switcher, A sikret, flag væk) | ✓ | `install.log` |
| ourui-md5 læses ved kørsel (v6.1-fangst) | ✓ | md5 ændredes under forberedelsen; install brugte den nye |

**Resten kræver enheden (online, bruger ved maskinen):** device-trinene i §4
(ægte reboot, fb-captures, gestus på fysisk touch, watchdog-race i praksis,
ourui v6.1 som B-binary). Fables §9-knapper skal være implementeret før
B→A-knappen kan testes; boot-test's tilbage-skift bruger dog kill+launch og
er uafhængig af Fable.

---

## 11. Fil-inventar (denne mappe)

| Fil | Formål |
|---|---|
| `switcher.c` | daemon-kilde (gestus + mode-check + SIGSTOP-først-skift) |
| `switcher` | ARM statisk binær (installeres som `/new_customer/ourui/boot/switcher`) |
| `switcher_selftest`, `switcher_host` | host-builds til lokale tests |
| `tinj_hold.c`, `tinj_hold` | touch-injektor m. long-press + `gesture` (boot-test) |
| `boot_install.py` | trin-script (device + mock-backends, gates, emergency) |
| `build.sh` | alle builds + self-test |
| `test_switcher.py` | end-to-end fil-mode-harness |
| `BOOT.md` | denne fil |
| `backups/` | lokale snapshots: `demo.sh.orig`, `demo.sh.installed`, state/md5-historik |
| `test-logs/` | self-test-, e2e-, mock- og (senere) device-captures/logs |
| `mock_root/` | lokal mock-rod (customer/new_customer/tmp/proc/dev) |
| `install.log` | samlet kørsels-log (alle trin, begge backends) |
| `demo.sh.new` | sidste installerede demo.sh-indhold (md5-bevis) |

*— boot-agent, 2026-11-17. Ingen enhed kontaktet; Fables filer urørt.*
