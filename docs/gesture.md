# GESTUS-FIX v6.12 — simplere center-hold-gestus + grab-bypass + auto raw-range

> **Note (offentlig udgave):** Udviklingsnoter skrevet under udviklingen. Stier som `artifacts/…`, `boot_install.py`-trin og md5-tabeller refererer til forfatterens arbejdsmiljø; som slutbruger skal du bruge `./install.sh` og `./revert.sh` (se [README](../README.md)). Producentens filer er ikke inkluderet; IP-adresser og andre personlige data er fjernet.

**Dato:** 2026-11-18 · **Status:** FORBEREDT LOKALT — **on-device verifikation PENDING**
(maskinen er slukket af brugeren; ingen kontakt med enheden er sket under forberedelsen).
**Ejer:** boot-agenten (`artifacts/ourui/boot/`). Fable's filer (`ourui_v6.c`, `UI_v6.md`,
`build_sim.sh`, `REVERT.md`) er **IKKE ændret**; `sim/mcu_sim.py`, `sim/scenarios/`,
`sim/run_e2e.py` er **IKKE ændrt** (jf. opgaveinstruks).

---

## 1. Rodårsag (FINDINGS.md 'Gestus-debug', 2026-11-18)

Gestus'en A→B triggede **ikke fysisk** på enheden. Diagnose indtil pause:

* Touch-hardware + driver virker (131 IRQs; stock reagerer på knapper efter IC-reset
  via driver-sysfs). Orientering KORREKT (grinder-cirklen øv-venstre åbnede grinder).
