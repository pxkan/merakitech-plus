# REVERT — Meraki+ / ourui (fase 2 soft-replace)

> **Note (offentlig udgave):** Udviklingsnoter skrevet under udviklingen. Stier som `artifacts/…`, `boot_install.py`-trin og md5-tabeller refererer til forfatterens arbejdsmiljø; som slutbruger skal du bruge `./install.sh` og `./revert.sh` (se [README](../../README.md)). Producentens filer er ikke inkluderet; IP-adresser og andre personlige data er fjernet.

> **Navn (v6.3):** UI'et hedder **Meraki+** på skærmen. `ourui` er fortsat kode-navnet — binær, proces, mappen `/new_customer/ourui/` og alle kommandoer herunder er uændrede. UI-tekster citeres på dansk (UI-SPROG = DANSK); en frisk installation starter på engelsk, og den engelske ordlyd står i UI_v6.md, Appendiks A. v6.3's eneste nye fil er sprog-filen — se [sidste afsnit](#v63-sprog-filen-lang--hvad-der-er-nyt-og-hvordan-det-fortrydes).

Normal revert (**v6.8, uændret i v6.9 og v6.10**): SYSTEM → **SKIFT TIL ORIGINAL NU** → **SKIFT NU** på Meraki+-skærmen. I run.sh-tilstand er det en ren
exit (efter exit-påmindelsen, hvis config er gemt) → `run.sh`'s trap SIGCONT'er demo_v9 → original UI tilbage på <1 s. Ingen reboot.
I slot B-boot (intet pauset stock-UI) starter samme knap stock-UI'et. **REVERT-knappen på HJEM findes ikke længere fra v6.8**
(den var en fase-2-sikring; se [v6.8-afsnittet](#v68-revert-knappen-på-hjem-er-fjernet)). Til og med v6.7: tryk **REVERT** på HJEM.

Manuel revert (telnet root@<display-ip>):
```sh
kill -CONT $(pidof demo_v9)     # original UI må gerne køre igen
kill $(pidof ourui) 2>/dev/null # hvis ourui hænger (run.sh-trap gør det også)
```
Hvis run.sh selv er dræbt uden trap: `kill -CONT $(pidof demo_v9)` alene er nok.

Garantier (v4, v6, v6.1, v6.2, v6.3, v6.4, v6.5, v6.6, v6.7, v6.8, v6.9 og v6.10 — v6.9 ændrer kun det, der tegnes ([afsnittet](#v69-rolig-graf-og-stabil-vægt--intet-nyt-på-enheden-ud-over-visningen)), og v6.10 kun hvordan trykket vises ([sidste afsnit](#v610-tryk-som-bar-estimat--kun-visning)); v6.2-tilføjelserne og v6.3's sprog-fil står i deres egne afsnit; v6.4 ændrer intet på enheden; v6.5's boot-push står i [sit afsnit](#v65-boot-push-ved-opstart--hvad-der-er-nyt-og-hvordan-det-fortrydes); v6.6's re-push i [sit afsnit](#v66-re-push-efter-mcu-genstart-64-bit-ur-damp-visning--hvad-der-er-nyt-og-hvordan-det-fortrydes); v6.7's net-vagt og v6.8's fjernede REVERT-knap i de to sidste afsnit):
* demo_v9-binary, libs, pinit, test.sh er IKKE ændret.
* ourui ligger i /new_customer/ourui (separat mappe). Med boot-integrationen (boot/BOOT.md) ligger samme binær desuden som /new_customer/demo_v9 — lagt der af `boot_install.py`, ikke af ourui.
* MCU modtager kun normal protokol (cmd3 poll / cmd6 write / cmd9 svar); ingen OTA.
* **v6.1:** hver cmd6-frame passerer en frame-politik (UI_v6.md §7a). ourui **kan ikke** sende
  ord 26/27 ≠ 0 (vægt-kalibrering, skriver MCU-flash), ord 30 ≠ 0 (MCU-reboot), drain (28/29),
  back-flush (20), self-test (8), sleep/drain (ord1 = 1) eller power-off (ord0 = 0). Der er
  derfor ingen MCU-flash-tilstand at reverte efter ourui.
* Watchdog (test.sh) checker kun demo_v9-PID-eksistens; SIGSTOP = PID findes → ingen genstart.
* ourui dræber aldrig demo_v9 — og fra v6.2 er det bredere: ourui sender ikke et signal til nogen proces overhovedet (0 `kill()`-kald i kilden). **Gælder fortsat i v6.7:** net-vagten dræber aldrig en wpa_supplicant (heller ikke et init-script, der hænger).
* **v6.7:** når wifi-stakken er NEDE (ingen wpa_supplicant), starter ourui den, som stock-UI'et gør: `/config/wifi/ssw01bInit.sh` (kun hvis `wlan0` mangler), `/config/wifi/wpa_supplicant -i wlan0 -Dnl80211 -c /appconfigs/wpa_supplicant.conf` og `udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1`. Den eneste fil, der kan skrives, er `/appconfigs/wpa_supplicant.conf` — kun ved en bring-up og kun hvis indholdet afviger fra det, config.json giver (stock skriver samme fil i samme format). Log: `/tmp/ourui_net.log`. En sund stak røres ikke. `OURUI_NONET=1` slår det fra.
* **v6.2:** ourui kan oprette/fjerne boot-flaget `/customer/new` og kan starte `/customer/demo_v9`. Begge dele sker kun fra to rækker nederst på SYSTEM, hver med bekræftelse.
* **v6.3:** eneste nye skrivning er sprog-filen `/new_customer/ourui/lang` (2 bytes), og kun ved GEM på UI-SPROG. Stock-UI'ets `config.json` (herunder `language`) røres ikke af den, og der sendes ingen frame til MCU'en.
* **v6.5:** ved opstart sender ourui stock-UI'ets to boot-push-blokke (`cmd6 sub1 len30` + `sub112 len3`) med værdierne fra `config.json`. Det er skrivninger til controllerens **RAM** gennem samme frame-politik som alt andet; der skrives ingen fil, og der er intet at reverte bagefter.

Fjern installation helt:
```sh
rm -rf /new_customer/ourui      # OBS v6: gem cfgbak/ først, hvis config er ændret (se nedenfor); fjerner også sprog-filen lang (v6.3)
```

---

## v6: persistente writes — hvad der er nyt

**v4 skrev aldrig til disk. v6 gør, når brugeren trykker GEM i settings.** Det er de eneste
persistente ændringer, og de rammer kun disse stier:

| Sti | Hvad | Hvornår |
|---|---|---|
| `/customer/config.json` | ét tal-felt udskiftet (atomisk tmp+rename) | hver GEM |
| `/customer/.ourui_cfg.tmp` | midlertidig; findes kun i millisekunder | under GEM |
| `/new_customer/ourui/cfgbak/config.json.orig` | **pristine kopi** fra før ourui's allerførste write; overskrives aldrig | første GEM |
| `/new_customer/ourui/cfgbak/config.json.bak` | versionen umiddelbart før seneste write | hver GEM |
| `/new_customer/ourui/cfgbak/config_writes.log` | én linje pr. write: tid, felt, gammel→ny, md5 før/efter | hver GEM |
| `/new_customer/ourui/cfgbak/RESTART_NEEDED` | flag: stock-UI har endnu ikke læst den nye config | hver GEM |

Derudover ændrer v6 tilstand, som **ikke** er på disk:
* **MCU-registre** (ord 2, 4, 11, 14, 17, 23, 25, 112–114) ved GEM — virker straks i controlleren.
  De er RAM i MCU'en og genskabes fra `config.json`, hver gang demo_v9 starter: `fuart_thread`
  boot-pusher `cmd6 sub1 len30` + `cmd6 sub112 len3` (PARITY.md §1.4). Efter en demo_v9-genstart
  er controlleren altså lig med config — uanset hvad ourui har skrevet i mellemtiden.
* **Backlight-PWM** (`/sys/class/pwm/pwmchip0/pwm2/duty_cycle`) under LYSSTYRKE-redigering.
  ANNULLER og exit gendanner den gemte værdi.
* **Input-greb** (`EVIOCGRAB` på `/dev/input/event0`). Frigives automatisk, når ourui-processen
  dør — også ved `kill -9`.

### Se hvad ourui har ændret
```sh
cat /new_customer/ourui/cfgbak/config_writes.log
md5sum /customer/config.json /new_customer/ourui/cfgbak/config.json.orig
```
Ens md5 = config er tilbage på udgangspunktet (kendt baseline: `<config-md5-redacted>`).

### Gendan config (fuld revert af alle settings-ændringer)
Kør mens ourui **ikke** kører. Samme tmp+mv-mønster som WRITETEST.md §7 — aldrig in-place:
```sh
cd /new_customer/ourui/cfgbak
md5sum config.json.orig                       # bekræft at det er den forventede baseline
cp config.json.orig /customer/.restore.tmp
md5sum /customer/.restore.tmp                 # skal matche linjen ovenfor
mv /customer/.restore.tmp /customer/config.json
kill $(pidof demo_v9)                         # watchdog genstarter; læser config + boot-pusher til MCU
sleep 5; pidof demo_v9; md5sum /customer/config.json
rm -f RESTART_NEEDED
```
Kun ét skridt tilbage: brug `config.json.bak` i stedet for `config.json.orig`.

Findes `cfgbak/` ikke, har v6 aldrig skrevet config. Er mappen væk, selv om config er ændret:
brug den lokale kopi `artifacts/write_test/config.json.baseline` (md5 `<config-md5>…`), pushet som
i WRITETEST.md §3.

### Efter en GEM: genstart stock-UI'et, før det bruges (ikke en revert)
demo_v9 læser `config.json` **kun ved opstart**. Efter REVERT (SIGCONT) kører det videre på sit
gamle RAM-spejl, og det har tre følger, indtil det genstartes:
1. Stock-UI'et **viser** de gamle værdier.
2. Dets næste blok-write (fx bryg-start, som medsender bryg-temp, damp-niveau og vand-vægt)
   sender de **gamle** værdier til controlleren og overskriver dem, ourui satte.
3. Ændrer man en indstilling i stock-UI'et, skriver det hele sit gamle config-træ — ourui's
   ændringer i filen kan gå tabt, eller stock's egne kan (dets write-fd peger på den gamle inode).

Derfor, når `cfgbak/RESTART_NEEDED` findes. Fra v6.2 viser ourui påmindelsen **på skærmen ved REVERT** (*"STOCK-UI KRÆVER GENSTART AF demo_v9 FOR NYE VÆRDIER"* + kommandoen) og på stdout ved exit — men kun når den er sand: der kører et stock-UI, som blev startet før seneste GEM i samme boot. Efter en maskin-genstart, efter en demo_v9-genstart og i slot B-boot (hvor intet stock-UI er pauset) vises den ikke, selv om filen ligger der:
```sh
kill $(pidof demo_v9) && rm -f /new_customer/ourui/cfgbak/RESTART_NEEDED
sleep 5; pidof demo_v9          # watchdog har startet den igen
```
Ved opstarten læser demo_v9 den nye config og boot-pusher den til controlleren, så config,
stock-UI og controller er i sync. Bemærk OTA-forbeholdet i WRITETEST.md §7: en genstart kan
udløse vendor-OTA, hvis der er kommet en ny version.

Gælder også **STOCK-SPROG** (redigerbar fra v6.1; hed SPROG før v6.3): feltet påvirker kun stock-UI'et og først efter
genstart. Står stock-UI'et på et sprog, man ikke kan læse, så sæt STOCK-SPROG tilbage til ENGELSK i
ourui (SYSTEM-siden) eller gendan config som ovenfor, og genstart demo_v9. Meraki+'s eget sprog er en
anden indstilling (UI-SPROG, v6.3) og ligger ikke i config — se sidste afsnit.

### Hvis en GEM blev afbrudt (strøm/crash midt i)
* `rename()` er atomisk: `config.json` er enten den gamle eller den nye, aldrig halv.
* En efterladt `/customer/.ourui_cfg.tmp` er harmløs: `rm -f /customer/.ourui_cfg.tmp`.
* MCU kan have fået den nye værdi, mens config har den gamle. Løses af en demo_v9-genstart
  (boot-pushen sætter controlleren lig config) eller ved at gemme parameteren igen i ourui.

### Tilbage til v4 (monitor-only, ingen writes)
Afhænger af hvordan integrations-ingeniøren installerer v6; anbefalet layout er v6 som `ourui`
og v4 bevaret som `ourui_v4.bin`:
```sh
cd /new_customer/ourui && cp ourui_v4.bin ourui
```
Bemærk at v4 har keep-awake-buggen (skriver ord1=1 = sleep/drain).

### Tilbage til v6.0 (før spec-afstemningen)
Frarådes: v6.0's grænser var antagelser (bl.a. bryg-temp 80–98 °C og gruppe-temp op til 90 °C mod
spec'ens 85–96 / 30–80), `timemode` var vendt om, og der var ingen frame-politik. v6.0-binæren
havde md5 `90bcf094253e69f0559051339d2032f5`; v6.1 har `4d927802cbeb69cc949f463021b29b99`.
En config gemt af v6.0 med en værdi uden for spec vises rødt i v6.1 og spærrer aktivitets-start,
indtil værdien er rettet (eller config er gendannet fra `config.json.orig`).

---

## v6.2: BOOT-UI-flag og runtime-skift — hvad der er nyt, og hvordan det fortrydes

v6.2 kan to nye ting fra SYSTEM-siden (UI_v6.md, CHANGES-v6.2; krav fra `boot/BOOT.md` §9). Selve boot-installationen (switcher-daemonen, guard-linjen i `demo.sh`, ourui som `/new_customer/demo_v9`) er kollegaens og fortrydes efter **BOOT.md §5** — ikke her.

### Hvad v6.2 rører

| Sti / tilstand | Hvad | Hvornår |
|---|---|---|
| `/customer/new` | vendor'ens boot-flag: findes = boot slot B, findes ikke = boot slot A. Oprettes (tom fil) eller fjernes, efterfulgt af `sync()` | **kun** ved GEM på BOOT-UI — og kun når `/new_customer` er mountet og dens `demo_v9` er ourui |
| `/new_customer/ourui/cfgbak/boot_ui.log` | én linje pr. flag-ændring og pr. runtime-skift (tid, hvad, årsag) | ved de to handlinger |
| `/new_customer/ourui/cfgbak/RESTART_NEEDED` | har fået en sidste linje `boot_id=… mono_s=…` (bruges til at afgøre, om påmindelsen stadig gælder); fjernes af ourui efter et vellykket runtime-skift | hver GEM / ved skift |
| en ny proces `/customer/demo_v9` | startet af ourui med `fork` + `setsid` + `execl`, cwd `/customer`, stdio `/dev/null` | **kun** ved SKIFT TIL ORIGINAL NU, når der ikke allerede findes et stock-UI |

v6.2 skriver **intet** i `/tmp`, rører hverken `demo.sh`, `test.sh`, `demo_v9`-binærerne, pinit eller libs, og **sender ikke et signal til nogen proces** (kilden har 0 `kill()`-kald; `round_v6/boot_check.txt`). Der startes ingen hjælper-proces: det, ourui forker, *er* stock-UI'et fra første instruktion efter `execl`. Der er derfor **ingen hjælper at dræbe**.

### Se tilstanden
```sh
ls -l /customer/new 2>/dev/null || echo "intet flag = slot A (original) ved naeste boot"
for p in $(pidof demo_v9); do echo "$p $(readlink /proc/$p/exe)"; done   # /customer/… = stock, /new_customer/… = ourui
pidof switcher; tail -5 /tmp/switcher.log
cat /new_customer/ourui/cfgbak/boot_ui.log
```
Det samme står på ourui's DIAGNOSE-side (scroll ned til *BOOT · SLOT · SWITCHER*).

### Fortryd BOOT-UI-valget (gælder først ved næste genstart)
I ourui: SYSTEM → BOOT-UI → vælg det andet → GEM. Eller fra telnet — det er præcis det samme, ourui gør:
```sh
rm -f /customer/new && sync      # original (slot A) ved naeste boot
touch /customer/new && sync      # ourui (slot B) ved naeste boot — KUN hvis /new_customer/demo_v9 er ourui
```
Flaget er hele persistensen; der er intet andet at rydde op. Bemærk BOOT.md §7: en vendor-OTA sætter eller fjerner selv flaget og kan lægge sin egen binær i slot B. ourui opdager det (BOOT-UI viser *"LÅST · SLOT B ER IKKE MERAKI+"*) og rører så ikke flaget.

### Fortryd et runtime-skift (ourui → stock, uden genstart)
Skiftet ændrer ingen filer. Tilbage til ourui:
* **Gestus:** 3 lange tryk (≥ 1,5 s) øverst til venstre inden for 6 s — kræver at switcher-daemonen kører (BOOT.md §2).
* **Genstart:** maskinen følger flaget.
* **Telnet, bootet i slot B:** `kill $(pidof demo_v9)` — watchdog'en er `test.sh new_customer` og starter ourui igen inden for ca. 1 s.
* **Telnet, bootet i slot A:** det er retningen stock → ourui; se nedenfor.

### Hvis et skift går galt
ourui afslutter kun, hvis stock-processen stadig lever 400 ms efter start. Dør den inden da, tager ourui skærmen igen og viser *"SKIFT FEJLEDE"*. Dør den senere, starter watchdog'en boot-slottets binær (ourui ved B-boot). Skulle noget alligevel stå forkert:
```sh
for p in $(pidof demo_v9); do echo "$p $(readlink /proc/$p/exe)"; done
# to UI'er paa én gang (maa ikke ske): afslut den, der ikke skal koere — ourui afslutter paent paa SIGTERM
kill <pid for /new_customer/demo_v9>
# intet UI og ingen watchdog-genstart efter et par sekunder: start stock i haanden (som demo.sh's A-gren)
cd /customer && export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/customer/libs:/config/wifi && ./demo_v9 &
```
Touch i stock-UI'et virker ikke efter et skift: en proces holder stadig `EVIOCGRAB`. `ls -l /proc/*/fd 2>/dev/null | grep event0` viser hvem; v6.2 lukker alle sine fds før `execl`, så det bør kun kunne være en hængende ourui.

### Den omvendte vej til tests (stock → ourui uden genstart) — ikke en del af ourui
Det gør switcher-daemonen ved gestus (BOOT.md §3.2), og `boot_install.py boot-test` trin c afprøver det med `tinj_hold`. I hånden er det samme mønster, **stop-først** så watchdog'en aldrig ser et hul:
```sh
A=$(pidof demo_v9)                      # skal vaere praecis én pid, og dens exe skal vaere /customer/demo_v9
kill -STOP $A                           # watchdog'en ser stadig pid'en
cd /new_customer && LD_LIBRARY_PATH=/new_customer/libs:/config/wifi ./demo_v9 >>/tmp/ourui_boot.log 2>&1 &
B=$!; sleep 1; readlink /proc/$B/exe    # skal vise /new_customer/demo_v9
kill -9 $A                              # foerst nu; kom B ikke op:  kill -CONT $A
```
Kør ikke `run.sh` i slot B-boot: den forudsætter et stock-UI at pause (BOOT.md §9.3).

### Fjern alt hvad v6.2 har lagt
```sh
rm -f /customer/new && sync                         # kun hvis flaget er sat af os (se boot_ui.log)
rm -f /new_customer/ourui/cfgbak/boot_ui.log
```
Fuld tilbagerulning af boot-integrationen (guard-linje, switcher, vendor-binær tilbage i slot B): BOOT.md §5.

### Tilbage til v6.1
`artifacts/ourui/ourui_v6_1.bin` (md5 `4d927802cbeb69cc949f463021b29b99`, kilde `ourui_v6_1.c`) er v6.1 uændret. `boot_install.py install` uploader `artifacts/ourui/ourui_v6` og læser md5 ved kørsel, så tilbagerulning er `cp ourui_v6_1.bin ourui_v6` lokalt og derefter `install`. Bemærk hvad man mister i slot B-boot: v6.1 har ingen SKIFT TIL ORIGINAL NU, og dens REVERT afslutter blot — hvorefter watchdog'en starter den igen. v6.2-binæren har md5 `0bb93da0ed0824c424f1e16530f07022`.

---

## v6.3: sprog-filen `lang` — hvad der er nyt, og hvordan det fortrydes

v6.3 giver UI'et et produktnavn (Meraki+) og to sprog (UI_v6.md, CHANGES-v6.3). Det eneste nye på disk er én lille fil.

### Hvad v6.3 rører

| Sti | Hvad | Hvornår |
|---|---|---|
| `/new_customer/ourui/lang` | UI-sproget som ren tekst: præcis `en` eller `da` (2 bytes, intet linjeskift) | **kun** ved GEM på SYSTEM → UI-SPROG |
| `/new_customer/ourui/lang.tmp` | midlertidig; findes kun i millisekunder (tmp → fsync → genlæs → `rename()`) | under samme GEM |

v6.3 skriver **ikke** til `/customer/config.json` for sprogets skyld (`language` dér er stock-UI'ets eget felt og er uafhængigt), sender ingen cmd6-frame, laver ingen `cfgbak/`-backup og intet `RESTART_NEEDED`. Filen læses én gang ved opstart. Mangler den, kan den ikke læses, eller står der andet end `en`/`da`, starter UI'et på **engelsk**.

### Se og skift sproget fra telnet
```sh
cat /new_customer/ourui/lang 2>/dev/null || echo "(ingen fil = engelsk)"
echo da > /new_customer/ourui/lang     # dansk fra naeste start (linjeskiftet taales)
echo en > /new_customer/ourui/lang     # engelsk fra naeste start
```
Et kørende ourui læser ikke filen igen; skift i UI'et (SYSTEM → UI-SPROG → −/+ → GEM) slår igennem med det samme uden genstart. DIAGNOSE nederst (*OM*) viser det aktive sprog og kilden: *FRA FIL*, *STANDARD (INGEN FIL)*, *UKENDT VÆRDI I FIL* eller *IKKE GEMT*.

### Fortryd (tilbage til default)
```sh
rm -f /new_customer/ourui/lang /new_customer/ourui/lang.tmp     # engelsk ved naeste start
```
Der er intet andet at rydde op. En efterladt `lang.tmp` (strøm væk midt i GEM) er harmløs; `lang` er enten den gamle eller den nye, aldrig halv.

### Hvis UI'et står på et sprog, man ikke kan læse
UI-SPROG er **øverste række på SYSTEM** (side 4 af 5: swipe tre gange mod venstre fra HJEM), og de to valg hedder altid *ENGLISH* og *DANSK*. Eller fra telnet: `rm -f /new_customer/ourui/lang` og start ourui igen.

### Miljø-variabel
`OURUI_LANGFILE=<fil>` peger sprog-filen et andet sted hen (test / simulator). Default er stien ovenfor.

### Tilbage til v6.2
`artifacts/ourui/ourui_v6_2.bin` (md5 `0bb93da0ed0824c424f1e16530f07022`, kilde `ourui_v6_2.c`) er v6.2 uændret. Tilbagerulning som for v6.1: `cp ourui_v6_2.bin ourui_v6` lokalt og derefter `boot_install.py install` (den læser md5 ved kørsel). v6.2 kender ikke sprog-filen og lader den ligge; den er dansk-sproget og viser navnet OURUI. v6.3-binæren har md5 `5815176d0e766c7a6532b88014e932fb`.

## v6.4: SIM-build — intet nyt på enheden

v6.4 gør UI'et kørbart uden hardware mod test-agentens simulerede MCU (UI_v6.md §13 + CHANGES-v6.4). **Enheds-binæren gør det samme som v6.3.** Kildeteksten efter preprocessoren er token for token den samme bortset fra to ting, begge kun tekst:

* versions-strengen `V6.4` (splash, DIAGNOSE, binær-mærket `@(#)ourui V6.4 …` — BOOT-UI-vagten genkender fortsat alle versioner på de første 10 tegn);
* toasten ved GEM uden ACK: *GEM FEJLET · INGEN ACK · INTET GEMT* (før: *INGEN ACK · INTET GEMT*).

Det betyder for revert:

* **Ingen nye filer, stier, writes eller miljø-variabler på enheden.** Alt i afsnittene ovenfor gælder uændret. `OURUI_SIM_*`-variablerne findes kun i SIM-build'et; enheds-binæren indeholder dem ikke (`strings ourui_v6 | grep -c OURUI_SIM` = 0).
* **Boot-pushen** (`cmd6 sub1 len30` + `sub112 len3`), som SIM-build'et sender, fordi test-kontrakten kræver den, er **ikke** i enheds-binæren. Efter en ourui-GEM gælder derfor stadig: genstart stock-UI'et, før det bruges (afsnittet *Efter en GEM*).
* SIM-binærerne `ourui_v6_sim` (vært) og `ourui_v6_sim_arm` (statisk ARM) skal **ikke** installeres på enheden. `ourui_v6_sim_arm` ville afslutte med exit 2, fordi `OURUI_SIM_ROOT` mangler — den åbner aldrig `/dev/fb0`, `/dev/ttyS1` eller `/dev/input/event0`.
* E2E-kørslerne skriver kun i `sim/reports/…` (sandkasser) og en kort `/tmp`-mappe til sockets. `artifacts/config.json` er urørt (md5 `<config-md5-redacted>`).

### Tilbage til v6.3
`artifacts/ourui/ourui_v6_3.bin` (md5 `5815176d0e766c7a6532b88014e932fb`, kilde `ourui_v6_3.c`, md5 `4ac62cd08de36f7fea026eec5a3494ab`) er v6.3 uændret. Tilbagerulning som for de tidligere versioner: `cp ourui_v6_3.bin ourui_v6` lokalt og derefter `boot_install.py install`. v6.4-binæren har md5 `1fe0b5b8c3b24abf0cadcde7b099076a`. Sprog-filen og `cfgbak/` er de samme i v6.3 og v6.4.

## v6.5: boot-push ved opstart — hvad der er nyt, og hvordan det fortrydes

v6.5 ændrer to ting i enheds-binæren (UI_v6.md CHANGES-v6.5):

1. **Boot-push.** Når ourui starter — i slot B-boot og i run.sh-tilstand — sender den efter PowerOn og keep-awake de to blokke, stock-UI'et sender ved sin egen opstart: `cmd6 sub1 len30` (ord 1..30) og `cmd6 sub112 len3` (ord 112..114), med værdierne fra `/customer/config.json`. Kører en aktivitet allerede, når ourui starter, springes den over.
2. **RX-resync-watchdog.** Ren modtage-logik; sender intet og skriver intet.

### Hvad boot-pushen rører

| | |
|---|---|
| Filer | **ingen** — hverken `config.json`, `cfgbak/`, flag eller sprog-fil |
| Controlleren | registerfilen i **RAM** (ord 1..30 og 112..114). Den nulstilles i forvejen ved hver strøm-cyklus, og stock-UI'et overskriver den med sin egen boot-push, hver gang det starter |
| MCU-flash | intet. Ord 26/27 (kalibrering) og 30 (reboot) er 0 i blokken og kan ikke være andet (frame-politikken) |
| Stock-UI'ets RAM-spejl | røres ikke. Reglen *Efter en GEM: genstart stock-UI'et, før det bruges* gælder uændret |

Der er derfor **ingen tilstand at reverte** efter en boot-push. Vil man have controlleren tilbage på det, stock-UI'et selv ville sætte: genstart stock-UI'et (`kill $(pidof demo_v9)` — watchdog'en starter det, og dets boot-push skriver de samme ord). Med uændret config er det de samme værdier.

To ting, boot-pushen gør i run.sh-tilstand, som ourui v6.4 ikke gjorde:

* Opskrifts-ord, stock-UI'et måtte have sat fra et NFC-kort eller et preset (ord 13, 15, 18), går tilbage til boot-værdierne (60 / 60 / 0) — som ved en genstart af stock-UI'et.
* Ord 113's lo-byte får igen `breakTime`; controlleren overskriver den selv ved næste bryg.

### Se hvad den gjorde
DIAGNOSE, scrollet én række ned: **BOOT-PUSH 1-30** og **BOOT-PUSH 112-114** viser OK · INGEN ACK · AFVIST (config uden for spec) · INGEN CONFIG · SPRUNGET OVER (en aktivitet kørte ved opstart) · FRA. `CMD6 OK` er 4 lige efter en normal start (PowerOn, vågen, to blokke). Boot-pushen skriver ingen log-fil.

### Slå den fra uden at skifte binær
```sh
OURUI_NOBOOTPUSH=1 ./run.sh          # v6.4-adfærden: ingen boot-push; DIAGNOSE viser FRA
OURUI_READONLY=1 ./run.sh            # som hidtil: ingen cmd6 overhovedet (heller ikke keep-awake)
```
I slot B-boot starter `demo.sh` binæren uden miljø-variabler; dér slås boot-pushen fra ved at lægge v6.4-binæren ind (nedenfor) — eller ved at boote stock (`rm /customer/new`).

### Tilbage til v6.4
`artifacts/ourui/ourui_v6_4.bin` (md5 `1fe0b5b8c3b24abf0cadcde7b099076a`, kilde `ourui_v6_4.c`, md5 `5bdaf341f77dcec3554707cf6cd9df70`) er v6.4 uændret. Tilbagerulning som for de tidligere versioner: `cp ourui_v6_4.bin ourui_v6` lokalt og derefter `boot_install.py install` (den læser md5 ved kørsel). v6.5-binæren har md5 `e84785d514c11ef88705ac97240b98c1`. Sprog-filen, `cfgbak/` og boot-flaget er de samme i v6.4 og v6.5; v6.4 kender ikke `OURUI_NOBOOTPUSH` og ignorerer den.

## v6.6: re-push efter MCU-genstart, 64-bit-ur, damp-visning — hvad der er nyt, og hvordan det fortrydes

v6.6 ændrer tre ting i enheds-binæren (UI_v6.md CHANGES-v6.6). Kun den første sender noget, v6.5 ikke sendte:

1. **Re-push.** Genstarter controlleren, mens ourui kører, sendes boot-pushen igen: de **samme to blokke** (`cmd6 sub1 len30` + `cmd6 sub112 len3`), samme værdier fra `config.json`, gennem samme frame-politik, write-kø og aktivitets-vagt. Den udløses af, at controllerens versions-forespørgsel (`0x89`) kommer igen efter opstarten, eller af at ord 112 og 113 begge står på 0 (MCU'ens startværdier) i tre læsninger i træk på en ledig maskine. Mindst 5 s mellem to; højst 3 signatur-udløste pr. 10 min.
2. **64-bit-ur.** Ingen ændring i, hvad der sendes eller hvornår — uret løber blot ikke længere rundt efter 24,86 døgn. **v6.5 og ældre må ikke stå som permanent slot B-boot:** ved 24,86 døgns oppetid sender de en strøm af keep-awake-writes i 30 s, og poll eller skærm kan derefter gå i stå, til processen genstartes.
3. **Damp-visning.** Kun skærmen: *DAMP·AUTO* / *DAMP·WAND*, damp-mærke under bryg, STOP-knappen kun for latchet damp. Start- og stop-frames er de samme som i v6.5.

### Hvad v6.6 rører

| | |
|---|---|
| Filer | **ingen nye, ingen ændrede formater** — `config.json`, `cfgbak/`, `RESTART_NEEDED`, flag og sprog-fil er som i v6.5 |
| Controlleren | som boot-pushen: registerfilen i RAM (ord 1..30 og 112..114), ingen flash, ord 26/27/30 = 0 |
| Miljø-variabler | ingen nye på enheden. `OURUI_NOBOOTPUSH=1` slår nu også re-pushen fra; `OURUI_READONLY=1` som hidtil |

Der er **ingen tilstand at reverte** efter en re-push: den skriver de værdier, controlleren allerede havde før sin genstart.

### Se hvad den gjorde
DIAGNOSE, scrollet én række ned. Uden re-push hedder rækken *BOOT-PUSH 1-30* / *BOOT-PUSH 112-114*, som i v6.5. Efter en re-push hedder den **RE-PUSH n · 1-30** / **112-114**, hvor n er antallet siden ourui startede; cellerne viser udfaldet af den seneste push. `CMD6 OK` er steget med 2 pr. re-push. Der skrives ingen log-fil.

### Slå den fra uden at skifte binær
```sh
OURUI_NOBOOTPUSH=1 ./run.sh          # ingen boot-push og ingen re-push; DIAGNOSE viser FRA
```
I slot B-boot starter `demo.sh` binæren uden miljø-variabler; dér er vejen v6.5-binæren (nedenfor) — med forbeholdet om uret.

### Tilbage til v6.5
`artifacts/ourui/ourui_v6_5.bin` (md5 `e84785d514c11ef88705ac97240b98c1`, kilde `ourui_v6_5.c`, md5 `f6e502b3d4cb7d212d1a1786520b2de4`) er v6.5 uændret. Tilbagerulning som for de tidligere versioner: `cp ourui_v6_5.bin ourui_v6` lokalt og derefter `boot_install.py install` (den læser md5 ved kørsel). v6.6-binæren har md5 `c22153af54069d9d1fcbe43fcb118c99`. Sprog-filen, `cfgbak/`, `RESTART_NEEDED` og boot-flaget er de samme i v6.5 og v6.6, så der er intet at rydde op ved et skift i nogen af retningerne. BOOT-UI-vagten genkender begge binærer som ourui (`round_v6/boot_check.txt`).

---

## v6.7: wifi ved opstart og selv-heal (net-vagt) — hvad der er nyt, og hvordan det fortrydes

**Hvorfor:** ved slot B-boot starter stock-UI'et aldrig, og det er stock-UI'et (libmi_wlan, styret af `/config/wifi/wlan.json`) — ikke `demo.sh` — der bringer wifi op. En B-bootet maskine var derfor uden netværk (boot/ondevice/INSTALL-REPORT.md §3.1): ingen telnet, ingen fjern-revert. v6.7 gør det samme som stock, men **kun når stakken er nede**.

### Hvad v6.7 rører

| | Hvornår | Hvad |
|---|---|---|
| Processer | kun når der ingen wpa_supplicant kører (ved opstart: straks; ellers efter 3 s) | `sh -c "source /config/wifi/ssw01bInit.sh dummy1 dummy2 "` hvis `wlan0` mangler (insmod af driveren, `mkdir /tmp/wifi/run`) · `wpa_supplicant` med stock's argumenter · 1 s senere `udhcpc` med stock's argumenter — medmindre `wlan0` stadig har sin IP og default-rute (så beholdes de). Børnene får PPid 1 og ingen af ourui's fd'er |
| | wpa_supplicant lever, men `wlan0` er administrativt nede i 3 s | `ifconfig wlan0 up` (ioctl, som stock's MI_WLAN_Open) |
| | wpa_supplicant lever, ingen IP (8 s) eller ingen default-rute trods carrier (3 s), ingen udhcpc | `udhcpc` med stock's argumenter (backoff op til 60 s ved gentagelse) |
| Filer | ved bring-up, kun hvis indholdet afviger | `/appconfigs/wpa_supplicant.conf` (tmp + fsync + rename), byte for byte i wpa_supplicant's `save_config`-format; ssid/password fra `config.json` (læses, skrives aldrig). `demo.sh` kopierer `.conf_bak` over filen ved **hver** boot, og stock's `save_config` skriver den igen, så der er intet varigt at fortryde |
| | altid (tmpfs) | `/tmp/ourui_net.log` (kun overgange; password skrives aldrig; roteres ved 128 kB), `/tmp/wifi/run/`, en død `/tmp/wifi/run/wpa_supplicant/wlan0`-socket fjernes før start |
| Controlleren | — | intet nyt fra net-vagten. **Ord 113 i boot-pushen** sendes nu som stock's optagede (`0x0600`, lo-byte 0 — GROUND-TRUTH.md); GEM-vejen er uændret |

**Aldrig:** et signal til en proces (0 `kill()`), en ændring af `config.json`, slots eller flag, et netværk når `wifionoff = 0` i config, og intet, når `OURUI_NONET=1` eller `OURUI_ROOT` er sat.

### Se hvad den gjorde
```sh
cat /tmp/ourui_net.log                                   # én linje pr. overgang (OPPE ved start / SELV-HEAL faerdig paa N ms / ...)
/new_customer/ourui/ourui --net-status                   # kun læsning: probe + om conf-filen er identisk med config.json's
```
DIAGNOSE (scrollet ned): **NETVÆRK · WPA-PID / IP / TILSTAND**, fx `841 / <display-ip> / OPPE` (grøn) eller `- / <display-ip> / STARTER` (rav).

### Slå den fra uden at skifte binær
```sh
OURUI_NONET=1 ./run.sh          # net-vagten er slået helt fra (DIAGNOSE: FRA)
```
Er netværket allerede startet af ourui, er det en almindelig wpa_supplicant med stock's argumenter (PPid 1). Den kan blive kørende; ved næste reboot starter stock / ourui forfra. Skal den væk: `kill $(pidof wpa_supplicant)` (det gør ourui aldrig selv).

### Tilbage til v6.6
`artifacts/ourui/ourui_v6_6.bin` (md5 `c22153af54069d9d1fcbe43fcb118c99`) er v6.6 uændret — **men v6.6 i slot B-boot giver en maskine uden netværk** (INSTALL-REPORT §3.1). Forgængerens ufærdige v6.7-mellemstand er gemt som `ourui_v6_7wip.c` / `ourui_v6_7wip.bin` til sporbarhed; den skal ikke installeres (bl.a. 10 røde selftests og et `kill()`-kald).

---

## v6.8: REVERT-knappen på HJEM er fjernet

REVERT-pillen nederst på HJEM var en fase-2-sikring. Fra v6.8 er HJEM: gauge + stats + aktivitets-ikoner + side-prikker. Der er **ingen tryk-zone** tilbage, hvor pillen sad (selftest: hele feltet 150..330 × 404..480 giver intet mål — både ledig og under bryg).

**Flugtvejene er uændrede:**
1. SYSTEM → **SKIFT TIL ORIGINAL NU** → **SKIFT NU**. I run.sh-tilstand = ren exit (exit-påmindelsen først, hvis config er gemt) → run.sh SIGCONT'er demo_v9 — præcis det, REVERT gjorde. I slot B-boot starter den stock-UI'et (BOOT.md §9.2). Spærret under en aktivitet.
2. SYSTEM → **BOOT-UI** (næste genstart: original).
3. Switcher-gestussen (3 lange tryk øverst til venstre) — uændret.
4. `run.sh`'s trap ved enhver exit (crash, Ctrl-C, `kill $(pidof ourui)`) og telnet-kommandoerne øverst i denne fil.

Run.sh-teksten på skift-skærmen hedder nu *RUN.SH-TILSTAND · REN EXIT* (før: *· SOM REVERT*). Ingen filer, ingen frames, intet på enheden ændres af v6.8. **Tilbage til v6.7-adfærden** (med knap) findes ikke som separat binær; v6.6 (`ourui_v6_6.bin`) har knappen, men ikke net-vagten.

---

## v6.9: rolig graf og stabil vægt — intet nyt på enheden ud over visningen

v6.9 ændrer **kun det, der tegnes** (UI_v6.md §14 og "CHANGES-v6.9"): graf, hero, gauge og BAR-felt viser median-3 + EMA af de rå samples, vægten har en fast kilde (latch i stedet for fase pr. sample) og et dødbånd under 0,25 g, og DIAGNOSE har rækken **GRAF** (glat / rå).

### Hvad v6.9 rører
| | |
|---|---|
| Filer | **ingen nye.** GRAF-valget på DIAGNOSE gælder kun sessionen og skrives ingen steder |
| Controlleren | **intet nyt** — samme frames, samme frame-politik, samme boot-push, samme auto-stop (MCU'ens). Build-tjekket viser 29 styrings-/protokol-stykker token-identiske med v6.8 |
| Processer / netværk | intet nyt (net-vagten er v6.7's, uændret) |

Styring, writes, vagter, tærskler og DIAGNOSE-tællere læser fortsat de **rå** værdier. Se dem rå på skærmen: DIAGNOSE → **GRAF · TRYK FOR AT SKIFTE** → **RÅ · UFILTRERET** (chartet mærkes **RÅ**; glemmes ved genstart).

### Tilbage til v6.8
`artifacts/ourui/ourui_v6_8wip.bin` (md5 `75887fb154ffef20cc72696064e16c39`, kilde `ourui_v6_8wip.c` md5 `5cdcc14b0503c67b939e27b6579655ff`) er v6.8 — den hidtidige enheds-binær, med net-vagten og uden REVERT-knap. Læg den på `ourui`'s plads som enhver anden version (ingen data, config eller flag at rydde op i). Stand ved v6.9-rundens start (forgængerens delvise v6.9): `ourui_v6_9wip.c` / `ourui_v6_9wip.bin` — **skal ikke installeres** (2 røde selftests, vægt-kilden følger fasen, intet dødbånd).


### Status ved v6.9-afslutningen (2026-11-17, kl. 23)
* **Enheds-binæren** `ourui_v6` har md5 `2dce4eba0b4ce68dcd0369e955cf3b77`. Kilden er `ourui_v6.c` md5 `440be5124c0d986ec23e4c3b08f84149`, og et nyt build fra den er byte-identisk. Den er **ikke** lagt på enheden.
* **Ingen tryk-zone fra REVERT (v6.8):** selftesten giver stadig `T_NONE` i hele feltet 150..329 × 404..479, både ledig og under bryg. Ingen UI-streng nævner REVERT. Mutanten `pille` fanges stadig (mutations-prøver 10/10).
* **Net-vagtens selv-heal på enheden (v6.7)** er stadig **ikke bevist på enheden**. Den svarede ikke på netværket (<display-ip> "Host is down"). `net_heal_proof.py` er uændret og klar. Den laver ingen reboots, rører ikke flag, slots eller config og bruger kun tmpfs, plus ét kontrolleret kill af stock's wpa_supplicant, mens demo_v9 er SIGSTOP'et.
* **Fortryd v6.9** som ovenfor: `ourui_v6_8wip.bin` på `ourui`'s plads. Der er ingen data at rydde op i.

## v6.10: tryk som ≈bar-estimat — kun visning

v6.10 ændrer **kun hvordan trykket vises** (UI_v6.md §16 og "CHANGES-v6.10"): *TRYK\* UKALIBRERET* er blevet **≈BAR · EST.** med ét lineært anker (pre-infusion 358 rå i optagelsen = config `preInfusionPressure`, 4 bar → k = 0,01117 bar/rå), pulser over 20 bar vises som *PULS* i stedet for et tal, DIAGNOSE har blokken **TRYK · ESTIMAT**, og et tap på chartet/BAR-feltet giver en toast med grundlaget.

### Hvad v6.10 rører
| | |
|---|---|
| Filer | **ingen nye.** Ankeret *læses* fra `config.json` (`preInfusionPressure`, samme indlæsning som settings-tabellen) — der skrives intet |
| Controlleren | **intet nyt** — samme frames, frame-politik, boot-push og auto-stop. Build-tjekket: 37 styrings-/protokol-/konditionerings-stykker er token-identiske med v6.9; toast-tappet sender ingen frames (selftest) |
| Processer / netværk | intet nyt |

### Tilbage til v6.9
`artifacts/ourui/ourui_v6_9.bin` (md5 `2dce4eba0b4ce68dcd0369e955cf3b77`, kilde `ourui_v6_9.c` md5 `440be5124c0d986ec23e4c3b08f84149`) er v6.9 — den hidtidige enheds-binær. SIM-builds: `ourui_v6_9_sim` / `ourui_v6_9_sim_arm`. Læg den på `ourui`'s plads som enhver anden version; der er ingen data, config eller flag at rydde op i. Rå tryk-værdier ses også uden at fortryde: DIAGNOSE → **GRAF** → **RÅ · UFILTRERET** (BAR-feltet viser så ord 51 rå under *TRYK RÅ*).

### Status ved v6.10-afslutningen (2026-11-17)
* **Enheds-binæren** `ourui_v6` har md5 `29bc801c1c777d9cb3484bfa960c66bc` (kilde `ourui_v6.c` md5 `23672df8557a1fbb4d54cdb6b220c244`; reproducerbar). Den er **ikke** lagt på enheden; enheden er ikke kontaktet i runden. Ingen reboots, flag-, slot- eller config-writes.
* Mutanten `pille` (REVERT-zonen) fanges stadig; mutations-prøver 14/14.

## v6.11: UX-review — kun visning og touch-ruter

v6.11 ændrer **hvordan UI'et ser ud og navigeres** (UI_v6.md §17 og "CHANGES-v6.11"): klart sprog i stedet for ord-numre og rå koder, hævet kontrast, et start-hint i det tomme chart-felt, et tandhjul til indstillingerne, luk-chevron og stryg-ned i detaljerne, en swipe-vagt og DIAGNOSE i sektioner.

### Hvad v6.11 rører
| | |
|---|---|
| Filer | **ingen nye.** Tandhjulet, chevronen og stryg-ned skifter kun side eller lukker en visning (som ANNULLER). Der skrives intet |
| Controlleren | **intet nyt.** Samme frames, frame-politik, boot-push, aktivitets-recepter og auto-stop. Build-tjekket: 47 styrings-/protokol-stykker er token-identiske med v6.10. Selftesten bekræfter, at tandhjul og kant-tryk ikke sender nogen frames |
| Processer / netværk | intet nyt |
| Revert-vejen | **uændret:** SYSTEM → *SKIFT TIL ORIGINAL NU* → *SKIFT NU*. Teksterne der er de samme. Tandhjulet ligger, hvor REVERT-pillen sad før v6.8, men det **skifter kun side** (selftest + mutanten `pille`) |

### Tilbage til v6.10
`artifacts/ourui/ourui_v6_10.bin` (md5 `29bc801c1c777d9cb3484bfa960c66bc`, kilde `ourui_v6_10.c` md5 `23672df8557a1fbb4d54cdb6b220c244`) er v6.10, den hidtidige enheds-binær. SIM-builds: `ourui_v6_10_sim` / `ourui_v6_10_sim_arm`. Læg den på `ourui`'s plads som ved tidligere versioner. Der er ingen data at rydde op i; UI-sprog-filen har samme format.

### Status ved v6.11-afslutningen (2026-11-18)
* **Enheds-binæren** `ourui_v6` har md5 `725386ccadade594275f20cc998885eb` (kilde `ourui_v6.c` md5 `e9eea8caebfa5d832884d96996dbd2c7`; reproducerbar). Den er **ikke** lagt på enheden; enheden er ikke kontaktet i runden. Ingen reboots, flag-, slot- eller config-writes.
* Mutanten `pille` (REVERT-zonen) fanges stadig; mutations-prøver 20/20.

## v6.13: manuel STOP for vand / bryg / kværn

v6.13 tilføjer STOP-knapper på HJEM (UI_v6.md §18 og "CHANGES-v6.13"). Et tryk sender én af tre nye stop-frames til controlleren: `sub16 len1` (ord 16 = 0, + `sub19 len1`, hvis ord 19 ≠ 0), `sub22 len1` (ord 22 = 0) eller `sub6 len1` (ord 6 = 0). Det er de samme ord, som MCU'ens egen auto-stop nulstiller.

### Hvad v6.13 rører
| | |
|---|---|
| Filer | **ingen nye.** STOP skriver hverken i config, flag eller lang-filen |
| Controlleren | **kun ved tryk på STOP:** én cmd6-write af ét ord til 0 (vand evt. to). Frame-politikken, start-recepterne, damp-stoppet, boot-pushen og auto-stop er token-identiske med v6.12 (build-tjekket: 47 stykker). En stop-frame indeholder kun nuller og kan ikke starte noget |
| Processer / netværk | intet nyt |
| Revert-vejen | **uændret:** SYSTEM → *SKIFT TIL ORIGINAL NU* → *SKIFT NU*. STOP-knappen ligger i aktions-rækken (y 344–400), ikke i SKIFT-/tandhjuls-zonerne |

### Tilbage til v6.12
`artifacts/ourui/ourui_v6_12.bin` (md5 `725386ccadade594275f20cc998885eb`, kilde `ourui_v6_12.c` md5 `e9eea8caebfa5d832884d96996dbd2c7`) er v6.12, den udrullede binær. SIM-builds: `ourui_v6_12_sim` / `ourui_v6_12_sim_arm`. Læg den på `ourui`'s plads på samme måde som ved de tidligere rul-tilbage (md5-tjek før og efter). Ingen config-/flag-ændringer er nødvendige: v6.13 har ingen tilstand på disken.

### Status ved v6.13-afslutningen (2026-11-18)
* **Enheds-binæren** `ourui_v6` har md5 `163692b5b3e11d8294471c5a0eae578b` (kilde `ourui_v6.c` md5 `eec5085ddc1861c212d396c70771e9dc`; reproducerbar). Den er **ikke** lagt på enheden. Maskinen var offline i hele runden, så der er ingen reboots og ingen flag-, slot- eller config-writes.
* On-device-beviset for STOP køres med `python3 verify_stop_v613.py`, når maskinen er tændt. Scriptet bruger run.sh-tilstand i `/tmp/ourui_s`, rører ingen config, flag eller slots og laver ingen reboot.