* Enhedens ABS_MT raw-range er **0..480** — ikke 4095 som tinj/sim antog.
* `/tmp/switcher.log` har **INGEN event-linjer nogensinde** (kun startup) mens IRQs
  flytter sig → **hypotese: stock `demo_v9` griber `/dev/input/event0 EKSKLUSIVT
  (`EVIOCGRAB`)** → den read-only switcher ser aldrig events → gestus A→B umulig
  mens stock kører og grabber.
* Tidligere "gestus bestået" (INSTALL-REPORT v6.6/v6.11 §6) var sandsynligvis
  **injection mens stock var SIGSTOP'et/ikke-grabbende** — ikke et fysisk bevis.

Konsekvens: gestus-fixet kræver (1) en gestus der kan udføres fysisk uden at ramme
stock-knapper, og (2) en **grab-bypass** så switcher'en ser events mens stock kører.

---

## 2. Ny gestus-specifikation (v6.12)

| Parameter | Værdi |
|---|---|
| Gestus | **ÉN sammenhængende hold ≥ 5000 ms** i skærmens center — ingen multi-press |
| Zone | cirkel om **(240,240)** med radius **40 px** (fingeren skal blive i zonen hele holdet) |
| Trigger | ved **release** efter gyldigt hold (driveren rapporterer kun ændringer; et stille hold giver ingen events undervejs) |
| Cooldown | 10 s efter trigger (uændret) |
| Fallback | `SWITCHER_GESTURE=corner` = legacy 3× long-press ≥1500 ms i x<90,y<90 inden for 6 s (uændret logik) |

**Env-konfiguration** (default = center 5000 ms):

| Env | Default | Effekt |
|---|---|---|
| `SWITCHER_GESTURE` | `center` | `corner` vælger hjørne-fallback |
| `SWITCHER_HOLD_MS` | 5000 (center) / 1500 (corner) | minimum hold-tid |
| `SWITCHER_CENTER_X` / `_Y` | 240 / 240 | gestus-center |
| `SWITCHER_CENTER_R` | **40** | gestus-radius i px (1..240) |

### 2.1 Center er verificeret ikke-interaktivt — men radius 60 ville kollidere

Målt på stock-home-capture (`final_A_stock_v611.png`,
script: `measure_stock_home.py`): de fire aktivitets-knapper
(Grinder/Hot Water/Brew/Steam) er cirkler med center ≈ (160,154), (320,154), (160,326),
(320,326) og radius ≈ 72 px. **Center (240,240) er baggrund** (mellem knapperne) ✓.
Men nærmeste knap-kant ligger kun **≈ 45 px** fra center:

| Knapp-center | Afstand til (240,240) | Kant (r≈72) |
|---|---|---|
| (160,154) | 117,5 px | 45,5 px |
| (320,154) | 117,5 px | 45,5 px |
| (160,326) | 117,5 px | 45,5 px |
| (320,326) | 117,5 px | 45,5 px |

En radius på ~60 px (opgavens oprindelige forslag) ville derfor **overlappe
knappernes tryk-flade**: en finger der driver 50 px mod en knap under holdet ville
udløse stock-knappen (stock modtager ALLE touches, også under gestus'en). Derfor er
**default-radius 40 px** (5 px margin til nærmeste kant) — stadig rigelig tolerance
for et stille hold (typisk finger-jitter ±5-10 px). Radius kan hæves med
`SWITCHER_CENTER_R` hvis brugeren accepterer drift-risikoen. På sleep-skærmen er
center også baggrund (ethvert tryk vækker kun skærmen). Gestus'en bør udføres fra
home/sleep; på andre stock-sider kan center indeholde navigation (harmløst —
aktuatorer startes kun fra de fire knapper med bekræftelse).

---

## 3. Grab-bypass (prioritet a → b → c, auto-fallback i switcher'en)

**(a) Duplikat-node (auto):** switcher'en åbner nu **ALLE** touch-noder
(`event0..9` med `ABS_MT_POSITION_X`, navn TECHWIN/tw31xx foretrækkes) read-only
**samtidigt** og læser fra dem alle (max 4). Findes en duplikat-node som stock ikke
grabber, leverer den events → gestus virker uden indgreb i stock. Begge noder levende
er harmløst: gestus-maskinen er idempotent over for duplikerede down/up, og cooldown
blokerer dobbelt-trigger. `EVIOCGABS` virker også på en grab'et node (grab blokerer
kun event-levering, ikke ioctl) → raw-range detekteres uanset.

**(b) Sysfs-fallback (auto):** tw31xx-driveren eksponerer **`lu_coordinate_x`,
`lu_coordinate_y`, `point_num`** i sysfs (fundet i `tw31xx_new.ko`'s strenge;
`sysfs_create_group` + attribut-navne). Grab påvirker kun evdev-klienter — ikke sysfs.
Switcher'en auto-scanner `/sys/bus/i2c/devices/*/` (override: `SWITCHER_SYSFS_DIR`)
og poller hvert 50 ms. **Blind-detektion:** hvis evdev har leveret 0 events siden
start mens sysfs ser touch (point_num > 0) i ≥3 s, skiftes automatisk til sysfs-kilden
(logges: "evdev BLIND … skifter til sysfs (bypass b)"). Leverer evdev igen events
(fx efter shim + stock-genstart), skiftes tilbage. `SWITCHER_SOURCE=evdev|sysfs`
tvinger kilde; `SWITCHER_SYSFS_SCREEN=1` hvis koordinaterne allerede er pixels
(default: raw i detekteret range — på enheden er raw 0..480 ≈ pixels, så forskellen
er ≤1 px og harmløs for center-zonen).

**(c) LD_PRELOAD-shim (forberedt, anvendes af verify-scriptet hvis a+b fejler):**
`grab_shim.c` → `grab_shim.so` (4500 B, ARM EABI5 hard-float,
**freestanding `-nostdlib`**: ingen DT_NEEDED, ingen UND-symboler — kun rå
EABI-syscalls (ioctl 54, readlink 85) → loader i stock's glibc/armhf-proces uden
libc-afhængighed). Shim'en interposer `ioctl` og returnerer **0 for `EVIOCGRAB`
uden kernel-kald** — men **KUN** i processer hvis `/proc/self/exe ==
/customer/demo_v9` (stock, inkl. watchdog-genstart og runtime-B→A-launch). Alle
andre ioctls og alle andre processer (inkl. ourui i B-mode) får uændret
pass-through → **ourui's eget grab bevares** (ingen fantom-tryk i SIGSTOP'et stock).
Stock tror den grabber, men gør det ikke → read-only switcher ser events.

Export sker i demo.sh's **eksisterende guard-linje** (præcis ÉN linje bevaret):

```sh
[ -x /new_customer/ourui/boot/switcher ] && { [ -f /new_customer/ourui/boot/grab_shim.so ] && export LD_PRELOAD=/new_customer/ourui/boot/grab_shim.so${LD_PRELOAD:+:$LD_PRELOAD}; /new_customer/ourui/boot/switcher & }
```

`-f`-testen gør manglende .so harmløs; export'en arves af `test.sh`-watchdog'en →
også watchdog-genstartet stock får shim'en. `boot_install.py install` opgraderer
automatisk en legacy guard-linje (v6.11) til den nye (md5-gates, stadig ÉN linje).

**Risiko (c):** stock mister KUN eksklusiviteten på event0 — evdev leverer stadig
til stock som før (grab fratager kun *andre* læsere events). Stock's egen
input-adfærd er uændret; ourui's grab i B-mode er uændret. Fejler preload i en
proces, ville glibc afvise processen — derfor loader verify-scriptet shim'en i
`/bin/true` (rc=0-gate) FØR guard-linjen ændres, og .so'en er verificeret selvbaaren
(ingen UND/NEEDED, hard-float ABI som stock's `ld-linux-armhf.so.3`).

---

## 4. Raw-range alignment (screen ↔ raw identisk overalt)

Fælles formel (tinj.c's invers): `screen = (raw−min)·480/(max−min+1)`;
injection: `raw = px·(max−min+1)/480` (floor → ±1 px afrunding, harmløst for zonen).

| Komponent | Range-kilde | Fallback | Status |
|---|---|---|---|
| switcher (device) | `EVIOCGABS` (ST+MT, MT forrang) ved åbning af hver node | **0..480** (device-konstant) | ✅ auto |
| switcher (fil-mode) | `SWITCHER_RAW_MAX` | **0..480** (var 4095) | ✅ device-default; sim-passede streams giver 4095 |
| tinj / tinj_hold | `EVIOCGABS` | **0..480** (var 4095) | ✅ auto + rebuild |
| ourui (device) | `EVIOCGABS` i `inp_open` (allerede til stede, bevist rc=0 på device) | 4095 (kun hvis ALLE ioctl fejler — sker ikke på device) | ✅ uændret kilde (Fable's parity-gate; SIM deklarerer 0..4095) |
| ourui (SIM) | deklareret 0..4095 (SIM-HAL, ingen ioctl) | — | ✅ uændrt |
| sim/proto.py | `touch_raw(px, rmin, rmax)`; **`DEVICE_RAW_MAX = 480`** tilføjet som konstant | default 4095 = SIM's deklarerede range | ✅ sim beholder sit range |

`pick_ranges()` (ren logik) er unit-testet: ingen ioctl-svar → 0..480; MT har forrang
over ST; 4095-svar → 4095 (auto-detektion virker begge veje).

---

## 5. Lokal verifikation (maskine OFF — alt kørt lokalt, 0 warnings)

| Gate | Resultat |
|---|---|
| `boot/build.sh` (ARM statisk switcher/tinj_hold + ARM .so + host-builds, `-Wall -Wextra -Werror`) | ✅ 0 warnings |
| `grab_shim.so` ELF-gate: ingen DT_NEEDED, ingen UND-symboler, kun `ioctl` eksporteret, hard-float | ✅ (build.sh-gate) |
| `switcher_selftest` (center-hold, grænser, drift, corner-fallback, skalering begge ranges, pick_ranges, sysfs-samples, env, mode-logik) | ✅ **50/50** |
| `grab_shim_selftest` (decision-matrix + live ioctl no-op/passthrough) | ✅ **13/13** |
| `test_switcher.py` (fil-mode e2e: center positiv/negativ×3, raw-4095 override, corner positiv/negativ, center-vs-corner, env-override) | ✅ **ALT GRØNT (9 gates)** |
| `boot_install.py` mock: setup/backup/install/boot-test/revert-test (ny mock-rod `mock_root_v612`) | ✅ alle gates |
| Legacy-opgradering: simuleret v6.11-device (demo.sh `53cfde05…` + state) → install opgraderer guard-linjen → **samme installed-md5 `b295aa11…` som fresh install**, præcis ÉN linje, placering gates | ✅ |
| tinj rebuild (ARM statisk) | ✅ |

Beviser: `boot/test-logs/` (selftest.log, v612_*.evdev/.switcher.log), `boot/install.log`,
`boot/mock_root_v612/`.

### MD5 (lokalt bygget 2026-11-18, v6.12)

| Fil | md5 |
|---|---|
| `boot/switcher` (ARM statisk) | `e6bcb460ddeec8504e45d664641cc4b3` |
| `boot/tinj_hold` (ARM statisk) | `9dfb369f3bc6ae697a13225769844591` |
| `boot/grab_shim.so` (ARM .so, 4500 B) | `62c4ae551bb95ccb5c8f213c351a6207` |
| `boot/evprobe` (ARM statisk, uændret kilde) | `beac2fce98108e89b8f7402b54d5f57a` |
| `tinj` (ARM statisk, rebuild m. 480-fallback) | `294650eb40081acd3708aeedadd3918a` |
| `boot/switcher_selftest` (host) | `4f137fdd96200e7a520e7169bed395af` |
| `boot/switcher_host` (host) | `8704444e226699cea5cea6eefc9127d6` |
| `boot/grab_shim_selftest` (host) | `67c88f7b333338741ced5144d5754b13` |
| `boot/demo.sh.new` (vendor-kopi + ny guard-linje) | `b295aa11f2a66593e50ff810a6b402c4` |
| `ourui_v6` (Fable's v6.11, **uændret**) | `725386ccadade594275f20cc998885eb` |

Device-md5'er (v6.11-sluttilstand, til sammenligning ved verify): switcher `6222d8c2…`,
tinj_hold `32d04919…`, demo.sh m. legacy guard `b8e3a47a…`, vendor-original `c1ed3d66…`.

---

## 6. On-device verifikation (NÅR MASKINEN TÆNDES) — PENDING

Kør (bruger ved maskinen, stock A ledig):

```sh
cd artifacts/ourui/boot/ondevice
python3 verify_gesture_v612.py          # --plan viser planen uden device-kontakt
```

Scriptet (`verify_gesture_v612.py`, harness-mønstre
fra net_heal_proof_v611: korte kommandoer, push'ede scripts, md5-round-trips):

1. **Preflight:** stock A exe-link + config-baseline-md5; brugeren bekræfter ledig maskine.
2. **Push:** switcher/tinj_hold/grab_shim.so/evprobe → `/new_customer/ourui/boot/` (md5-gates).
3. **Node-inventar:** `evprobe info` → touch-noder + raw-ranges (forventet 0..480).
4. **Grab-diagnose:** `evprobe cap` på alle touch-noder samtidigt (6 s) mens brugeren
   rører skærmen → hvilke noder leverer events mens stock kører?
5. **Sysfs-probe:** `point_num/lu_coordinate_x/y` samples (24×250 ms) mens brugeren
   holder center → live?
6. **Bypass-valg i prioritet:** (a) duplikat/ugrebet node leverer → switcher auto;
   (b) sysfs live → switcher auto-fallback; (c) ellers shim: `/bin/true`-load-test →
   guard-linje opgraderes atomisk (backup `demo.sh.bak.v611`, md5-gates, ÉN linje) →
   ét kontrolleret `kill` af stock (watchdog genstarter MED shim-env; fallback-launch
   efter 20 s) → `/proc/<pid>/maps` skal vise `grab_shim.so`.
7. **Switcher-genstart** (ny binær); startup-log skal vise CENTER-hold + kilder.
8. **BRUGEREN holder center ≥5 s** → A→B verificeres: `GESTUS UDLØST` + `skift A→B
   FÆRDIG` i switcher.log + exe-link `/new_customer/demo_v9` (ingen reboot).
9. **BRUGEREN trykker SWITCH NOW** (tandhjul → SYSTEM → SKIFT TIL ORIGINAL NU →
   SWITCH NOW, fysisk) → B→A verificeres: exe-link `/customer/demo_v9` + boot_ui.log.
10. **Slut-gates:** config-md5 urørt, flag væk, switcher kører, `/tmp/ourui_g` ryddet.
11. Skriver `ondevice/gestus_v612/{result.json,GESTUS-VERIFY-v612.md,telnet_transcript.txt}`
    og opdaterer markør-sektionen nedenfor.

Ingen reboot, ingen writes til config/flag/UART/fb; device-writes kun boot-binærer,
`/tmp/ourui_g` og (kun ved bypass c) demo.sh + ét stock-kill med watchdog-fallback.

<!-- ONDEVICE-RESULT:BEGIN -->
**Status:** VERIFIED · **Bypass:** a_enkelt_node_ugrebet · **Dato:** 2026-11-18

On-device verifikation: ALLE GATES GRØNNE (`GESTUS-VERIFY-v612.md`, `result.json`).
<!-- ONDEVICE-RESULT:END -->

---

## 7. REVERT (fortryd grab-bypass / gestus-fix)

**Bypass a/b (ingen demo.sh-ændring):**
```sh
kill $(pidof switcher) 2>/dev/null
rm -f /new_customer/ourui/boot/switcher /new_customer/ourui/boot/grab_shim.so
# (guard-linjen er harmløs uden binary: [ -x … ]-gaten; legacy-linjen uden shim-export
#  hvis den blev opgraderet: se nedenfor)
kill $(pidof demo_v9)     # watchdog genstarter stock uden preload (kun nødvendigt ved c)
```

**Bypass c (shim + guard-linje):**
```sh
# 1. demo.sh tilbage til v6.11-tilstand (legacy guard, UDEN shim-export):
cp /new_customer/ourui/boot/demo.sh.bak.v611 /customer/.demo_sh.tmp
md5sum /customer/.demo_sh.tmp     # skal være b8e3a47a7cc3f222066154ce82f85ee9
mv /customer/.demo_sh.tmp /customer/demo.sh && chmod 755 /customer/demo.sh && sync
# 2. shim væk + switcher væk:
rm -f /new_customer/ourui/boot/grab_shim.so
kill $(pidof switcher) 2>/dev/null
# 3. stock genstartes uden LD_PRELOAD (watchdog arver demo.sh's env):
kill $(pidof demo_v9)             # eller reboot
```

**Fuld revert (alt boot-persistens væk, byte-identisk vendor-tilstand):**
`python3 boot_install.py revert-test` (BOOT.md §5.1) — genskaber demo.sh fra
`demo.sh.bak` (vendor-original `c1ed3d66…`), dræber switcher, fjerner flag, reboot,
genskaber vendor-B. Herefter er hverken guard-linje, shim eller switcher til stede.

**Kun gestus'en tilbage til legacy hjørne (behold install):** start switcher med
`SWITCHER_GESTURE=corner` (env i guard-linjen eller manuel start) — center-gestus'en
er default, men corner-logikken er uændret og testet.

---

## 8. Sim-påvirkning + pending til sim-ejeren

* `sim/play.py`: gestus-API/smoke opdateret til v6.12 (center-hold 5,2 s + corner-
  fallback som to checks; `SWITCHER_RAW_MAX=4095`/`SWITCHER_GESTURE` sættes midlertidigt
  om `run_e2e.run_gesture`, som arver `os.environ`).
* `sim/proto.py`: `DEVICE_RAW_MAX = 480` tilføjet; `touch_raw`/`touch_stream` beholder
  SIM's deklarerede 0..4095 (uændret adfærd).
* **`sim/run_e2e.py` + `sim/scenarios/` er IKKE ændrt** (instruks). Deres gestus-trin
  (`run_gesture`: 3×1,6 s i (75,75), raw 4095, ingen env) forventer den GAMLE default
  og vil fejle mod v6.12-switcher'en. **Pending til sim-ejeren:** enten sæt
  `SWITCHER_GESTURE=corner` + `SWITCHER_RAW_MAX=4095` i `run_gesture`'s env (ét linje-
  fix) eller skriv scenarierne om til center-hold (1×5200 ms i (240,240)). Indtil da
  er gestus-scenarierne kendt-røde mod v6.12; resten af suiten er upåvirket (UI/
  protokol-ruter bruger SIM's eget 4095-range).
* `ourui_v6.c` er ikke ændret: device-pathen auto-detekterer allerede raw-range via
  `EVIOCGABS` (bevist på enheden), og Fable's `build_sim.sh`-parity-gate ville flagge
  ethvert indgreb. 4095-fallback'en der rammer kun hvis alle fire ioctl'er fejler
  (sker ikke på device). Næste UI-runde kan eventuelt sætte fallback'en til 480.

---

## 9. Leverance-filer (v6.12)

| Fil | Indhold |
|---|---|
| `boot/switcher.c` + `boot/switcher` | center-hold-gestus + multi-node (a) + sysfs-fallback (b) + auto raw-range |
| `boot/grab_shim.c` + `boot/grab_shim.so` + `boot/grab_shim_selftest` | LD_PRELOAD-bypass (c), freestanding ARM .so |
| `boot/tinj_hold.c` + `boot/tinj_hold`, `tinj.c` + `tinj` | 480-fallback + `gesture center/corner` |
| `boot/build.sh`, `boot/test_switcher.py` | builds + lokale gates (50+13+9) |
| `boot/boot_install.py`, `boot/demo.sh.new` | ny guard-linje (ÉN linje, shim-export) + legacy-opgradering |
| `boot/measure_stock_home.py` | geometri-måling af stock-home (radius-begrundelse) |
| `boot/ondevice/verify_gesture_v612.py` | on-device verify (køres når maskinen tændes) |
| `boot/GESTUS-FIX.md` | dette dokument |
| `boot/mock_root_v612/`, `boot/test-logs/v612_*` | lokale beviser |

*Boot-agenten, 2026-11-18. Maskinen står slukket i stock A (v6.11 installeret i B);
v6.12 er bygget og testet lokalt og venter på on-device verify.*
