# UI_v6 — Meraki+ (kode-navn ourui) v6.14: den fysiske tænd/sluk-knap + manuel STOP for vand / bryg / kværn + settings-paritet (spec-afstemt) + boot-slot-integration + EN/DA + design til den runde skærm + SIM-build/E2E + boot-push og RX-resync på enheden + live-bevist fase-semantik, 64-bit-ur og re-push efter MCU-genstart + wifi ved opstart og selv-heal (net-vagt) + HJEM uden REVERT + rolig graf og stabil vægt (visnings-konditionering, vægt-latch, dødbånd) + tryk som ≈bar-estimat (anker: pre-infusion = config-tryk) + UX-review (klart sprog, kontrast, start-hint, tandhjul, luk-chevron, swipe-vagt, DIAGNOSE-grupper)

> **Note (offentlig udgave):** Udviklingsnoter skrevet under udviklingen. Stier som `artifacts/…`, `boot_install.py`-trin og md5-tabeller refererer til forfatterens arbejdsmiljø; som slutbruger skal du bruge `./install.sh` og `./revert.sh` (se [README](../../README.md)). Producentens filer er ikke inkluderet; IP-adresser og andre personlige data er fjernet.

**Dato:** 2026-11-18 · **Version:** v6.14 (v6.13 + **den fysiske tænd/sluk-knap** — brugerfejl: knappen virkede ikke i ourui; k62091-noden (event1) læses nu, og stock's sluk-/tænd-semantik er reverset og spejlet, §19 + [CHANGES-v6.14](#changes-v614)). v6.13 = (v6.12 + **manuel STOP for VAND, BRYG og KVÆRN** — brugerfejl: varmt vand kunne ikke stoppes fra skærmen; stop-ordene er reverset fra MCU-firmwaren og krydstjekket mod stock-UI'ets egne stop-kald; auto-stop er uændret — se [§18](#18-manuel-stop-v613) og [CHANGES-v6.13](#changes-v613); v6.12 = v6.11-UI'et + boot-agentens gestus-fix (`boot/GESTUS-FIX.md`), kilde bevaret som `ourui_v6_12.c`; v6.11 = v6.10 + **UX-review**: et kritisk design-pas over alle sider og tilstande — klart sprog i stedet for ord-numre og rå koder, hævet kontrast, et start-hint i det tomme chart-felt, et synligt tandhjul til indstillingerne, luk-chevron + stryg-ned i detaljerne, swipe-vagt med akse-lås, korte stat-labels og DIAGNOSE i sektioner; styring og protokol er uændret — se [§17](#17-ux-review-v611) og [CHANGES-v6.11](#changes-v611); v6.10 = v6.9 + **tryk-estimat**: "TRYK\* UKALIBRERET" er erstattet af **≈BAR · EST.** med ét lineært anker — pre-infusion-plateauet 358 rå i optagelsen = config `preInfusionPressure` (4 bar) — og en puls-grænse på 20 bar; kun visning — se [§16](#16-tryk-estimat-v610) og [CHANGES-v6.10](#changes-v610); v6.9 = v6.8 + **visnings-konditionering** af graf og hero — median-3 + EMA, robust skala, puls-gate, tryk-kurve kun under bryg/vand —, **stabil vægt-kilde** (latch i stedet for fase pr. sample: brugerens periodiske vægt-spikes) og **vægt-dødbånd** < 0,25 g; kun visning, al styring læser rå værdier — se [§14](#14-visnings-konditionering-og-stabil-vægt-v69) og [CHANGES-v6.9](#changes-v69); v6.8 = v6.7 uden REVERT-knappen på HJEM — se [CHANGES-v6.8](#changes-v68); v6.7 = v6.6 + **net-vagt**: wifi ved opstart (slot B-boot var uden netværk) og selv-heal af wifi-stakken, plus ord 113 i boot-pushen som stock's optagede — se [CHANGES-v6.7](#changes-v67); v6.6 = v6.5 + fase-semantik efter live-sessionen — to damp-veje og samtidige faser —, **64-bit-ur** (v6.5's ur løb rundt efter 24,86 døgn på enheden) og **re-push af boot-pushen efter en MCU-genstart** — se [CHANGES-v6.6](#changes-v66); v6.5 = v6.4 + boot-push fra config ved opstart **også på enheden**, med aktivitets-vagt, og en resync-watchdog i RX-parseren — se [CHANGES-v6.5](#changes-v65); v6.4 = v6.3 + SIM-build mod det simulerede testmiljø — se [§13](#13-sim-build-og-e2e-v64) og [CHANGES-v6.4](#changes-v64); v6.3 = v6.2 + produktnavnet **Meraki+** og to UI-sprog, engelsk default / dansk — se [CHANGES-v6.3](#changes-v63); v6.2 = v6.1 + BOOT-UI, runtime-skift til stock, exit-påmindelse — se [CHANGES-v6.2](#changes-v62)) · **Status:** implementeret, cross-kompileret og **kun verificeret lokalt** (offline previews + selftest + maskinel spec-afstemning). **Intet er kørt på maskinen, og ingen netværksenhed er kontaktet.** On-device-arbejdet er integrations-ingeniørens (§11).
**Kilde:** `ourui_v6.c` (én fil, ren C, ingen nye afhængigheder) · **Binær:** `ourui_v6` (statisk ARMv7 EABI5 musl) · **Previews:** `round_v6/` (engelsk; `kontakt-ark`) + `round_v6/da/` (dansk; `kontakt-ark`) · `EN | DA side om side` · **Revert:** `REVERT.md` · **Boot-integration:** `boot/BOOT.md` (kollegaens; §9 = UI-kravene til v6.2) · **Spec:** [PARITY.md](PARITY.md) / `parity_spec.json` · **Forgænger:** `ROUNDUI.md` (v4)

> **Status for forbeholdet i v6:** spec'en manglede, da v6 blev skrevet, og alle grænser var antagelser. v6.1 har erstattet dem med spec'ens beviste intervaller. Afstemningen er maskinel: `parity_check.py` sammenligner `SET[]` i binæren med `parity_spec.json` og fejler ved enhver ubegrundet afvigelse (`round_v6/parity_check.txt`: **41 spec-parametre → 24 rækker, 17 udeladt med begrundelse, ALT AFSTEMT**). Hvad der stadig er skøn eller låst, står i §4.4. Alle ændringer er listet i [CHANGES-v6.1](#changes-v61).

> **Navne og sprog (v6.3):** Produktet hedder **Meraki+** i UI'et. `ourui` er fortsat kode-navnet — kildefil, binær, proces, mappen `/new_customer/ourui/`, miljø-variabler og logs — og bruges derfor også i dette dokument, når der menes programmet. UI'et findes på **engelsk (default) og dansk**. Dokumentet omtaler skærm-elementer med deres **danske** tekst (det man ser med UI-SPROG = DANSK); den engelske ordlyd står i [Appendiks A](#appendiks-a--streng-tabel-id--en--da). Nogle danske tekster er omdøbt i v6.3 (CHANGES-v6.3 pkt. 5): afsnit 1–12 bruger de nye navne, CHANGES-v6.1 og -v6.2 citerer de daværende.

---

## 1. Hvad er nyt ift. v4

| Område | v4 | v6 |
|---|---|---|
| Keep-awake | skrev `ord1=1` (= sleep/drain, **bug**) | skriver **`ord1=0`** ved start + hver 30 s; hver 3 s hvis spejlet viser `ord1≠0` |
| Writes | kun power/awake | write-kø m. CRC + ACK-vent + **én** retransmit (150 ms → retransmit → 300 ms → fejl); **frame-politik** på hver cmd6-frame: skrive-bånd, trigger-ord og pr.-ord spec-interval (§7a) |
| Aktiviteter | inerte pills | KVÆRN / BRYG / VAND / DAMP m. observerede recepter, **hold-for-start** (650 ms), STOP for damp |
| Settings | ingen | 24 config-rækker på 3 sider (21 redigerbare, 2 låste, 1 sensor) + fra v6.3 UI-SPROG (egen fil), stepper-detalje, SAVE/CANCEL, atomisk `config.json`-persist m. backup + md5-log |
| Faser | idle/grind/brew/hotwater | + **DAMP** (`work_sta & 0x4020`), SOVER (`ord1==1` eller bit 8 ryddet), FEJL (E1/E2/E3), OFFLINE, VARMER OP/KLAR, FÆRDIG |
| Grafik | 5×7 blok-font, hel-side-redraw | egen anti-aliaset stroke-font, AA-primitiver, gauge m. glød, dirty-rect-compositor, slide/crossfade, ripple, toasts, splash |
| Input | events også til demo_v9's kø | `EVIOCGRAB` (eksklusiv) — se §7.4 |
| Poll | ord 0..111 | + `cmd3 sub112 len3` hvert sekund (ord 112..114 til pre-infusion/gruppe-temp) |

Bevaret uændret: global circle-clip (chord-mask i `pset`/`hspan`, R=234), 10 Hz poll-mønster (sub0/sub33 @50 ms skiftevis + sub56 @200 ms), CRC-tællere, cmd9-versionssvar, graceful exit på SIGINT/SIGTERM, **run.sh-kontrakten** (SIGSTOP/SIGCONT; run.sh er ikke ændret).

---

## 2. Design-system

**Farver** (næsten-sort m. radial vignette, ordered dither mod banding; ren sort uden for R=234):

| Token | RGB | Brug |
|---|---|---|
| tekst 1 / 2 / 3 | 242,244,247 / **176,183,196** / **140,147,162** (v6.11; v6.10: 160,167,180 / 104,111,126) | værdier / under-linjer / mikro-labels — kontrast målt i selftesten: tekst 3 ≥ 4,5:1 på bund, flade 1 og 2; tekst 2 ≥ 7:1 |
| deaktiveret (v6.11) | 98,105,120 | slukkede ikoner, stepper-tegn ved grænsen, offline-hero, deaktiveret GEM — ≥ 3:1 (store glyffer), bevidst svagere end tekst 3 |
| flade 1 / 2 / 3 | 25,28,35 / 37,41,51 / 52,57,70 | chips, knapper, tryk-tilstand |
| accent "crema" | 244,192,122 | **eneste UI-accent**: settings-værdier, GEM, stepper-ring, splash |
| ok / advarsel / fejl | 98,222,168 / 255,190,84 / 255,96,96 | toasts, CRC-chip, fejl |

Semantiske fase-farver (bruges konsekvent på gauge-fyld, tændte ticks, fase-chip, under-linje, chart og aktiv knap):

| Fase | Farve | | Fase | Farve |
|---|---|---|---|---|
| OFFLINE | grå 124,130,144 | | KVÆRNER | lime 174,228,112 |
| SOVER | indigo 136,144,222 | | PRE-INFUSION | blå 92,180,255 |
| FEJL | rød 255,96,96 | | EKSTRAKTION | kobber 255,142,68 |
| VARMER OP | rav 255,186,84 | | VARMT VAND | cyan 70,216,230 |
| KLAR / FÆRDIG | mint 98,222,168 | | DAMP | violet 198,152,255 |

**Typografi:** egen stroke-font (stregtegnede glyffer i 10-enheders gitter, rasteriseret m. afstandsfelt → ægte anti-aliasing, cachet pr. størrelse). Bevidst **versal-typografi m. spatiering**; tal er **tabulære** (alle cifre samme bredde, så værdier ikke hopper). Fuld ÆØÅ. Skala (versalhøjde px): XS 11 · S 14 · M 18 · L 26 · XL 44 · XXL 66 (hero-tal). Ikoner (bønne, kop, dråbe, damp) er glyffer i samme motor.

> Afvigelse fra opgaveteksten: den nævnte en skaleret bitmap-font (1x–4x). En 5×7-font skaleret ×8 giver trappetrin, som ikke kan kaldes poleret, så jeg skrev en stroke-font i stedet. Stadig ren C uden afhængigheder, og font-data er ~70 linjer i kilden.

**Grid:** centreret søjle; alt ligger inden for gaugens inder-radius (~193). Rækker i lister: 336×50 px chips, 58 px pitch, hjørneradius 16. Pills: fuldt afrundede, 2 px blød skygge.

**Gauge:** 270° bue (135°→405°), centerlinje r=214, tykkelse 14, runde ender, gradient mørk→lys langs fyldet, hvid "hoved"-prik, blød glød ±5 px, 50 ticks (hver 5. større) r 225..232 der tændes i fasefarve op til værdien, hvid **mål-markør** på tværs af ringen. Under bryg en tynd hvid indre ring (r=198) = vægt-fremskridt mod auto-stop.

**Motion:** eksponentielt easet gauge-fyld (τ≈110 ms) · sweep fra nul når gaugen skifter størrelse (temp ↔ vægt ↔ tryk) · 300 ms farve-crossfade ved faseskift · touch-ripple (360 ms) · tryk-tilstand på alle knapper · hold-ring rundt om aktivitets-knap · pulserende ring på aktiv aktivitet · page-slide 260 ms (ease-in-out) · crossfade 200 ms ind/ud af detalje · toast fade/slide · splash m. bue-sweep (1,5 s).

---

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

## 5. Gem-sti: rækkefølge og reload-semantik

GEM er **alt-eller-intet** og kører i denne rækkefølge (samme som stock's onClick-handlere: MCU først, så config — PARITY §5 pkt. 8):

1. **Vagter:** værdi inden for interval, config indlæst, ingen aktivitet i gang, link oppe (for MCU-parametre), write-køen tom.
2. **MCU-write** (hvis parameteren har et ord): cmd6-blok gennem frame-politikken → vent på `0x86`-ACK m. echo. Ingen ACK efter én retransmit → toast *"GEM FEJLET · INGEN ACK · INTET GEMT"* (v6.4; før *"INGEN ACK · INTET GEMT"*), config røres ikke.
3. **config.json-persist** (`cfg_set_int`): frisk læsning → md5 → `cfgbak/config.json.orig` oprettes hvis den mangler (**pristine, overskrives aldrig**) → `cfgbak/config.json.bak` (rullende) → nyt indhold hvor **kun tal-tokenet** for det ene top-niveau-felt er udskiftet → `/customer/.ourui_cfg.tmp` → `fsync` → genlæses og sammenlignes → `rename()` → `fsync` af mappen → linje i `cfgbak/config_writes.log` → flag-filen `cfgbak/RESTART_NEEDED`.
4. Fejler trin 3, sendes den **gamle** værdi til MCU'en igen (rollback), og UI'et viser *"CONFIG-FEJL · INTET GEMT"*.

**Format-paritet med stock.** Stock's `setNumber` skriver straks hele træet med `cJSON_Print` (`fseek/ftruncate/fwrite/fflush/fsync`, PARITY §1.2). ourui beholder det sikrere mønster (tmp + rename + backup + md5), men resultatet er **byte-identisk** med det stock selv ville have skrevet: `cjson_canon.py` genskaber `cJSON_Print`-formatet uafhængigt og viser, at (a) baseline-filen er i præcis det format, (b) ourui's fil efter en GEM er lig `cJSON_Print(baseline med feltet ændret)`, (c) feltrækkefølgen er uændret og intet felt er tilføjet (`round_v6/config_format_check.txt`). ourui tilføjer aldrig nøgler og rører aldrig strenge eller `presetStore`.

**Reload-semantik (rettet i v6.1 efter PARITY §1.4):**

| Hvem | Hvornår slår en GEM igennem |
|---|---|
| **Controlleren (MCU)** | **straks** — cmd6-writen er ACK'et, før config røres |
| **ourui** | straks |
| **Stock-UI (demo_v9)** | **først efter genstart** (`kill $(pidof demo_v9)`; watchdog starter den igen på < 4 s). Ved opstart læser den `config.json` og **boot-pusher** selv værdierne til controlleren: `cmd6 sub1 len30` (ord 1..30) + `cmd6 sub112 len3` (ord 112..114) |

Det betyder:
* Efter genstart er config, stock-UI og controller **automatisk i sync** — også for et felt, hvis MCU-write skulle være gået tabt.
* **Uden** genstart (kun exit → SIGCONT; v6.8: *SKIFT TIL ORIGINAL NU* i run.sh-tilstand) kører stock-UI'et videre med sit **gamle RAM-spejl**. Det viser gamle værdier, og dets næste blok-write (fx bryg-start `sub5 len18`, som medsender ord 11/14/17) sender de **gamle** værdier til controlleren igen. Dets egen næste config-skrivning udskriver desuden dets gamle træ. Derfor: **genstart demo_v9 efter en ourui-GEM, før stock-UI'et bruges.** `RESTART_NEEDED` markerer at det udestår, og ourui skriver en påmindelse på stdout ved exit.
* **ourui dræber aldrig demo_v9 selv** — det ville bryde run.sh-kontrakten (run.sh SIGCONT'er den PID, det startede med). Genstarten er et manuelt trin.
* UI-teksten i detalje-visningen siger derfor *"CONTROLLER STRAKS + CONFIG"* / *"STOCK-UI: EFTER GENSTART"* (v6 skrev blot *"VIRKER STRAKS"*, hvilket var en overdrivelse for stock-UI'ets vedkommende).

Stier kan omdirigeres: `OURUI_CONFIG` (default `/customer/config.json`), `OURUI_BAKDIR` (default `/new_customer/ourui/cfgbak`), og fra v6.3 `OURUI_LANGFILE` (default `/new_customer/ourui/lang`; UI-sproget, som **ikke** går gennem gem-stien her — se CHANGES-v6.3 pkt. 2).

---

## 6. Aktivitets-styring

Frames bygges af `act_push()`; kun de config-afledte felter substitueres, resten er som observeret. Selftesten sammenligner dem **byte-for-byte inkl. CRC** med de optagne frames:

| Aktivitet | Blok | Indhold | Kilde | Stop |
|---|---|---|---|---|
| KVÆRN | sub3 len4 | `1, dosis, 60, 1` | [L] BREW.md W1 | auto ved vægt · **STOP KVÆRN** = `sub6 len1` ord 6 = 0 (v6.13, §18) |
| BRYG | sub5 len18 | `60,0,0,0,1,0,temp,0,60,niveau,60,0,vand,0,0,0,0,1` | [L] BREW.md W3 | auto ved vægt (prædiktivt) · **STOP BRYG** = `sub22 len1` ord 22 = 0 (v6.13, §18) |
| VAND | sub9 len11 | `1,0,temp,0,60,niveau,60,1,vand,0,0` | [L] HOTWATER.md W1 | auto ved vægt · **STOP VAND** = `sub16 len1` ord 16 = 0 (+ `sub19 len1` ord 19 = 0, hvis ord 19 ≠ 0) (v6.13, §18) |
| DAMP | sub24 len8 | `1, mål, 0,0,0,0,0,0` | [L] STEAMLIVE.md §4.1 (byte-identisk) | auto ved mælk (ord 48) ≥ mål (ord 25) · **STOP DAMP** = samme blok m. ord24=0 — kun for latchet damp (v6.6) |

* **Før BRYG** sammenlignes MCU-spejlet med config; kun ved afvigelse sendes `sub23 len1` (bryg-vægt) og/eller `sub112 len3` (pre-infusion + gruppe-temp) først. ord113 sammenlignes kun på hi-byten (§4a nr. 7). Kan en frame i sekvensen ikke køes, køes ingen af dem.
* **Vagter** (ellers toast m. årsag): link oppe, ikke SOVER, ingen fejlkode, ingen aktivitet i gang, write-kø tom, alle frame-relevante config-værdier indlæst og inden for spec, og tilstanden er den observerede (`grindMode=1` vægt, `hotWaterMode=1` auto) — kværn på tid og manuelt vand har ingen observeret recept.
* **Intent:** hver start-frame erklærer hvilke start-ord den må sætte (KVÆRN 3+6, BRYG 9+22, VAND 9+16, DAMP 24). Alt andet afvises (§7a).
* **Auto-stop-forventning** står i under-linjen (*"AUTO-STOP VED 36.0 G · ELLER STOP"* — v6.13; *"AUTO-STOP VED 65.0° · ELLER STOP"*) og som markør/indre ring.
* **Manuel STOP (v6.13):** mens kværn, bryg eller vand kører, står **STOP KVÆRN / STOP BRYG / STOP VAND** dér, hvor start-ikonerne står (tryk-felt 180 × 56 px). Tryk → *"STOPPER VAND…"* → stop-frame → *"VAND STOPPET"*, når MCU'en har ryddet bits'ene. Detaljer: [§18](#18-manuel-stop-v613).
* **Damp (v6.6, live-bevist i STEAMLIVE.md):** efter start vises *"STARTER DAMP…"*; dampen starter af sig selv ca. 0,2 s efter writen, uden at wand'en åbnes. Chippen er da **DAMP·AUTO** med mål (ord 25), auto-stop-forventning og STOP-knap. Åbnes wand'en uden en start fra skærmen (flush), er chippen **DAMP·WAND**: ingen auto-stop-forventning, ingen STOP-knap — den stopper, når wand'en slippes, og ourui sender intet. Damp oven i et bryg (work_sta `5327`) vises som bryggets side med et damp-mærke og mælke-temperaturen i stat-rækken. Detaljer: [CHANGES-v6.6](#changes-v66) pkt. 2.
* **v6.8:** HJEM har ingen REVERT-knap. Vejen tilbage til stock-UI'et er SYSTEM → *SKIFT TIL ORIGINAL NU* (spærret under en aktivitet; MCU'en auto-stopper selv), switcher-gestussen og run.sh's trap.

---

## 7. Protokol- og system-detaljer

1. **Keep-awake:** `AA 55 06 01 01 00 00 C5 33` (ord1=0). Startsekvens: `sub0=[1]`, `sub1=[0]`.
2. **Fase-detektion** (`phase_raw`): `0x4020` → DAMP · bit 6 → KVÆRNER · bits 1+2 → bryg (bit 9 = ekstraktion) · bits 1+3 → VARMT VAND · E1/E2/E3 ≠ 0 → FEJL · `ord1==1` eller bit 8 ryddet → SOVER · ord40 < ord11 − 2,0 °C → VARMER OP · ellers KLAR. (v4's bryg-maske `0x206` kunne aldrig matche pre-infusion `0x1107`; rettet.) **v6.6:** `phase_raw` er uændret og er det, vagterne og state-kontrakten bruger. HJEM viser `phase_main`: kværn / bryg / vand går forud for damp, og damp-vejen afgøres af ord 24, mens `0x4020` er sat (≠ 0 → AUTO, klæbende; = 0 → WAND; ord 44 bit0 / ord 46 bekræfter).
3. **Fejl/tomme tilstande:** OFFLINE efter 1,5 s uden gyldig frame · CRC-chip i 10 s efter en CRC-fejl · fejlkoder afkodet til klart sprog (REGISTERMAP §2.7; v6.11: koden E1/E2/E3 står nedtonet i chart-feltet med en henvisning til DIAGNOSE) · v6.11: det tomme chart siger, hvad man kan gøre (*HOLD ET IKON FOR AT STARTE* / *STRYG FOR INDSTILLINGER*, eller grunden til at start er spærret) i stedet for "INGEN AKTIVITET".
4. **`EVIOCGRAB`:** mens demo_v9 er SIGSTOP'et, lægger kernen stadig hvert tryk i dens evdev-kø, og den afspiller dem efter SIGCONT. I v4 var det ufarligt (få tryk, på baggrund). v6 har mange tryk midt på skærmen — præcis hvor stock-UI'ets Grinder/Brew/Steam/Hot Water-knapper sidder — så ourui tager eksklusivt greb om input-enheden og slipper det ved exit. **Bivirkning:** `tinj` (en anden proces) kan ikke injicere, mens grebet holdes. `OURUI_NOGRAB=1` slår grebet fra til tinj-kørsler.
5. **Read-only:** `OURUI_READONLY=1` blokerer alle cmd6-writes (også keep-awake og boot-push) — til første skærm-/touch-runde.
6. **Boot-push (v6.5):** efter startsekvensen, når linket er oppe og work_sta er læst: `cmd6 sub1 len30` + `cmd6 sub112 len3` fra config.json — stock-UI'ets egne to blokke — gennem frame-politikken og write-køen. Kører en aktivitet allerede ved opstart, springes den over (aktivitets-vagt). `OURUI_NOBOOTPUSH=1` slår den fra. Detaljer: [CHANGES-v6.5](#changes-v65) pkt. 2.
7. **RX-parser (v6.5):** en frame accepteres kun med hel længde og gyldig CRC; en kandidat med umulig længde forkastes straks, og en ufuldstændig kandidat forkastes efter 400 ms (`RX_RESYNC_MS`), så parseren finder næste `AA 55`. Tælles på DIAGNOSE (*RX CRC / RESYNC*). Detaljer: [CHANGES-v6.5](#changes-v65) pkt. 3.
8. **Ur (v6.6):** alle tider er `ms_t` (`int64_t`), dannet af `CLOCK_MONOTONIC`. Til og med v6.5 var uret en `long` (32 bit på enheden) og løb rundt efter 24,86 døgn. Main-loopets timere står i `lt`. Detaljer: [CHANGES-v6.6](#changes-v66) pkt. 3.
9. **Re-push efter MCU-genstart (v6.6):** kommer versions-forespørgslen (`0x89`) igen mere end 200 ms efter boot-pushen, eller står ord 112 og 113 begge på 0 i tre læsninger i træk på en ledig maskine, sendes boot-pushen forfra — samme blokke, samme frame-politik, samme aktivitets-vagt; mindst 5 s mellem to, højst 3 signatur-udløste pr. 10 min. DIAGNOSE: *RE-PUSH n · 1-30*. `OURUI_NOBOOTPUSH=1` slår også den fra. Detaljer: [CHANGES-v6.6](#changes-v66) pkt. 4.

---

## 7a. Sikkerheds-assertioner: frame-politikken

MCU'en har **ingen range-checks** (cmd6 = `memcpy` + ACK), og nogle ord er triggere, som MCU'en konsumerer: 3/6/9 = start/tare, **26/27 = vægt-kalibrering (skriver MCU-flash)**, **30 = MCU-reboot**. Derfor går **hver** cmd6-frame — settings-GEM, aktivitets-start, sync før bryg, keep-awake, rollback — gennem én port, `wq_push_t()` → `frame_policy()`, før den kan nå UART'en:

| Lag | Regel | Håndhævet |
|---|---|---|
| Skrive-bånd | kun ord 0..31 og 112..114 | runtime |
| **TRIG_NEVER** = ord 26, 27, 30 | må **aldrig** være ≠ 0. Ingen kalder kan tillade dem | runtime **+ compile-time** (`_Static_assert`: ingen intent-maske overlapper TRIG_NEVER) |
| **TRIG_START** = ord 3, 6, 9, 16, 22, 24 | må kun være ≠ 0 i en frame, hvis kalderen eksplicit har navngivet ordet (`I_GRIND`, `I_BREW`, `I_WATER`, `I_STEAM`) | runtime + compile-time (intent ⊆ TRIG_START) |
| Pr.-ord interval (`word_ok`) | ord 4/11/14/17/23/25/2/112/113/114 skal ligge i `SET[]`-intervallet · ord 5/13/15 = 60 · ord 0 = 1 · ord 1 = 0 · **alle øvrige ord = 0** (7, 8, 10, 12, 18–21, 26–31) | runtime |

Konsekvenser: ourui kan ikke sende kalibrering, reboot, drain (28/29), back-flush (20), self-test (8), sleep/drain (ord1 = 1), power-off (ord0 = 0) eller manuel-vand-switch (19) — heller ikke ved en fremtidig kode-fejl. En settings-GEM kan aldrig starte en aktivitet. Den ene undtagelse, der er bygget ind: **STOP DAMP** klemmer mål-ordet ind i intervallet, så et stop aldrig kan blive afvist.

En afvist frame køes ikke, tælles i `wq.n_block` (DIAGNOSE: fjerde tal i **CMD6 OK·RTX·FEJL·BLOK**, rav hvis ≠ 0) og giver toasten *"AFVIST AF FRAME-POLITIK"* / *"WRITE AFVIST"*. Ved exit skriver ourui antallet på stdout. **Forventet værdi i al normal brug: 0.**

**Audit af v6's frames mod write-atlasset (PARITY §3)** — ingen afvigelser fundet; v6 sendte allerede 0 i ord 26/27/30:

| Frame | Atlas | Trigger-ord i framen | Resultat |
|---|---|---|---|
| KVÆRN `sub3 len4` | §3.1 | 3 = 1, 6 = 1 (tilsigtet) | byte-identisk m. BREW W1 |
| BRYG `sub5 len18` | §3.2 | 9 = 1, 22 = 1 (tilsigtet); 6 = 0, 16 = 0 | byte-identisk m. BREW W3 |
| VAND `sub9 len11` | §3.3 | 9 = 1, 16 = 1 (tilsigtet); 19 = 0 | byte-identisk m. HOTWATER W1 |
| DAMP start/stop `sub24 len8` | §3.4 | 24 = 1/0; **26 = 27 = 30 = 0**, 28/29/31 = 0 | byte-identisk m. SESSION §10.2/10.3 |
| sync `sub23 len1`, `sub112 len3` | §3.7–3.8 | ingen | ok |
| settings-GEM (10 ord-typer) | §3.7–3.8 | **dækker aldrig** ord 3/6/9/16/22/24/26/27/30 | ok — `grindMode` sendes som `sub2 len1`; stock's `sub2 len2` ville medsende start-ord 3 |
| keep-awake `sub0=[1]`, `sub1=[0]` | §3.8 | ingen | ok |

`sub18 len14` (steam mirror-sync, §3.5) og `sub4 len21/22` (NFC/preset, §3.6) bruges ikke af ourui.

---

## 8. Rendering og ydelse

* **Lag:** `bgpure` (vignette, bygges én gang) → `bgbase` (+ sidens statiske tekst) → `slay` (+ gauge) → `canvas` (+ widgets + overlays).
* **Gauge-LUT:** vinkel/dækning/glød pr. pixel beregnes én gang; pr. frame genberegnes kun pixels i det ændrede vinkel-interval.
* **Widgets** har en signatur (hash af indhold) og et fast rect; de tegnes kun, når signaturen ændrer sig eller deres rect rammes af damage.
* **Present:** kun dirty-rects kopieres til fb; pga. page-flip kopieres unionen af denne og forrige frames rects. Ingen ændring → intet flip. 30 fps under animation/touch, 10 fps ellers.

Måling på Mac (host), ikke på enheden:

| Scenarie | host ms/frame |
|---|---|
| bryg, animeret gauge + chart + tal | 0,03 (max 0,07) |
| fuld genopbygning af en side | 0,19 |
| page-slide | 0,17 |
| idle uden ændring | 0,001 |

Selv med en pessimistisk faktor 100 til Cortex-A7 ligger det under 30 ms-budgettet, men det er en ekstrapolation. **DIAGNOSE-siden viser den reelle frame-tid på enheden** (`FRAME MS NU/MAX`, max over 5 s) — den skal aflæses i testen (§11 C).

---

## 9. Tilbageværende antagelser og udskudt

**Antagelser der står tilbage efter afstemningen:**
* De to SKØN-max og volume-vælgerens mapping (§4.4).
* `grindMode` skrives som `sub2 len1`. Stock bruger `sub2 len2`, som også skriver ord 3 (grind-start-trigger); ourui holder bevidst trigger-ordet ude af framen. MCU'en er ren `memcpy`, så ord 2 får samme værdi.
* Betydningen af `Temperature` (0 = °F, 1 = °C) er spec'ens; `timemode` er nu bevist fra koden (§4a).
* Sprog-navnene vises på dansk (ENGELSK, TYSK, FRANSK, ITALIENSK, SPANSK, POLSK, ARABISK, KINESISK FORENKL., KINESISK TRAD., KOREANSK, JAPANSK) i spec'ens indeks-rækkefølge 0..10. ourui selv forbliver dansk — feltet styrer kun stock-UI'et efter dets genstart.

**Punkt 8 i afstemningen — sprog, tidszone, presets:**
| Emne | Beslutning | Hvorfor |
|---|---|---|
| **STOCK-SPROG** (v6.1: SPROG) | **oplåst**, 0..10 | Spec'en beviser tabellen: 11 sprog-strenge @ 0x166d84..0x166df4, indeks 0..10, konfidens H. Handleren [0xe1c78] er kun `setLangType(idx)`. Jeg har desuden tjekket at sprogvalget ikke påvirker OTA: `GetUpdateLatestVersion` [0x1375b4] bruger en **hardcodet** `"English"` i manifest-URL'en |
| **TIDSZONE** | fortsat **låst** | Spec'en giver `min`/`max` = null: "UTC-offset-index" uden tabel-størrelse. Feltet driver `time_sync_thread` + DST-beregning; et indeks uden for tabellen er ikke bevist harmløst |
| **Presets** (`presetStore`) | fortsat **udskudt** | Spec'en giver blok-rammen (`sub4 len22`, ord 4..25) og feltnavnene (`name, iconId, grind, dose, brewTemp, yield, steamLevel, milkTemp`, `currentIndex`, `presetAppliedIndex`), men **ikke** et komplet bevist format: `template_words` er null, der er 6 numeriske felter mod 5 ord (4/11/23/14/25) uden felt→ord-mapping eller enheder, og nuværende `presetStore` er `{}`, så der findes intet eksempel på serialiseringen. At gemme et preset kræver desuden at skrive et indlejret objekt og tilføje nøglen `presetAppliedIndex` — ourui's writer udskifter bevidst kun ét tal-token og tilføjer aldrig nøgler. At gætte her ville skrive en struktur, stock-UI'et måske ikke kan parse |
| **NFC** | udskudt | kræver MF522-driver + kortformat; spec'en anbefaler selv at udelade det |

**Øvrigt udskudt:**
| Emne | Hvorfor |
|---|---|
| Manuel stop af kværn / bryg / vand | Spec'en nævner kandidater for vand (`sub16 len1` + `sub19 len1` = 0), men kun fra statisk analyse. Bør fanges i en capture af stock-UI'ets stop, før det får en knap |
| Kværn på tid (`grindMode=2`), manuelt vand (`hotWaterMode=2`) | ingen observeret start-recept; start er spærret m. forklaring. Indstillingen kan godt gemmes — den gælder så stock-UI'et |
| Lydstyrke-preview, eget auto-dvale i ourui | lyd ejes af demo_v9; ourui holder maskinen vågen pr. design |
| Kalibreret tryk | kræver fysisk reference (BREW.md §7) |
| Kinetisk scroll | direkte træk er nok til 5–10 rækker |

---

## 10. Build og preview

```sh
cd artifacts/ourui
# device (statisk ARMv7 musl) — zig 0.17.1 brugt; 0 warnings
zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -o ourui_v6 ourui_v6.c
# enheds-binær + SIM-build (vært + statisk ARM) + liste over hvad der er ændret i enheds-build'et ift. v6.5
# + (hvis den lokale ARMv7-container findes) hele selftesten som ægte 32-bit ARM-binær
sh build_sim.sh
# alt det lokale i ét hug: preview-build + selftest + bench + spec-afstemning + alle previews
# + sammenligning m. v6.5 + mutations-prøver + config-format-tjek + circle-tjek + kontakt-ark
sh round_v6/build_previews.sh
# pr. iteration (§15 DEV-ITERATION-POLICY): sim-selftest + hurtigt kerne-subset, ét sprog, parallelt (ca. 45 s)
python3 ../../sim/selftest.py && (cd ../.. && python3 sim/run_e2e.py --all --quick --parallel 4 --ui-bin artifacts/ourui/ourui_v6_sim)
# HELE E2E-verifikationen (> 35 min) — KUN i CI/CD eller ved et release-milestone (§15)
python3 sim_e2e.py                   # -> sim/reports/v6.9/ (enkelte dele: --only calm | net | ref | …)
```
Enkeltvis: `/tmp/ourui_v6_preview [--lang en|da] <outdir> <config.json>` · `[--lang en|da] --selftest <config.json>` · `--bench <config.json>` · `--dump-table` (SET[] som TSV) · `--dump-strings` (streng-tabellen som TSV: id, klasse, EN, DA) · `--textw "TEKST"…` (pixelbredder) · `--is-ourui <fil>…` (BOOT-UI-vagtens binær-genkendelse; exit 0 = alle er ourui). PNG: `python3 round_v6/fb2png.py <fil.bin>…`.

**Lokale resultater v6.9** (hele tabellen i [CHANGES-v6.9](#changes-v69) pkt. 6): selftest **354 checks, 0 fejl** på engelsk og dansk — og **353 / 0** som ægte 32-bit ARM-binær; previews 67 × 2, circle-tjek 0 violations, 90 af 126 byte-identiske med v6.8 (resten tilsigtet); mutations-prøver 10/10; spec-afstemning ALT AFSTEMT; 0 warnings.

**Lokale resultater v6.6** (historisk; alt er kørt igen; hele tabellen står i [CHANGES-v6.6](#changes-v66) pkt. 1): selftest **304 checks, 0 fejl** på engelsk og dansk — og **303 / 0** som ægte 32-bit ARM-binær; previews 62 × 2, circle-tjek 0 violations, 102 af 114 byte-identiske med v6.5; spec-afstemning ALT AFSTEMT; 0 warnings.

**Lokale resultater v6.5** (historisk; detaljer i [CHANGES-v6.5](#changes-v65) pkt. 1):
* **Selftest (enheds-koden):** **241 checks, 0 fejl** på engelsk og dansk (v6.4: 199; de 42 nye dækker boot-pushen på enheden og RX-resync).
* **Previews:** 57 × 2 sprog; **circle-tjek 57/57 + 57/57, 0 violations**; 102 af 112 byte-identiske med v6.4, de 10 andre er DIAGNOSE + versions-strengen (`v64_compare.txt`); 1 nyt scenarie.
* **Enheds-build mod v6.4:** 14 ændrede og 14 nye stykker, alle tilsigtede (`sim_build_check.txt`). **Spec-afstemning: ALT AFSTEMT.** Config-format 5/5, boot-tjek 11/11, i18n-tjek 6/6. **0 warnings** (enhed, SIM-ARM, SIM-vært).
* **E2E mod simuleret MCU, rigtigt UI:** engelsk **23/23 scenarier / 589/589 checks**, dansk **23/23 / 589/589**; settings-matrix 188/188 i begge; boot-push 23/23; `sim/selftest.py` 107/107 (`sim/reports/v6.5/SUMMARY.md`).

**Lokale resultater v6.4** (historik; detaljer i [CHANGES-v6.4](#changes-v64) pkt. 1–2):
* **E2E mod simuleret MCU, rigtigt UI:** engelsk **22/22 scenarier / 553/553 checks**, dansk **22/22 / 553/553**; settings-matrix 188/188 i begge; `sim/selftest.py` 107/107 (`sim/reports/v6.4/SUMMARY.md`).
* **Selftest (enheds-koden):** **199 checks, 0 fejl** på engelsk og dansk (v6.3: 193; de 6 nye dækker SIM-boot-pushen).
* **Previews:** 56 × 2 sprog; **circle-tjek 56/56 + 56/56, 0 violations**; 100 af 112 byte-identiske med v6.3, de 12 andre tilsigtede (`v63_compare.txt`).
* **Enheds-build = v6.3 + to tekster:** token-sammenligning efter preprocessoren, 10 forskelle (`sim_build_check.txt`). **Spec-afstemning: ALT AFSTEMT.** Config-format 5/5, boot-tjek 10/10, i18n-tjek 6/6. **0 warnings** (enhed, SIM-ARM, SIM-vært).

**Lokale resultater v6.3** (historik; alt er kørt igen i v6.4 og er fortsat grønt — detaljer i [CHANGES-v6.3](#changes-v63) pkt. 8):
* **Selftest:** **193 checks, 0 fejl** — kørt to gange, med engelsk (`selftest.txt`) og dansk (`selftest_da.txt`) som UI-sprog (v6.2: 152). De 41 nye er i18n-tabellen, sprog-filen, UI-SPROG-rækken og layout i begge sprog.
* **Previews:** **56 scenarier × 2 sprog = 112** (`round_v6/` engelsk, `round_v6/da/` dansk). **Circle-tjek 56/56 + 56/56, 0 violations**, max ikke-sort radius 234,67 (`check_circle.txt`, `check_circle_da.txt`).
* **Spec-afstemning:** uændret, **ALT AFSTEMT** (41 → 19 bevist, 2 skøn, 2 låst, 1 sensor, 17 udeladt). **Config-format:** 5/5 PASS. **Boot-tjek:** 8/8 PASS. **i18n-tjek** (`i18n_check.txt`): 6/6 PASS.
* **Statisk build:** 0 warnings, 0 errors.

**Lokale resultater v6.2 (historik — de dele der står herunder, er kørt igen i v6.3 og er fortsat grønne; tallene er v6.2's):**
* **Selftest** (`round_v6/selftest.txt`): **152 checks, 0 fejl** (v6.1: 87). De 65 nye dækker flag-logik, exit-påmindelsen, runtime-skiftet og layout.
  * *Frame-vektorer:* de 6 observerede frames (keep-awake, KVÆRN, BRYG, VAND, DAMP start/stop) er stadig byte-identiske inkl. CRC; skrive-bånd, retransmit, timeout, ACK.
  * *Pakning:* ord112/113 for alle kombinationer (switch 0–1, tryk 3–4, tid 5–60, pause 0–255) + 5 nye byte-vektorer m. uafhængigt beregnet CRC (GEM af pre-infusion FRA, tryk 3, tid 7, gruppe-temp 80, kværn-tilstand).
  * *Trigger-ord-audit:* 36 settings-frames (min/max/aktuel for alle 12 MCU-rækker) dækker aldrig et trigger- eller start-ord; hver aktivitets-frame har præcis sine tilsigtede start-ord; damp-blokkens ord 26–31 er 0; 13 negative tests (ord30 = 1, ord26/27 ≠ 0, start-ord uden eller med forkert intent, service-ord, værdier uden for spec, pakkede ord) bliver alle afvist og talt.
  * *Boot-push-ækvivalens:* en uafhængig model af `model_data_init` (config → spejl) viser, at hver GEM skriver præcis de ord og værdier, stock's boot-push ville sætte fra samme config.
  * *Tabel/editor:* fjernede felter er væk, sensor/låst/oplåst-status, stepper-clamp og trin-gitter, layout-tjek (alle tekster i detalje-visningen passer i cirklens korde for alle 24 rækker).
  * *Config-persist:* som v6 + **alle 21 redigerbare felter gemt som min → max → original**, md5 tilbage på baseline `<config-md5-redacted>`, pristine backup urørt, én log-linje pr. write (69), ukendte nøgler (`theme`) afvises.
  * *Uden for spec:* beviste grænser udvides ikke, skøn udvides kun til loftet, enum uden for tabellen giver intet opslag uden for navne-arrayet, start spærres, og frame-politikken afviser bryg-blokken, selv hvis UI-vagten omgås.
* **Spec-afstemning** (`round_v6/parity_check.txt`): 41 spec-parametre → 19 bevist, 2 skøn, 2 låst, 1 sensor, 17 udeladt m. begrundelse. **ALT AFSTEMT.**
* **Config-format** (`round_v6/config_format_check.txt`): 4/4 PASS — ourui's fil er byte-identisk med `cJSON_Print` af samme ændring.
* **Circle-tjek** (`round_v6/check_circle.txt`): **51/51 previews OK, 0 violations**, max ikke-sort radius 234,67.
* **Statisk build:** 0 warnings, 0 errors.
* Gem-scenarierne i previewet kører den ægte persist-kode mod **kopier** (`round_v6/_preview_config*.json`); `artifacts/config.json` er urørt (md5 uændret).

Previews (51 stk., `.bin` + `.png`; `39`–`50` er nye i v6.2, se CHANGES-v6.2 pkt. 8): `00_splash` · `01–13_home_*` · `14–17_list_*` (BRYG, scrollet, DAMP · VAND m. sensor-række, SYSTEM) · `18–25_detail_*` (uændret, ændret, enum, **låst tidszone**, hold-acceleration, gemt-toast, gemmer, ingen-ACK) · `26_diag` (m. BLOK-tæller) · `27–29` · **nye i v6.1:** `30_detail_estimate_hint` (MAX = SKØN) · `31_detail_sensor_vandtemp` · `32_detail_breaktime_locked` · `33_detail_language` · `34_detail_steam_gear` · `35_list_system_scrolled` · `36_list_bryg_out_of_spec` · `37_detail_out_of_spec` · `38_home_start_blocked_out_of_spec`. **Nye i v6.3:** `51_detail_ui_language` · `52_ui_language_live_preview` · `53_ui_language_saved_toast` · `54_home_other_language` · `55_diag_about` — og hele sættet findes nu i to sprog (`round_v6/` = engelsk, `round_v6/da/` = dansk).

---

## 11. ON-DEVICE TESTPLAN (til integrations-ingeniøren)

> **Sprog (v6.3):** en frisk installation starter på **engelsk**. Planen citerer de danske tekster — lav derfor O1–O2 først (sæt UI-SPROG = DANSK), eller slå den engelske ordlyd op i Appendiks A.

Intet af dette er udført. Rækkefølgen går fra ufarligt til aktuator-kørsel; stop ved første afvigelse.

**A. Forberedelse**
- [ ] Baseline: `pidof demo_v9`, `md5sum /customer/config.json` (forventet `<config-md5-redacted>`), `cat /sys/class/pwm/pwmchip0/pwm2/duty_cycle` (forventet 37), fb-capture.
- [ ] Hent en lokal kopi af `/customer/config.json` (uafhængig af ourui's egne backups).
- [ ] Upload `ourui_v6` til `/tmp`, md5-round-trip. Installér først efter B–F: v4 → `ourui_v4.bin`, v6 → `ourui` (run.sh uændret).

**B. Read-only runde** — `OURUI_READONLY=1 OURUI_NOGRAB=1`
- [ ] Splash → HJEM vises; fb-capture af alle 5 sider; `check_circle.py` = 0 violations.
- [ ] DIAGNOSE: LINK OK, RX OK stiger, RX CRC / RESYNC = 0 / 0, CMD6 = 0/0/0/0, **FRAME MS MAX < 30**; scrol én række: BOOT-PUSH = FRA / FRA (read-only sender intet).
- [ ] Værdierne i de tre lister svarer til `config.json`; ingen række er rød (uden for spec); `SPEJL 112-114` viser `0104 0605 70` (eller `0104 0606 70`, hvis der er brygget siden stock's boot — se L).
- [ ] VAND-SENSOR (DAMP · VAND) følger BRYGKEDEL-tallet på HJEM; rækken har ingen −/+.
- [ ] TIDSFORMAT viser **12-TIMERS** (config `timemode=1`), og stock-UI'ets ur er 12-timers m. A.M./P.M. Stemmer det ikke, er fundet i §4a forkert: meld tilbage.
- [ ] Swipe + kant-tap skifter side; SYSTEM → *SKIFT TIL ORIGINAL NU* → SKIFT NU (v6.8; før: REVERT) → demo_v9 SIGCONT, stock-UI tegner sig selv.

**C. Skrive-runde uden aktuatorer**
- [ ] Start uden `OURUI_READONLY`. DIAGNOSE: VÅGEN = JA, WORK_STA = `0101`, CMD6 OK ≥ 4 (PowerOn, vågen og fra v6.5 de to boot-push-blokke — se Q), FEJL = 0, **BLOK = 0**. Kedlerne holder temperatur over 5 min (v4-buggen ville lade dem køle).
- [ ] FRAME MS under slide, scroll og hold-gentagelse: aflæs MAX.

**D. EVIOCGRAB** (fysisk finger, uden `OURUI_NOGRAB`)
- [ ] Tryk rundt på HJEM og i lister, derefter *SKIFT TIL ORIGINAL NU* (v6.8; før: REVERT): stock-UI'et må **ikke** reagere på nogen af trykkene.
- [ ] `kill -9` på ourui: grebet frigives, stock-UI'et reagerer normalt på touch efter SIGCONT.

**E. Settings-matrix** — for hver række: åbn → ændr → GEM → verificér → sæt tilbage → GEM → verificér

| Parameter | Test-ændring | Verificér efter GEM |
|---|---|---|
| LYSSTYRKE | 9 → 5 → 9 | duty 37 → 65 under redigering (live), 65 efter GEM, 37 efter revert; ANNULLER gendanner duty |
| BRYG-TEMP | 87,0 → 88,0 → 87,0 | DIAGNOSE CMD6 OK +1; `ord 11` = 880 (HJEM under-linje viser 88.0°) |
| GRUPPEHOVED | 70 → 80 → 70 | `SPEJL …/114` = 80; kan ikke komme over 80 (skøn) |
| PRE-INF. TRYK | 4 → 3 → 4 | `SPEJL 112` = `0103` → `0104`; kan ikke vælge andet end 3/4 |
| DAMP-NIVEAU | MEDIUM → STÆRK → MEDIUM | ord 14 = 4 → 3 |
| VARMT VAND | 40 → 50 → 40 g | ord 17 = 500 → 400; trin à 10 g |
| STOCK-SPROG (kun config) | ENGELSK → TYSK → ENGELSK | kun config (`language` 0 → 1 → 0); se F for stock-UI |
| PRE-INF. TID | 6 → 7 → 6 | `SPEJL 113` = `0705` |
| PRE-INFUSION | TIL → FRA → TIL | `SPEJL 112` = `0004` → `0104` |
| KVÆRN-DOSIS | 18,5 → 18,0 → 18,5 | ord 4 |
| MÆLKETEMP | 65,0 → 60,0 → 65,0 | ord 25 |
| LYDSTYRKE (kun config) | 31 → 30 → 31 | kun config ændret, CMD6-tæller uændret |

For **hver** GEM:
- [ ] `md5sum /customer/config.json` = `md5_after` i sidste linje af `cfgbak/config_writes.log`.
- [ ] `diff` mod forrige version viser **kun** det ene tal ændret.
- [ ] `cfgbak/config.json.orig` har stadig baseline-md5; `config.json.bak` = versionen før.
- [ ] Ingen `/customer/.ourui_cfg.tmp` efterladt.
- [ ] Efter hele matricen: `md5sum /customer/config.json` = baseline.
- [ ] Fejl-sti: stop MCU-svar er ikke muligt at fremprovokere sikkert — spring over; dækket af selftest.

**F. Reload-semantik**
- [ ] Gem én ændring, *SKIFT TIL ORIGINAL NU* (v6.8; før: REVERT): stock-UI viser stadig den **gamle** værdi (forventet), og ourui har skrevet genstart-påmindelsen på stdout.
- [ ] `kill $(pidof demo_v9)` → watchdog genstarter → stock-UI viser den **nye** værdi → sæt tilbage via ourui eller stock-UI, verificér baseline-md5.
- [ ] Valgfrit: STOCK-SPROG → TYSK, genstart demo_v9, stock-UI er på tysk; sæt tilbage til ENGELSK + genstart.

**G. Aktiviteter** (supervised, vand i tanken, hånd på afbryderen)
- [ ] KVÆRN (portafilter i): hold knappen → fase KVÆRNER inden 3 s → auto-stop nær dosis → FÆRDIG. Sammenlign TX-frame m. `aa 55 06 03 04 01 00 b9 00 3c 00 01 00 5d 6e`.
- [ ] VAND (kop under): auto-stop nær 40 g.
- [ ] BRYG: PRE-INFUSION (blå) ≈ 6 s → EKSTRAKTION (kobber) → auto-stop ≈ 34–37 g; indre ring når 100 %.
- [ ] Kort tap på en aktivitets-knap starter **ikke** noget.

**H. Damp-session med mælk** (recepten er siden live-bevist med **stock-UI'et** — STEAMLIVE.md; med ourui er den stadig ikke kørt. Brug **R2–R6** for det, v6.6 viser; punkterne her er fortsat gyldige for frames og stop)
- [ ] Kold mælk i kanden, sonde/dyse nede. Hold DAMP → *"STARTER DAMP…"* (v6.5 og ældre: *"DAMP ARMERET · ÅBN DAMPHANEN"*).
- [ ] ~~Notér: starter dampen på write alene, eller først når hanen åbnes?~~ Besvaret live: på writen alene, efter ca. 0,2 s. Åbnes wand'en uden en write, er det en flush (R4).
- [ ] Under damp: WORK_STA har bits 5+14 (`4121`), fase DAMP, hero = mælk-temp stiger, markør ved 65 °C.
- [ ] **Auto-stop:** stopper MCU'en selv ved ≥ 65,0 °C? Notér sluttemperatur (overshoot) og om ord 24 går til 0.
- [ ] **Manuel stop** i en ny session: tryk STOP DAMP ved ~45 °C → stopper dampen? TX = `aa 55 06 18 08 00 00 8a 02 … 0c f7`.
- [ ] Timeout-adfærd, hvis dampen ikke starter (UI nulstiller efter 15 s; hvad gør MCU'en med ord 24 = 1? — stadig åbent, CHANGES-v6.6 pkt. 8). **Hvis ord 24 bliver stående på 1, så tryk ikke bare videre — skriv 0 via STOP eller stock-UI.**

**J. Trigger-ord-audit på enheden** (PARITY §6.2 — ord 26/27/30 må aldrig være ≠ 0)
- [ ] Optag ourui's TX under hele E + G + H (UART-sniff eller `strace -e write` på ourui-PID'en). Dekod hver `AA 55 06 SUB LEN …`-frame og bekræft: ingen frame har ord 26, 27 eller 30 ≠ 0; ord 3/6/9 ≠ 0 forekommer **kun** i de tre start-blokke (`sub3 len4`, `sub5 len18`, `sub9 len11`); ingen settings-GEM dækker et start-ord.
- [ ] DIAGNOSE efter hele sessionen: **BLOK = 0** (politikken har intet haft at afvise) og FEJL = 0.
- [ ] Ingen MCU-reboot: RX OK stiger uafbrudt, LINK har ikke været NEDE, kedel-temperaturerne har ikke haft et dyk.
- [ ] Ingen utilsigtet kalibrering: læg en kendt vægt på før og efter sessionen — VÆGT på HJEM viser det samme.

**K. Boot-push-sync** (PARITY §1.4 — første live-bekræftelse af den statisk fundne boot-push)
- [ ] Gem BRYG-TEMP 87,0 → 88,0 og GRUPPEHOVED 70 → 80 i ourui (controller + config har nu 880 / 80). *SKIFT TIL ORIGINAL NU* (v6.8; før: REVERT).
- [ ] Mens ourui **ikke** kører: gendan config fra `cfgbak/config.json.orig` med tmp+mv (REVERT.md), men genstart **ikke** demo_v9 endnu. Nu er config = 870/70, controller = 880/80.
- [ ] `kill $(pidof demo_v9)` og optag UART'en under opstarten: forvent `AA 55 06 01 1E …` (sub1 len30) og kort efter `AA 55 06 70 03 04 01 05 06 46 00 C7 02` (sub112 len3).
- [ ] Start ourui m. `OURUI_READONLY=1` (ingen writes fra ourui): HJEM-underlinjen viser *"BRYG-TEMP SAT TIL 87.0°"* (ord 11 = 870) og `SPEJL …/114` = 70. Controlleren er altså synket fra config af stock's boot, ikke af ourui.
- [ ] Hvis ord 11 stadig er 880: boot-pushen findes ikke i den form spec'en beskriver — meld tilbage; §5 skal så rettes.

**L. breakTime-quirk** (PARITY §4 — MCU 0x1E10 overskriver ord113 lo)
- [ ] Efter en frisk demo_v9-opstart: `SPEJL 113` = `0605` (tid 6, pause 5).
- [ ] Bryg én gang m. PRE-INFUSION = TIL. Under eller efter brygget: `SPEJL 113` = **`0606`** — lo-byten er blevet til pre-inf. tiden. Notér varigheden af fasen PRE-INFUSION (TID-feltet).
- [ ] Sæt pause-tiden til fx 9 s i **stock-UI'et** (ourui har bevidst ingen editor), genstart ourui: PAUSE-TID viser 9, `SPEJL 113` = `0609`. Bryg igen: `SPEJL 113` = `0606`, og PRE-INFUSION varer det samme som før. Det er quirken demonstreret.
- [ ] Modprøve: sæt PRE-INF. TID 6 → 8 i ourui og bryg: `SPEJL 113` = `0808`, og PRE-INFUSION varer ca. 2 s længere.
- [ ] PRE-INFUSION = FRA og bryg: `SPEJL 113` lo-byte = `00`, ingen blå fase. Sæt alt tilbage.

**M. Luk skønnene** (PARITY §6.6)
- [ ] I stock-UI'et: tæl options i pickerne for pre-inf. tid, pre-inf. tryk, pause og gruppe-temp, og notér højeste værdi. Notér også hvilke config-værdier den diskrete lyd-vælger (High/Medium/Low/Mute) skriver. Resultatet lukker de to SF_EST-rækker og volume-hinten.

**N. Boot-slot og runtime-skift (v6.2)** — efter `boot_install.py install` (BOOT.md §4); intet af det er kørt
- [ ] **N1 · Vagt i A-boot via run.sh:** SYSTEM nederst viser BOOT-UI = ORIGINAL (redigerbar, hvis ourui ligger som `/new_customer/demo_v9`). DIAGNOSE nederst: FLAG = FINDES IKKE, STARTET FRA = RUN.SH, SLOT B = MERAKI+ FUNDET, SWITCHER-DAEMON = KØRER, STOCK-PROCES = PAUSET.
- [ ] **N2 · Flag:** vælg MERAKI+ → GEM; `ls -l /customer/new` (tom fil), `cat /new_customer/ourui/cfgbak/boot_ui.log`. Vælg ORIGINAL → GEM; filen er væk. `md5sum /customer/demo.sh /customer/config.json` uændrede. Genstart med flaget sat → ourui booter (DIAGNOSE: STARTET FRA = SLOT B, STOCK-PROCES = INGEN).
- [ ] **N3 · `LD_LIBRARY_PATH` (lukker afvigelsen i CHANGES-v6.2 pkt. 2):** i slot B-boot: `tr '\0' '\n' </proc/$(pidof demo_v9)/environ | grep LD_LIBRARY`. Er der et præfiks foran `/new_customer/libs`, er det dét, ourui bevarer. Kør derefter med `OURUI_SWITCH_DRY=1`, tryk SKIFT NU og læs stien i `boot_ui.log`.
- [ ] **N4 · Runtime-skift B→A:** i slot B-boot, HJEM = KLAR: SYSTEM → SKIFT TIL ORIGINAL NU → SKIFT NU. Forvent *"SKIFTER TIL ORIGINAL"*, derefter stock-UI'et uden genstart. `for p in $(pidof demo_v9); do readlink /proc/$p/exe; done` = kun `/customer/demo_v9`; `ls /proc/$(pidof demo_v9)/fd` indeholder ingen fd arvet fra ourui; **touch virker i stock-UI'et** (det er grab-testen); `cat /new_customer/ourui/cfgbak/boot_ui.log`. Optag UART under skiftet: forvent stock's boot-push (`AA 55 06 01 1E …`), ingen halve frames fra ourui.
- [ ] **N5 · Watchdog-racen:** gentag N4 ti gange (tilbage med gestussen). Der må aldrig køre to `demo_v9` bagefter, og ourui må ikke komme tilbage af sig selv.
- [ ] **N6 · Tilbage med gestus:** 3 × 1,6 s i (75,75) → ourui; DIAGNOSE viser hændelsen fra `/tmp/switcher.log`.
- [ ] **N7 · Kendt konsekvens:** efter N4, `kill $(pidof demo_v9)` → watchdog'en starter **ourui** (slot B), ikke stock. Forventet.
- [ ] **N8 · (v6.8: udgået — REVERT-knappen er fjernet.)** Erstattet af: HJEM har ingen tryk-zone, hvor pillen sad (tryk 150..330 × 404..480 gør intet); *SKIFT TIL ORIGINAL NU* starter stock i slot B-boot og er en ren exit i run.sh-tilstand.
- [ ] **N9 · Exit-påmindelse:** run.sh-tilstand, gem én indstilling, *SKIFT TIL ORIGINAL NU* → SKIFT NU (v6.8; før: REVERT) → påmindelsen vises; AFSLUT → stock-UI'et viser den gamle værdi; `kill $(pidof demo_v9)` → den nye. Gentag uden GEM: ingen påmindelse. Gentag efter en maskin-genstart (flaget ligger der endnu): ingen påmindelse.
- [ ] **N10 · Spærring:** start damp, prøv SKIFT NU → *"VENT TIL AKTIVITET ER SLUT"*, ourui kører videre.
- [ ] **N11 · Fejlvej (valgfri, kræver at man tør):** `chmod -x /customer/demo_v9` gør rækken låst; sæt x-bitten tilbage straks efter.

**O. Sprog og produktnavn (v6.3)** — intet af det er kørt på enheden
- [ ] **O1 · Default:** første start uden `/new_customer/ourui/lang` (`ls` viser at filen ikke findes): splash viser ordmærket MERAKI med "+"-badge, *"ADVANCED BREWING INTERFACE"* og *"V6.4 · CONFIG LOADED"*; HJEM er på engelsk med titellinjen MERAKI+ over fase-chippen; DIAGNOSE nederst (ABOUT): *"ENGLISH · DEFAULT (NO FILE)"*.
- [ ] **O2 · Live-skift:** SYSTEM → UI LANGUAGE (øverste række) → **+**: hele skærmen slår straks om til dansk (titel UI-SPROG, knapper ANNULLER / GEM) → GEM → toast *"SPROG GEMT · DANSK"*. Ingen genstart. `cat /new_customer/ourui/lang` = `da`, `wc -c` = 2. Gennemse alle 5 sider på dansk; fb-capture af dem + `check_circle.py` = 0 violations.
- [ ] **O3 · Uafhængigt af stock:** efter O2 er `md5sum /customer/config.json` uændret, `grep language /customer/config.json` uændret, `cfgbak/config_writes.log` har ingen ny linje, der er intet nyt `RESTART_NEEDED`, og DIAGNOSE's CMD6-tællere er de samme (ingen frame sendt).
- [ ] **O4 · Huskes:** afslut (v6.8: *SKIFT TIL ORIGINAL NU*) og start igen (eller genstart i slot B-boot): dansk fra første frame (splash: *"AVANCERET BRYGGE-INTERFACE"*); DIAGNOSE / OM: *"DANSK · FRA FIL"*.
- [ ] **O5 · Fallback:** `echo de > /new_customer/ourui/lang` → start: engelsk, ABOUT viser *"ENGLISH · UNKNOWN FILE VALUE"* i rav. `rm` filen → *"DEFAULT (NO FILE)"*. `echo da > …` (med linjeskift) → dansk.
- [ ] **O6 · ANNULLER:** UI-SPROG → skift → ANNULLER: sproget går tilbage, filen er uændret.
- [ ] **O7 · STOCK-SPROG er noget andet:** STOCK-SPROG → TYSK → GEM ændrer kun `language` i config (som i E); Meraki+ bliver på sit eget sprog. Sæt tilbage.
- [ ] **O8 · Produktnavn:** gennemse fb-captures af alle sider på begge sprog: navnet er Meraki+ overalt, og der står ingen generations-betegnelse nogen steder.
- [ ] **O9 · °F-layout (strammet i v6.3):** TEMP-ENHED = °F → i BRYG- og DAMP · VAND-listerne rører label og værdi ikke hinanden; HJEM-underlinjen under damp er ikke klippet. Sæt tilbage til °C.
- [ ] **O10 · Frame-tid:** aflæs FRAME MS MAX lige efter et sprogskift (fuld gentegning + nye glyffer i cachen).

**P. v6.4** — enheds-binæren gør det samme som v6.3; kun to tekster er nye
- [ ] **P1 · Version:** splash og DIAGNOSE viser *V6.5* (O1 nævner v6.4's streng; fra v6.5 står der *V6.5 · CONFIG LOADED*). `strings /new_customer/demo_v9 | grep -c OURUI_SIM` = 0 (det er enheds-binæren, ikke SIM-build'et).
- [ ] **P2 · Toast:** fremprovokér et GEM uden ACK, hvis det kan gøres sikkert (ellers spring over): toasten er *"GEM FEJLET · INGEN ACK · INTET GEMT"*, og config er uændret.
- [ ] **P3 · (v6.4; beslutningen er taget i v6.5 — brug den som modprøve, Q6) Mål før beslutningen om boot-push (CHANGES-v6.4 pkt. 6):** kold start i slot B-boot; før noget er gemt eller brygget: aflæs ord 4, 11, 14, 17, 23, 25 og 112–114 (`sdump.py`, eller HJEM: *BRYG-TEMP SAT TIL …*) og sammenlign med config. Forventet uden boot-push: MCU'ens startværdier (ord 11 = 920, ord 114 = 50), ikke config'ens (870 / 70).

**Q. v6.5: boot-push og RX-resync** — intet af det er kørt på enheden
- [ ] **Q1 · Version og nye celler:** splash og DIAGNOSE viser *V6.5*. DIAGNOSE: *RX CRC / RESYNC* = `0 / 0`; scrol én række ned: *BOOT-PUSH 1-30* = OK og *BOOT-PUSH 112-114* = OK (grønne).
- [ ] **Q2 · run.sh-tilstand, ledig maskine — optag ourui's TX** (UART-sniff eller `strace -e write` på ourui-PID'en) fra start: efter `aa 55 06 00 01 01 00 c5 5f` og `aa 55 06 01 01 00 00 c5 33` kommer **`aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 … 62 3e`** (67 bytes, sub1 len30) og **`aa 55 06 70 03 04 01 05 06 46 00 c7 02`** (sub112 len3) — med baseline-config. Hver har én ACK (`aa 55 86 …` med samme payload). DIAGNOSE: CMD6 = `4 / 0 / 0 / 0`. Notér ACK-latensen for 30-ords-blokken, og om RTX er 0.
- [ ] **Q3 · Samme blok som stock:** sammenlign Q2's to frames byte for byte med stock-UI'ets egne fra testplan K (samme config). Afviger et ord, er PARITY §1.3–1.4 ufuldstændig: meld tilbage, og kør med `OURUI_NOBOOTPUSH=1` indtil videre.
- [ ] **Q4 · Ingen bivirkning i run.sh-tilstand:** efter Q2 er WORK_STA fortsat `0101`, kedel-temperaturerne holder, RX OK stiger uafbrudt (ingen MCU-reboot), VÆGT på HJEM er uændret (ingen kalibrering), `SPEJL 112-114` = `0104 0605 70`.
- [ ] **Q5 · Slot B-boot, kold start (det rettelsen er til for):** sæt flaget, sluk og tænd. Når HJEM vises: under-linjen siger **ikke** *BRYG-TEMP SAT TIL 92.0°*; `sdump.py` (eller DIAGNOSE) viser ord 11 = 870, ord 4 = 185, ord 14 = 3, ord 17 = 400, ord 23 = 360, ord 25 = 650 og `SPEJL 112-114` = `0104 0605 70`; BOOT-PUSH = OK / OK. Brygkedlen varmer mod 87,0 °C. Notér work_sta under opvarmningen — er der bits ud over `0101`?
- [ ] **Q6 · Modprøve (valgfri):** samme kolde start med v6.4-binæren (`ourui_v6_4.bin`) — forventet ord 11 = 920 og ord 114 = 50 (testplan P3). Det er forskellen, v6.5 lukker.
- [ ] **Q7 · Aktivitets-vagten:** start et bryg i **stock-UI'et**, og start `run.sh` mens det løber. Brygget fortsætter til sit vægt-mål uden afbrydelse; DIAGNOSE: BOOT-PUSH = SPRUNGET OVER / SPRUNGET OVER, CMD6 OK = 2. Gentag med damp (dyse i mælk): dampen stopper ikke, når ourui starter.
- [ ] **Q8 · Config uden for spec:** med en kopi af config, hvor `brewTemper` = 1200 (`OURUI_CONFIG=<kopi>`): BOOT-PUSH 1-30 = AFVIST, 112-114 = OK, BLOK = 1, og ord 11 på controlleren er uændret. Ret værdien med et GEM i ourui: BOOT-PUSH 1-30 skifter til OK.
- [ ] **Q9 · Slå fra:** `OURUI_NOBOOTPUSH=1` → BOOT-PUSH = FRA / FRA, CMD6 OK = 2 (v6.4-adfærden).
- [ ] **Q10 · RESYNC over en lang session:** lad ourui køre gennem E + G + H og aflæs *RX CRC / RESYNC* til sidst. Forventet `0 / 0`. Er RESYNC over 0, så notér begge tal, og om LINK har været NEDE — efter v6.5 må et enkelt tal dér ikke give OFFLINE.

**R. v6.6: fase-semantik, ur og re-push** — intet af det er kørt på enheden
- [ ] **R1 · Version og uændret DIAGNOSE:** splash og DIAGNOSE viser *V6.6*. Boot-push-rækken hedder *BOOT-PUSH 1-30* / *BOOT-PUSH 112-114* (OK / OK) — **ikke** *RE-PUSH …*. `strings /new_customer/demo_v9 | grep -c OURUI_SIM` = 0.
- [ ] **R2 · Latchet damp** (dyse i kold mælk; start med DAMP-knappen, **rør ikke wand'en**): under-linjen viser kort *STARTER DAMP…*; inden for ca. 0,3 s er chippen **DAMP·AUTO**, label'en *MÆLK · MÅL 65.0°*, under-linjen *AUTO-STOP VED 65.0° · ELLER STOP*, og STOP DAMP er synlig. Chippen må på intet tidspunkt vise *DAMP·WAND*. Dampen stopper selv ved ca. 65 °C; CMD6 OK er kun steget med 1 (start-writen) — ourui har ikke sendt nogen stop. Derefter FÆRDIG med *FÆRDIG PÅ … S · STIGER ≈4° TIL*. Notér, hvor højt hero når de næste ~12 s (STEAMLIVE: 68,9 °C).
- [ ] **R3 · Manuel STOP under latchet damp:** start igen, tryk STOP DAMP ved ca. 40 °C: toast *STOP SENDT · ORD 24 = 0*; chippen forbliver *DAMP·AUTO*, til dampen er stoppet (intet *DAMP·WAND*-blink); derefter FÆRDIG.
- [ ] **R4 · Wand / flush alene** (rør ikke skærmen; åbn wand'en 3–4 s): chippen er **DAMP·WAND**, label'en *MÆLKEPROBE · INTET MÅL*, under-linjen *INGEN AUTO-STOP · SLIP WAND*; **ingen STOP-knap** — de fire knapper står der, DAMP markeret; CMD6-tællerne står stille. Slip: FÆRDIG med *FÆRDIG PÅ … S* (uden note). På DIAGNOSE under flush: FASE = *DAMP·WAND*, WORK_STA = `4121` (evt. med pulser).
- [ ] **R5 · Wand over målet:** hold wand'en åben mod proben, til hero viser over 65 °C: dampen fortsætter (som i STEAMLIVE §6.2); ourui viser fortsat *DAMP·WAND*, ingen FÆRDIG, ingen frame. Start-knapperne svarer *AKTIVITET I GANG*.
- [ ] **R6 · Samtidige faser:** start et bryg; åbn wand'en i ca. 3 s midt i ekstraktionen. Chip **EKSTRAKTION**, lige under den mærket **DAMP·WAND**; hero er vægten og stiger videre; stat-rækken er *TID · ≈BAR · EST. · MÆLK*; BRYG **og** DAMP er markeret; ingen STOP-knap. Slip: mærket forsvinder, FLOW er tilbage, brygget kører til sit mål, og *FÆRDIG PÅ …* er hele bryg-tiden. Notér WORK_STA, hvis DIAGNOSE nås (`5327`).
- [ ] **R7 · Ingen falsk re-push:** efter hele sessionen (E + G + H + R2–R6): DIAGNOSE-rækken hedder stadig *BOOT-PUSH 1-30*. Står der *RE-PUSH n · 1-30*: notér tallet, cellernes udfald, WORK_STA, og hvad der skete lige før — det betyder en MCU-genstart, eller at controlleren også sender `0x89` uden for sin opstart (ikke set i nogen capture).
- [ ] **R8 · Slot B-boot med wand'en åben (valgfri):** kold start i slot B-boot, hold wand'en åben, mens ourui starter: BOOT-PUSH = SPRUNGET OVER (vagten). Slip wand'en: inden for ca. 5 s hedder rækken *RE-PUSH 1 · 1-30* med OK / OK, ord 11 = 870, og `SPEJL 112-114` = `0104 0605 70`. Under-linjen på HJEM siger ikke længere *BRYG-TEMP SAT TIL 92.0°*.
- [ ] **R9 · Lang drift (uret):** kan ikke forceres på enheden. Lad ourui køre så længe som muligt i slot B-boot (helst over 25 døgn); notér `cat /proc/uptime`, og at HJEM stadig opdaterer, RX OK stiger, FRAME MS er som før, og keep-awake holder maskinen vågen. **v6.5 og ældre må ikke bruges til permanent slot B-boot.**
- [ ] **R10 · Modprøve (valgfri):** R2 med `ourui_v6_5.bin` viser chippen *DAMP* og under-linjen *DAMP ARMERET · ÅBN DAMPHANEN*; R4 viser *DAMP* med *AUTO-STOP VED 65.0° · ELLER STOP* og en STOP-knap, der intet gør.

**I. Afslutning**
- [ ] Afslut (v6.8: *SKIFT TIL ORIGINAL NU*), `pidof demo_v9` uændret (medmindre F/K/L genstartede den), config-md5 = baseline, PWM duty = 37, `/tmp` ryddet, `cfgbak/` gennemset, `RESTART_NEEDED` fjernet efter sidste demo_v9-genstart.


### S · Manuel STOP (v6.13) — kør først, når maskinen er tændt og ledig

Kør `python3 verify_stop_v613.py --plan` (ingen kontakt med maskinen) og derefter `python3 verify_stop_v613.py`.

Hvad scriptet gør:
* lægger v6.13-binæren og en kopi af run.sh i `/tmp/ourui_s/` og starter ourui med `OURUI_NOBOOTPUSH=1` og `OURUI_NONET=1`; stock-UI'et SIGSTOP'es
* sætter strace (passiv) på ourui's `read`/`write` af `/dev/ttyS1`
* beder en person ved maskinen om at holde VAND-ikonet og trykke **STOP VAND** efter ca. 2 s (ingen touch-injektion; ourui griber touch som altid)
* stopper ourui, så run.sh's trap SIGCONT'er stock-UI'et
* henter loggen og sletter `/tmp/ourui_s/`
* afgør bagefter tjekkene i [§18.1](#181-stop-frames--byte-for-byte-med-bevis): stop-framen byte for byte, ACK, work_sta-bits ryddet, flow faldet, ingen andre writes, stock-PID uændret, config og flag uændrede

Der er ingen reboot og ingen skrivning til config, flag eller slots. Et sikkerhedsnet stopper ourui efter 240 s. Resultatet havner i `stop_v613_ondevice/<aktivitet>_<tid>/PROOF.md`.

- [ ] **S1 · vand** (kop under udløbet): `verify_stop_v613.py`. Forventet: `aa 55 06 10 01 00 00 c0 0f` → ACK ~20–280 ms → work_sta `030B` → `0101` (bit 3 ryddet) inden for ~0,5 s, flow (ord 52) → 0, toast *VAND STOPPET*.
- [ ] **S2 · kværn** (valgfri; bønner + kop): `--activity grind`. Forventet: `aa 55 06 06 01 00 00 c4 47` → bit 6 ryddet, kværn-strøm (ord 58) falder.
- [ ] **S3 · bryg** (valgfri; portafilter med kaffe): `--activity brew`. Forventet: `aa 55 06 16 01 00 00 c0 87` → bits 1+2+9+12 ryddet, ventil-puls `0105` i ~0,2 s og pumpen stopper.

### T · Tænd/sluk-knappen (v6.14) — kør, når maskinen er tændt og ledig

Kør `python3 verify_power_v614.py --plan` (ingen kontakt) og derefter `python3 verify_power_v614.py`. Kræver `ourui_v6_14.bin` (bygget af `build_v614.sh`).

Hvad scriptet gør:
* lægger `ourui_v6_14.bin` og en kopi af run.sh i `/tmp/ourui_p/` og starter ourui med `OURUI_NONET=1`; stock-UI'et SIGSTOP'es. **Boot-/re-push er TIL** (MCU'en vågner med et reset og skal have config'en tilbage)
* noterer de åbne input-noder (forventet `/dev/input/event0` + `/dev/input/event1`) og sætter strace (passiv) på `/dev/ttyS1`
* beder en person ved maskinen om at **holde tænd/sluk-knappen ~2 s** (SLUKKER… → SLUKKET), læser `pwm2/enable`, og efter ≥ 10 s om at **holde den igen** (TÆNDER… → HJEM)
* stopper ourui (run.sh SIGCONT'er stock; ourui tænder lyset ved exit), henter loggen og sletter `/tmp/ourui_p/`

Ingen reboot, ingen config-/flag-/slot-writes. Sikkerhedsnet: 300 s. Resultat: `power_v614_ondevice/<tid>/PROOF.md`.

- [ ] **T1 · sluk:** cmd8 (alarm 0/0/0) → ≥ 80 ms → `aa 55 06 00 02 00 00 00 00 56 94` → ACK; ord 0 = 0; controlleren tavs ≥ 3 s; `pwm2/enable = 0`; ingen keep-awake i mellemtiden.
- [ ] **T2 · tænd:** controlleren vågner (0x89-runden); `aa 55 06 00 01 01 00 c5 5f` + `aa 55 06 01 01 00 00 c5 33` → ACK; ord 0 = 1; re-push → ord 11/14/25/114 = config.
- [ ] **T3 · efter:** stock-PID uændret og kørende, lys som før, config-md5 + flag uændrede, display-uptime voksende.
- [ ] **T4 (valgfri, manuel):** hold knappen under et bryg → toast *STOP AKTIVITETEN FØRST · SÅ SLUK*, intet sendes; efter brygget slukker et nyt hold.

---

## 12. Filer

| Fil | Indhold |
|---|---|
| `ourui_v6.c` / `ourui_v6` | **v6.13** (manuel STOP) — kilde- og binær-md5 står i [CHANGES-v6.13](#changes-v613) pkt. 7 |
| `ourui_v6_12.c` / `ourui_v6_12.bin` | **ny (v6.13):** v6.12 bevaret uændret til sammenligning/tilbagerulning — kilde `e9eea8caebfa5d832884d96996dbd2c7`, binær `725386ccadade594275f20cc998885eb` (= den udrullede v6.11/v6.12-binær). SIM: `ourui_v6_12_sim` / `ourui_v6_12_sim_arm` |
| `verify_stop_v613.py` | **ny (v6.13):** on-device-bevis for STOP (vand; `--activity grind|brew`): run.sh-tilstand i `/tmp`, passiv strace, fysisk tryk; `--plan`, `--selftest`, `--decode` |
| `round_v6/v612_compare.py` | **ny (v6.13):** previews mod v6.12 — kun HJEM's aktions-række, underlinje og toast må afvige |
| `ourui_v6.c` (historik) | hele **v6.9** (7095 linjer), md5 `440be5124c0d986ec23e4c3b08f84149` |
| `ourui_v6` | statisk ARM-binær **v6.9** (enheden), md5 **`2dce4eba0b4ce68dcd0369e955cf3b77`** (reproducerbar m. zig 0.17.1 i samme mappe og med standard-cachen) |
| `ourui_v6_9wip.c` / `ourui_v6_9wip.bin` | **ny (v6.9):** standen ved v6.9-rundens start (v6.8 + forgængerens delvise konditionering) — kilde `048a40ea7894dab5b57a58f6d9af8ead`, binær `bcb1b50c913700c64a6cfc39bb5a5776`. Skal ikke installeres |
| `ourui_v6_8wip.c` / `ourui_v6_8wip.bin` | v6.8 (net-vagt, uden REVERT-knap) — kilde `5cdcc14b0503c67b939e27b6579655ff`, binær `75887fb154ffef20cc72696064e16c39`; reference for build- og preview-sammenligningen |
| `ourui_v6_6.c` / `ourui_v6_6.bin` | v6.6 — kilde `3369e228fa36c42988b7f0421ff6eef4`, binær `c22153af54069d9d1fcbe43fcb118c99` |
| `round_v6/fbcrop.py` | **ny (v6.9):** udsnit af fb-dumps/PNG'er i et gitter (chart-før/efter-ark) |
| `sim_scenarios/ui_calm_graph.json` | **ny (v6.9):** graf + vægt gennem ro m. pulser, kværn, bryg, ro efter bryg, damp og tara — køres mod v6.8 og v6.9 (`sim_e2e.py --only calm`) |
| `ourui_v6_sim` / `ourui_v6_sim_arm` | SIM-build til værten (den `sim/run_e2e.py` starter; md5 skifter ved hver linkning) / statisk ARMv7 (kontraktens build-linje; v6.9: `cbebc8b128f413c1d40a83de2bd6343a`; tidligere md5 `61d69f8489634a16739da14ada213bf3`). **Skal ikke på enheden** |
| `ourui_v6_5.c` / `ourui_v6_5.bin` | **ny:** v6.5 bevaret uændret til sammenligning/tilbagerulning — kilde md5 `f6e502b3d4cb7d212d1a1786520b2de4`, binær md5 `e84785d514c11ef88705ac97240b98c1`. **Ikke til permanent slot B-boot** (32-bit-uret, CHANGES-v6.6 pkt. 3) |
| `ourui_v6_4.c` / `ourui_v6_4.bin` | **ny:** v6.4 bevaret uændret til sammenligning/tilbagerulning — kilde md5 `5bdaf341f77dcec3554707cf6cd9df70`, binær md5 `1fe0b5b8c3b24abf0cadcde7b099076a` |
| `ourui_v6_3.c` / `ourui_v6_3.bin` | **ny:** v6.3 bevaret uændret til sammenligning/tilbagerulning — kilde md5 `4ac62cd08de36f7fea026eec5a3494ab`, binær md5 `5815176d0e766c7a6532b88014e932fb` |
| `build_sim.sh` | bygger enheds- og SIM-binærer; advarsler m. frisk cache; **v6.6:** sammenligning af enheds-build'et m. v6.5 pr. funktion/data-objekt mod en liste over tilsigtede ændringer (tidstype-forskelle for sig); fil-adgangs-gennemgang; exit-koder; røgtest af ARM-SIM-binæren og **hele selftesten som 32-bit ARM-binær** i lokal ARMv7-container → `round_v6/sim_build_check.txt`, `round_v6/selftest_arm32.txt` |
| `sim_e2e.py` | kører `sim/selftest.py`, alle scenarier (EN + DA), ourui's egne touch-scenarier, runtime-skiftet uden dry-run boot-pushen mod den simulerede MCU, RX-stall-reproen og de målrettede scenarier og — **v6.6** — fase-semantikken linje for linje mod LIVE-optagelsen (`phases`), ur-omløbet i realtid (`wrap`) og MCU-genstart → re-push (`repush`); noterer for hver del, om test-agentens filer ændrede sig undervejs; samler alt i `sim/reports/v6.6/` |
| `sim_ui_profile.json` | v6.4's 22 overrides. **Ude af brug fra v6.5:** test-agentens scenarier asserter nu ourui's adfærd direkte, og runneren har sin egen (tomme) standard-profil |
| `sim_scenarios/` | 4 scenarier med ægte FIFO-touch på ourui's egne koordinater (v6.4: navigation + stepper + GEM + hold-for-start; de afvigende rækkers ægte frames; auto-væk; spærret/tilladt skift) + **ny (v6.6):** `ui_replay_steam_live_x4.json` — LIVE-optagelsen (steam m. auto-stop, grind, bryg, wand-flush under bryg) afspillet 4× |
| `ourui_v6_2.c` / `ourui_v6_2.bin` | **ny:** v6.2 bevaret uændret til sammenligning/tilbagerulning — kilde md5 `c2dc02caf271097e6b4e65e598db0af1`, binær md5 `0bb93da0ed0824c424f1e16530f07022` |
| `ourui_v6_1.c` / `ourui_v6_1.bin` | v6.1 bevaret uændret — kilde md5 `7ff7d9d497e3c74cc2a4ac477ac01728`, binær md5 `4d927802cbeb69cc949f463021b29b99` (v6.0 var `90bcf094…`) |
| `boot/` | kollegaens boot-leverance (switcher-daemon, `boot_install.py`, `BOOT.md`). **Ikke rørt af v6.2, v6.3, v6.4, v6.5 eller v6.6.** |
| `parity_check.py` | **ny:** SET[] ↔ `parity_spec.json` (grænser, trin, tiers, ord); exit 1 ved ubegrundet afvigelse |
| `cjson_canon.py` | beviser at ourui's config-fil er byte-identisk m. `cJSON_Print` |
| `i18n_appendix.py` | **ny (v6.3):** genererer Appendiks A fra binærens streng-tabel (`--write`) og fejler, hvis appendikset ikke er i sync (`--check`, køres af `build_previews.sh`) |
| `round_v6/` | **v6.6:** 62 scenarier pr. sprog; nye filer `v65_compare.txt`, `mutation_check.txt`, `selftest_arm32.txt` · **engelsk:** `.bin` + `.png`, `contact_sheet.png` · **dansk:** `da/` m. de samme + `contact_sheet_da.png` · `contact_sheet_en_da.png` (EN \| DA side om side) · `selftest.txt` + **`selftest_da.txt`**, `parity_check.txt`, `config_format_check.txt`, `check_circle.txt` + **`check_circle_da.txt`**, `bench.txt`, `boot_check.txt`, **`i18n_check.txt`**, **`strings.tsv`** (streng-tabellen: id, klasse, EN, DA), `build_previews.sh`, `fb2png.py`; `_preview_root/` er mock-roden, `_preview_lang` sprog-filen i preview-kørslen |
| `REVERT.md` | opdateret: boot-push-semantik, genstart før stock-settings, frame-politik, flag, runtime-skift, `boot_ui.log` + **v6.3:** sprog-filen `lang` + **v6.4:** intet nyt på enheden; tilbage til v6.3 + **v6.5:** boot-push ved opstart, `OURUI_NOBOOTPUSH`, tilbage til v6.4 + **v6.6:** re-push efter MCU-genstart, tilbage til v6.5 |
| run.sh | **uændret** (md5 `3d799859549d2b8ba32245453022c20d`) |

---

## 13. SIM-build og E2E (v6.4)

Fra v6.4 kan UI'et køre **uden hardware** mod test-agentens simulerede MCU ([sim/](../../sim/), kontrakt: [sim/CONTRACT.md](../../sim/CONTRACT.md)). Det er samme kildefil; `-DOURUI_SIM` skifter kun de tre fil-noder ud og lægger en test-flade ovenpå. Enheds-build'et indeholder intet af det (CHANGES-v6.4 pkt. 2).

### 13.1 Build og kørsel

```sh
cd artifacts/ourui
sh build_sim.sh                      # enheds-binær + SIM (vært + statisk ARM) + alle build-tjek -> round_v6/sim_build_check.txt
python3 sim_e2e.py                   # hele E2E-verifikationen -> sim/reports/v6.5/SUMMARY.md (ca. 30 min)
python3 sim_e2e.py --only bootpush   # v6.5: kun boot-pushen mod den simulerede MCU (også: rxstall, targeted)
python3 ../../sim/run_e2e.py --all   # kun scenarierne (finder selv ourui_v6_sim); --ui-lang da for dansk
```

| Binær | Bygges med | Bruges til |
|---|---|---|
| `ourui_v6` | `zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2` | enheden (uændret kontrakt) |
| `ourui_v6_sim` | `zig cc -DOURUI_SIM -O2 … -lm` (værten; macOS har ingen statisk libc) | **den `run_e2e.py` starter** (auto-detekteres) |
| `ourui_v6_sim_arm` | som enheden + `-DOURUI_SIM` (statisk ARMv7) | kontraktens build-linje; røgtestet i en lokal ARMv7-container |
| `/tmp/ourui_v6_preview` | `-DOURUI_PREVIEW` (også sammen med `-DOURUI_SIM`) | previews + selftest, som før |

### 13.2 HAL: hvad SIM-build'et skifter ud

| På enheden | I SIM | Miljø |
|---|---|---|
| `/dev/ttyS1` | pty-slave fra `mcu_sim.py`; samme `uart_open()`/`uart_flush()` (raw 8N1, non-blocking) | `OURUI_SIM_TTY` (påkrævet) |
| `/dev/fb0` + page-flip | én 480×480 BGRA-buffer i hukommelsen. `fb_present()` kopierer **kun de rects compositoren melder som ændret** — som enheden — og bufferen skrives til fil (tmp + rename; højst 10 Hz, altid ved ændring af side/fase/toast) | `OURUI_SIM_FB` (påkrævet) |
| `/dev/input/event0` + `EVIOCGRAB` | FIFO med rå 16-byte evdev-records (samme som `tinj_hold`). Ingen grab, ingen `EVIOCGABS`. Records afvikles efter deres **tidsstempler**, og afkodningen er enhedens egen `inp_poll()` | `OURUI_SIM_TOUCH` |
| `/customer`, `/new_customer`, `/proc`, `/tmp` | under sandkasse-roden (`rp()`); config, backup-mappe og sprog-fil følger roden, medmindre deres egen variabel er sat | `OURUI_SIM_ROOT` (påkrævet; `/` afvises), `OURUI_CONFIG`, `OURUI_BAKDIR`, `OURUI_LANGFILE` |
| backlight-sysfs | skrives aldrig; preview-værdien står i state-JSON'en | — |
| `sync()` efter flag-ændring | udeladt (ingen global sync på værten) | — |
| — | state-JSONL: én linje pr. renderet frame, hvis indholdet er ændret; ellers mindst hver 90 ms | `OURUI_SIM_STATE` (påkrævet) |
| — | ctrl-socket med test-kroge (§13.4) | `OURUI_SIM_CTRL` |

Exit-koder: 0 normal · 2 miljø-fejl · 3 pty kan ikke åbnes · 4 fb/touch kan ikke åbnes · 130 SIGINT. `OURUI_SIM_NOBOOTPUSH=1` slår boot-pushen fra; `OURUI_SIM_BOOTPUSH_GUARD=1` (v6.5) giver SIM-build'et enhedens aktivitets-vagt (spring over i stedet for at udskyde); `OURUI_SIM_NOPROVISION=1` lader sandkassen være, som runneren lavede den (så er BOOT-UI og SKIFT NU låst).

**Fil-adgang:** `build_sim.sh` gennemgår alle fil-kald i SIM-build'et efter preprocessoren: den eneste faste sti uden sandkasse-rod er `/dev/null`, og der er ingen reference til `/dev/fb0`, `/dev/input`, `/dev/ttyS1`, `/sys/class`, `EVIOCGRAB` eller `FBIO*`.

### 13.3 State-JSON

Én linje pr. renderet frame, når noget i linjen er ændret siden den forrige (RX-tælleren `crc_ok` og frame-tids-felterne tæller ikke som ændring), ellers mindst hver 90 ms. En register-værdi, UI'et har haft på en frame, står dermed altid i strømmen.

Kontraktens felter (`t_ms`, `view`, `page`, `phase`, `work_sta`, `regs`, `lang`, `boot_flag`, `crc_ok`, `crc_bad`, `cmd6`, `frame_ms`, `toast`, `config_oor`, `config_missing`, `link_up`, `save`, `activity_pending`, `regs_s`, `boot_pushed`, `client`, `version`) plus:

| Felt | Indhold |
|---|---|
| `phase` / `phase_ui` | `phase_raw()` (aldrig `DONE`) / det chippen viser (`DONE` i 8 s efter en aktivitet) |
| `page` / `view_raw` / `tab` | `home` · `settings` · `detail` · `sys` · `diag` · `bye` / `splash` · `main` · `detail` · `sys` · `bye` / `home` · `brew` · `steam` · `system` · `diag` |
| `shown` | det skærmen viser: HJEM = chip, hero, enhed, label, under-linje og de tre stat-par; editor = label, værdi, gemt værdi, interval, låst/gemmer |
| `toast` | `text` = tabellens **danske** tekst (assert-sprog), `shown` = teksten på skærmen, `id` = streng-id, `kind` = ok/err/warn/info |
| `lang` / `ui_lang` / `lang_src` | `config.language` (stock-UI'ets sprog) / Meraki+'s eget sprog (`en`/`da`) / hvor det kommer fra |
| `boot_flag`, `next_boot`, `boot_lock`, `exe_slot`, `self_exe` | flaget (læst friskt), hvad næste genstart giver, BOOT-UI-låsen, hvilken slot ourui kører fra |
| `switch` | `mode` (launch/cont), `locked`, `dry`, `bye`, `stock_n`, `switcher_pid`, `child`, `last_evt` |
| `restart_needed` | `file`, `pending` (venter et stock-UI med gammelt spejl?), `stamp` (`boot_id=… mono_s=…`) |
| `cmd6` + `blok`, `wq_n` | ok / retx / fail / block; `blok` = frames afvist af frame-politikken (DIAGNOSE "BLOK") |
| `boot_push` | `sub1_len30` og `sub112_len3`: ok / noack / blocked / nocfg / skipped (v6.5: aktivitets-vagten); `runs` |
| `rx_resync` | **v6.5:** forkastede header-kandidater (umulig længde, watchdog, overløb) — DIAGNOSE "RESYNC". `crc_bad` er som før CRC-fejl på hele frames |
| `phase_main` / `phases` | **v6.6:** HJEM's hoved-aktivitet (kværn / bryg / vand går forud for damp) / alle aktive, hoved-aktiviteten først — fx `["EXT","STEAM"]` ved work_sta `5327`. `phase` er uændret (`phase_raw()`: damp først) |
| `steam` | **v6.6:** `on`, `kind` (none / unknown / auto / wand), `last_kind`, `concurrent`, `wand_sw` (ord 44 bit0 eller ord 46), `wand_seen`, `ord24`, `target` (ord 25), `milk` (ord 48), `stop_btn` (vises STOP-knappen?) |
| `shown.badge` | **v6.6:** damp-mærket under samtidige faser (ellers `null`); `shown.chip` er *STEAM·AUTO* / *STEAM·WAND* under damp |
| `repush` | **v6.6:** `count`, `ver` (udløst af `0x89`), `defaults` (udløst af startværdi-signaturen), `last`, `armed`, `ver_reqs` (antal `0x89` modtaget), `defaults_sig` |
| `save` | `state` = pending / ok / fail / rejected / unchanged, `param`, `value` — også for et GEM startet med touch |
| `frame_ms`, `frame_ms_max`, `frame_ms_avg`, `frame_ms_peak`, `frames` | sidste frame, max siden forrige linje, snit og max over hele kørslen, antal renderede frames |

### 13.4 Test-kroge (ctrl-socket)

Krogene kalder de **samme funktioner som et tryk** og omgår hverken vagter eller frame-politik:

| Krog | Hvad den gør i UI'et |
|---|---|
| `action {name}` | `act_request()` — knappen uden hold-tiden. Alle vagter gælder. `steam_stop` = STOP DAMP |
| `save {param,value}` | åbner rækkens editor, sætter værdien, `save_begin()`. Ikke eksponeret/låst: *LÅST*. Uden for interval eller trin: *UDEN FOR GRÆNSER* |
| `touch {x,y,ms}` | samme record-sekvens som `tinj_hold`, lagt i touch-køen |
| `page {name,…}` | navigation; `detail` tager `param`, `settings` tager `tab`, `sys` tager `x` |
| `boot_flag {value}` | BOOT-UI → vælg → GEM (`sys_confirm` → `boot_flag_set` med vagt) |
| `runtime_switch` | SKIFT TIL ORIGINAL NU → SKIFT NU (`sys_confirm` → `do_switch`). Dry-run viser farvel-skærmen i 1 s |
| `raw_write {sub,words}` | emulerer et **stock-UI-write**. Eneste lempelse af frame-politikken: ord1 ∈ {0,1}. ord1=1 sætter ourui's keep-awake på pause, til et `raw_write` med ord1=0 |
| `config_reload` | sprog-fil + config læses igen, og boot-pushen gentages |
| `layout` | de rigtige tryk-mål i den aktuelle visning (til fx `sim/play.py`). **v6.8:** `revert` er væk (knappen er fjernet) |
| `net {name}` | **v6.7:** wifi-MODELLEN (SIM-build'et har ingen ægte stak): `kill` (wpa_supplicant + udhcpc dør, IP/rute bliver), `ip_drop`, `if_down` (IP bliver, ruter væk), `cold` (ingen wlan0), `ap_down` / `ap_up`, `status`. Net-vagten reagerer som på enheden; `OURUI_SIM_NET=cold\|off` ved start. State-JSON: `net{state, iface, ifup, wpa_pid, dhcp_pid, carrier, ip, gw, heals, boots, heal_ms, init_runs, wpa_starts, dhcp_starts, conf_writes, fails}` |
| `ping`, `state_now`, `fb_dump {path}`, `exit {code}` | som kontrakten |

Er write-køen optaget i det øjeblik en `action`/`save`-krog kommer (fx af en keep-awake), venter krogen op til 600 ms — som et tryk et øjeblik senere.

### 13.5 Det SIM-build'et gør, som enheden ikke gør

| | SIM-build | Enheds-build |
|---|---|---|
| **Boot-push** `sub1 len30` + `sub112 len3` | ved opstart **og** ved `config_reload`; en aktivitet **udskyder** den (kontrakt-adfærd, uændret fra v6.4). `OURUI_SIM_BOOTPUSH_GUARD=1` giver enhedens vagt | **v6.5:** ved opstart; kører en aktivitet allerede, **springes den over** (aktivitets-vagt, CHANGES-v6.5 pkt. 2). (v6.4: ingen boot-push) |
| Dry-run af SKIFT NU | farvel-skærm i 1 s + toast | kun log + toast |
| `sync()`, backlight, `EVIOCGRAB` | nej | ja |

Alt andet — main-loop, poll-rotation, RX-parser m. resync-watchdog, keep-awake, write-kø (blok-grænse 30 ord i begge fra v6.5), ACK/retransmit, frame-politik, boot-pushens ord-layout og tilstandsmaskine, gem-sti, aktivitets-vagter, boot-/skift-logik, tegne-kode — er samme kode.

### 13.6 Hvad E2E-miljøet ikke siger noget om

* **Enhedens frame-tid.** SIM-tallene er målt på værten og kan ikke ganges op til enheden (CHANGES-v6.4 pkt. 7). Enhedens skal måles på enheden (DIAGNOSE viser den).
* **Pixel-eksakthed.** En ARMv7-oversættelse af samme kode giver ikke bit-identiske billeder med værtens (CHANGES-v6.4 pkt. 6).
* fb-mmap/page-flip, touch-elektrik, OTA og den ægte MCU's timing — se [sim/README.md](../../sim/README.md) §6.

---

## 14. Visnings-konditionering og stabil vægt (v6.9)

**Problemet:** grafen og tallene på HJEM sprang. Der var tre kilder til det:
* **Ord 51 (tryk/pumpe-drive)** har ADC-støj på ±9..±40. Uden for en aktivitet har det **vedligehold-pulser**: små varmecyklusser på 400–900 i 0,5–1,5 s og deadhead-pulser på 3000–16670 i 0,1–0,25 s (BREW.md §5–7, STEAMLIVE.md §5.3). v6.8 tændte et "aktivt" chart på ét enkelt sample ≥ 40 og skalerede efter max. Hver puls i ro blev derfor en spike, der fladtrykte resten af kurven.
* **Vægten** (brugerens klage: *periodiske vægt-spikes, også når maskinen ikke røres*). Grafen valgte vægt-ord pr. sample efter fasen (`phase_main()==KVÆRN ? ord 38 : ord 39`) og hero'en efter den viste fase. Ét forkert work_sta-sample, et link-hul eller ét bit-6-sample lagde derfor en anden vægt ind i kurven, fx kværn-vægten 18,6 g midt i koppens 36,7 g. Dertil kommer kop-støj på ±0,1–0,2 g i ro.
* **Ord 38/39 er signed** (tara `0xFFFD` = −0,3 g). Et filter på de unsigned ord smører taraen ud mod 6553 g; sim'ens demo-'calm' havde præcis den fejl. UI'et filtrerer på int16.

**Princippet:** kun det, der **tegnes**, ændres. `reg[]`, ringbufferen (`g_p/g_w/g_m`), state-JSON's `regs`/`regs_s`, alle writes, frame-politikken, auto-stop-forventninger, vagter, tærskler, fase-logikken og DIAGNOSE-tællerne læser fortsat de rå værdier. Build-tjekket beviser det: 29 styrings-/protokol-stykker er token-identiske med v6.8 (`sim_build_check.txt`).

### 14.1 Hvad der tegnes

| Element | Konditionering | Konstant (`-D…` ved build · ctrl `cond` i SIM) |
|---|---|---|
| Chart-serierne (tryk, vægt, mælk) | pr. sample (10 Hz) median af de 3 seneste → EMA | `CD_A_CHART` 0,18 (tidskonstant 0,5 s) · `CD_MED` 3 |
| Chart-tryk: puls-gate | springer median-3 til > 2 × vist + 400, holdes den viste værdi i op til 3 samples; varer springet længere, følges det | `CD_GATE` 3 (0 = fra) |
| Chart ved aktivitets-skift | de næste 2 samples følger median-3 direkte (ingen EMA-hale; et puls-sample lige ved skiftet slipper ikke igennem) | `CD_FAST` 2 |
| Chart-vægt ved kilde-skift og kop af/på | kilde-skift 38 ↔ 39: vægt-filtret starter forfra på det nye ord (ingen historik fra det andet ord — ellers et blink af den gamle vægt). Median-spring > 2,0 g på ét sample = kop af/på → snap (støj ±0,2 g; et bryg stiger ca. 0,2 g/sample) | `CD_W_JUMP` 20 |
| Chart-skala | toppen = max(mål/gulv × 1,08, **p90 af vinduets AKTIVE rå samples** × 1,15). Rå samples over toppen tegnes som små streger lige under loftet | `CD_PCT` 90 · `CD_HEAD` 1,15 · `scale` = `pct` / `hold` / `max` (v6.8) |
| Tryk i ro | tegnes kun for samples under bryg/præ-infusion/vand (`g_a`). Uden for aktiviteten er ord 51 vedligehold, og pulserne vises som små streger på grundlinjen. Chartet er "aktivt" i ro, så længe 40-s-vinduet har et bryg/vand | — |
| Hero (vægt / mælk / kedel) + gauge + BAR-felt | median-3 + EMA i 10-Hz-takt; **snap til rå** ved fase-skift (inkl. FÆRDIG), kilde-skift, set-punkt-/mål-skift, et median-spring > 15,0 og efter > 0,5 s uden opdatering | `CD_A_HERO` 0,25 |
| Vægt (hero, chart, stat-felt) | **dødbånd**: \|vægt\| < 0,25 g (\|ord\| < 2,5) vises som 0 | `CD_WDB` 2,5 |

**Hvorfor p90 kun over de aktive samples:** en p90 over hele 40-s-vinduet er 0 i de første ca. 4 s af en ekstraktion, fordi vinduet mest er tomgang. Kurven ville blive klemt op i loftet. p90 tåler, at op til 10 % af de aktive samples er pulser. Er der flere, er de en del af signalet og kommer med i skalaen.

### 14.2 Stabil vægt-kilde (latch)

* Standard er **bryg-vægten (ord 39)**.
* **Kværn-vægten (ord 38)** bruges kun, mens en kværning *er* i gang (work_sta bit 6), og mens dens FÆRDIG-skærm står i 8 s. Det sidste gælder kun, hvis kilden allerede var latchet til 38 af en ægte kværning.
* Et skift kræver, at den ønskede kilde har stået i **≥ 300 ms** (`WSRC_DEB_MS`). Vedligehold-bits (0x0400/0x0800/0x2000/0x0C00/0x2800), et link-hul eller ét afvigende work_sta-sample skifter den aldrig.
* **Undtagelse:** har UI'et selv startet aktiviteten (`act_request`, < 4 s siden), og er aktivitetens bits sat, skiftes der straks. Aktiviteten er ægte, og hero'en viser så taraen 0,0 g i stedet for koppens vægt. Starter en kværning *uden* UI'et (fysisk), viser hero'en de første 300–400 ms den latchede kilde.
* Grafen og hero'en bruger samme kilde. State-JSON: `weight:{src, switches, shown}`.

### 14.3 DIAGNOSE → GRAF (glat / rå)

DIAGNOSE har rækken **GRAF · TRYK FOR AT SKIFTE**. Standard er **GLAT · MEDIAN 3 + EMA**; et tryk giver **RÅ · UFILTRERET**. RÅ er v6.8's serie og max-skala, uden dødbånd og gate, og chartet er mærket **RÅ** med rav i højre hjørne. Valget gælder kun denne session: ingen config, ingen frames, og det glemmes ved genstart.

### 14.4 Kendt afvejning

Hero'en halter efter en jævn rampe: median-3 giver 1 sample, EMA 0,25 giver ca. 3 samples. Ved en kværning på ca. 2 g/s viser hero'en ca. 0,8 g under den rå værdi (preview 03: 11,1 g mod rå 12,0 g). Ved FÆRDIG snapper den til den rå slutvægt. Auto-stoppet er MCU'ens og påvirkes ikke. Skal hero'en følge hurtigere, hæves `CD_A_HERO` (fx 0,4 → ca. 0,35 g lag).

### 14.5 Tuning i SIM

```sh
# ctrl-socket (SIM-build): alle felter valgfri; uden felter = læs
{"cmd":"cond"}                                    # -> {"ok":true,"raw":false,"a_chart":0.180,"a_hero":0.250,"med":3,"scale":"pct","hold":6,"pct":90,"head":1.15,"gate":3}
{"cmd":"cond","a_chart":0.3,"gate":0}             # live — filtrene starter forfra
{"cmd":"cond","raw":1}                            # = DIAGNOSE → GRAF = RÅ
```
State-JSON (tillæg, kun visning): `cond{…, wdb, gate, hero_snaps}`, `chart{mode, active, full, top, raw_max, disp_max, ticks, base_ticks, clipped, n, n_active}`, `weight{src, switches, shown}`.

### 14.6 Før/efter-tal (graf + vægt)

`round_v6/calm_stats.py` læser state-linjerne fra `ui_calm_graph` (v6.8- og v6.9-SIM-build'et, samme sim og seed) og regner pr. segment på det, der stod på skærmen. Resultatet står i `sim/reports/v6.9/calm_stats.txt`, og snapshots ligger side om side i `calm_chart_v68_v69.png` (chart-udsnit) og `calm_full_v68_v69.png` (hele skærmen). Tallene står i [CHANGES-v6.9](#changes-v69) pkt. 6.

## 15. Udviklings-iteration (DEV-ITERATION-POLICY)

**Brugerens direktiv (2026-11-17):** den FULDE E2E-suite tager over 35 min og køres **ikke** pr. iteration.

| Hvornår | Hvad køres | Tid |
|---|---|---|
| **Hver iteration** | `sh round_v6/build_previews.sh` (selftest EN + DA, previews begge sprog + circle-tjek, spec-afstemning, mutations-prøver) · `sh build_sim.sh` (0 warnings, statisk ARM, SIM-builds) · `python3 sim/selftest.py` · `python3 sim/run_e2e.py --all --quick --parallel 4 --ui-bin artifacts/ourui/ourui_v6_sim` (5 kerne-scenarier, kun engelsk) | ca. 3–4 min i alt; quick-kørslen ca. 45 s |
| **Målrettet, når ændringen kræver det** | én enkelt del af `sim_e2e.py`, fx `--only calm` (graf/vægt før/efter, ca. 6 min) eller `--only net`, eller ét scenarie med `run_e2e.py --scenario <navn>` | minutter |
| **Kun i CI/CD eller ved et release-milestone** | `run_e2e.py --all` på EN **og** DA, REF-klienten, steam30, replay-scenarierne, UI-scenarierne på begge sprog — dvs. `python3 sim_e2e.py` uden `--only` | > 35 min |

Der står altid i rapporten, hvilket niveau der er kørt. Et grønt quick-run er ikke det samme som en grøn fuld suite. Resultater fra en tidligere fuld kørsel må gerne citeres, men de skal mærkes med den binær (md5), de blev kørt mod.

## 16. Tryk-estimat (v6.10)

**Brugerens ønske:** trykket stod som *UKALIBRERET*. Der findes et rimeligt anker, så UI'et viser nu et forsvarligt **estimat** i bar — tydeligt mærket som estimat, med rå-værdien altid tilgængelig.

### 16.1 Ankeret og skalaen

| | Værdi | Kilde |
|---|---|---|
| **P0** (`P_ANCHOR_RAW`) | **358 rå** (ord 51) | Pre-infusion-plateauet i UART-optagelsen `BREW.md §6`: bryg 20:54:05–06,7, varighed 6,06 s (= config `preInfusionTime` 6), median 356, spænd 332..366. `STEAMLIVE.md` bekræfter ~360 / 6,84 s i to uafhængige bryg |
| **Anker-tryk** | config **`preInfusionPressure`** (default 4 bar) | `/customer/config.json` (`preInfusionPressure: 4` under optagelsen). Læses via settings-tabellen (`SET[S_PIPRESS]`): ændres config, ændres skalaen. Uden for 1..15 bar eller uden config: `P_ANCHOR_BAR_DEF` = 4 |
| **k** | anker-tryk / P0 = 4 / 358 = **0,011173 bar pr. rå enhed** | `p_k()` |
| **Vist bar** | ord 51 (konditioneret, §14) × k | `p_bar()` |
| **Puls-grænse** (`P_EST_MAX`) | **20 bar** (= 1790 rå ved anker 4) | Over grænsen vises intet tal |

| Plateau (optagelsen) | Rå | ≈bar (anker 4) |
|---|---|---|
| Pre-infusion | 358 | **4,0** |
| Ekstraktion (median) | 1346 | **15,0** |
| Ekstraktion (start-top / slut) | 1379 / 1325 | 15,4 / 14,8 |
| Overgangs-overshoot | 1481 | 16,5 |
| Varmt vand (v2) | ~1300 | 14,5 |
| Deadhead / vedligehold / damp | 15 716..16 670 | (176..186 lineært) → **PULS**, intet tal |

Konstanterne kan ændres ved build: `-DP_ANCHOR_RAW=… -DP_ANCHOR_BAR_DEF=… -DP_EST_MAX=…`.

### 16.2 Hvad skærmen viser

| Sted | Tekst (DA / EN) |
|---|---|
| HJEM, stat-feltet under bryg | label **≈BAR · EST.**, værdi fx **15.0**; over 20 bar **PULS** / PULSE |
| HJEM, chartets billedtekst (bryg, vand, ro efter bryg) | **TRYK ≈BAR · EST.** / PRESS. ≈BAR · EST. (erstatter *TRYK\* UKALIBRERET*) |
| HJEM, tap på chartet eller BAR-feltet | toast (fodnoten): **PRÆ-INF. 4 BAR = RÅ 358 · LINEÆR** / PRE-INF. 4 BAR = RAW 358 · LINEAR |
| DIAGNOSE, ny blok **TRYK · ESTIMAT** (mellem GRAF og OM) | *ANKER · PRÆ-INF. I OPTAGELSEN* → **4 BAR = RÅ 358 · k 0.01117** · *NU · ORD 51 · PUMPE-SIDE, LINEÆR* → **RÅ 1346 = ≈15.0 BAR**, eller i rav **RÅ 16670 · VEDLIGEHOLD-PULS** (i ro) / **RÅ … · PULS, IKKE TRYK** (under en aktivitet) · noter: *ESTIMAT · PUMPE-SIDE · IKKE VED PUCKEN* og *> 20 BAR = PULS · RÅ: GRAF-PILLEN* |
| DIAGNOSE → GRAF = **RÅ** | BAR-feltet viser ord 51 som **rå tal** under labelen **TRYK RÅ** (også pulser, uklemt); chartet er v6.9's rå serie. Gauge og ringbuffer er rå som før |
| SIM state-JSON (tillæg) | `press{raw, k, anchor_bar, p0, max_bar, bar, pulse}` — `bar` er `null` og `pulse` `true` over grænsen |

Kun visning: styring, writes, auto-stop, vagter og tærskler bruger aldrig bar (build-tjekket: 37 styrings-, protokol- og konditionerings-stykker er token-identiske med v6.9).

### 16.3 Forbehold — derfor *EST.* og ikke en kalibrering

* **(a) Pumpe-side, ikke puck.** Det lineære anker giver ekstraktion ≈ 15 bar. Det er plausibelt for en vibrationspumpe **uden OPV** (budget-maskine; 12–15 bar er almindeligt), men det er tryk på pumpe-siden — ikke trykket ved pucken, som typisk er lavere.
* **(b) Pulser er ikke fysiske under en lineær skala.** Deadhead-/vedligehold-pulserne i ro (rå op til ~16 670) og damp-niveauet (~16 650, STEAMLIVE §5.3) ville være 175–186 bar. UI'et viser dem derfor aldrig som tal: over 20 bar står der *PULS* (HJEM) og *VEDLIGEHOLD-PULS* / *PULS, IKKE TRYK* (DIAGNOSE); chartet i ro viser dem som streger på grundlinjen (v6.9).
* **(c) To-punkts-affin passer ikke.** Ro ≈ 0, pre = 4 bar og ext = 9 bar (den gængse espresso-norm) kan ikke ligge på én ret linje gennem 0 (1346/358 = 3,76, ikke 2,25), og pulserne passer ikke på nogen af dem (BREW.md §7, anker A/B). Ord 51 kan være ikke-lineær, eller et pumpe-drive-niveau snarere end et tryk (REGISTERMAP: SPI2-ADC kanal 0xA, tryk- *eller* drive-kandidat). Derfor er tallet et **estimat** og ikke en certificeret kalibrering.
* **(d) Config-ankeret er en antagelse.** At skalaen følger `preInfusionPressure` forudsætter, at plateauet *er* reguleret til det tryk. P0 = 358 er målt ved 4 bar. Sættes config til 3, bruger UI'et k = 3/358, men det er ikke målt, at plateauet så stadig ligger på 358 (eller flytter sig). Kun 4 bar er optaget.
* **Fysisk verifikation** (manometer på en blind-kurv, eller sensor-ID på transduceren) er stadig den eneste vej til en egentlig kalibrering (BREW.md §7, anbefaling). Rå-værdien er altid tilgængelig: DIAGNOSE → GRAF = RÅ og nu-linjen i TRYK · ESTIMAT.

---

## 17. UX-review (v6.11)

Et kritisk pas over **alle** sider og tilstande (73 previews × 2 sprog + koden). Kriterierne var: informations-hierarki, om skærmen kan aflæses på armslængde, touch-mål, feedback og affordances, om navigationen kan findes, label-tekst og versaler, farvesemantik, fejl-, tom-, offline- og sove-tilstande, brugen af den runde skærm, og kontrast. Fundene fra orkestratoren (O1–O10) og mine egne (E1–E9) er slået sammen og prioriteret efter **effekt for brugeren ÷ risiko** (P1 = gør det nu, P2 = gør det, hvis det er billigt, P3 = udskudt).

### 17.1 Fund, prioritet og udfald

| # | Fund (før) | Prio | Udfald i v6.11 |
|---|---|---|---|
| E1 | **Stat-labels løb sammen:** *GROUP STEAM BOILER WEIGHT* (115 px label i en 94 px kolonne, 6 px luft) læstes som én linje. Det samme gjaldt *KVÆRN-TRIN* (100 px) og *TRYK RÅ* | P1 | **Gjort.** *DAMP* / *STEAM* i ro, *KEDEL* i damp-trioen (*TID · DAMP · NIVEAU* ville læses som "damp-niveau"), *TRIN* og *RÅ*. Selftesten kræver nu ≥ 16 px luft mellem to labels (`STAT_GAP`) |
| O3 + E2 | **HJEM i ro:** *INGEN AKTIVITET* gentog fase-chippen og fortalte ikke, hvad man kunne gøre. *BRYG-TEMP SAT TIL 87,0°* var en lang F_XS-linje | P1 | **Gjort.** Det tomme chart-felt viser nu **HOLD ET IKON FOR AT STARTE** og **STRYG FOR INDSTILLINGER**. Kan der ikke startes, viser det grunden (vagtens tekst, rav ved config-fejl). Underlinjen er **MÅL 87,0°** i F_S, og label'en er **BRYGKEDEL · NU**, så hero = nu og underlinje = mål |
| O1 | **Kunne ikke findes:** indstillinger kun via swipe eller usynlige kant-zoner. "Hold for at starte" stod kun i en toast efter et forkert tryk | P1 | **Gjort.** Et **tandhjul** under side-prikkerne (tryk-felt 64 × 46 px) åbner BRYG, og start-hintet står i chart-feltet (se O3). Kant-tryk virker nu også på listerne. Side-navnet står allerede som titel på alle sider undtagen HJEM, hvor *MERAKI+* er titlen. Prikkerne har derfor ikke fået labels |
| O4 | **Teknisk sprog for brugeren:** *VÆKKER CONTROLLER · ORD 1 = 0*, *INGEN SVAR FRA CONTROLLER*, *E2 · BRYGKEDEL-SENSOR*, noter som *ORD 11 · BRYGKEDEL-SETPOINT* / *ORD 112 HI-BYTE*, toasts med *ACK*, *FRAME-POLITIK*, *WRITE-KØ*, *STOP SENDT · ORD 24 = 0* | P1 | **Gjort.** Teksterne er nu *VÅGNER…*, *INGEN FORBINDELSE TIL MASKINEN* og *FEJL PÅ BRYGKEDEL-SENSOR*. E-koden står nedtonet i chart-feltet (*KODE E2 · SE DIAGNOSE*). Noterne beskriver virkningen (*MÅL FOR BRYGKEDLEN*, *VÆDER KAFFEN FØR FULDT TRYK*, *MASKINEN KENDER KUN 3 OG 4* …). Toasts: *GEM FEJLET · INTET SVAR*, *MASKINEN SVAREDE IKKE*, *SPÆRRET AF EN SIKKERHEDSREGEL*, *OPTAGET · PRØV IGEN*, *STOPPER DAMP…*. Ord-numre står kun på DIAGNOSE og i §4. En selftest kontrollerer, at ingen brugerrettet streng (HJEM, toast, note, fodlinje) indeholder ord-numre, ACK, frame-politik eller write-kø |
| O9 | **Kontrast:** mikro-labels (tekst 3 = 104,111,126) havde 3,5:1 på bunden og 2,9:1 på listerækker, ved 11 px versaler med tynde streger | P1 | **Gjort.** Tekst 3 er nu 140,147,162: 5,7 / 5,5 / 4,7:1 på bund / flade 1 / flade 2. Tekst 2 er nu 176,183,196: 8,7 / 8,5 / 7,2:1. Deaktiverede elementer har fået deres egen farve, 98,105,120 (3,2:1, store glyffer), så "slukket" stadig ser slukket ud. Fremskridts-ringen under bryg har beholdt sin gamle farve |
| O5 | **Swipe vs. rul:** en skrå gestus (fx dx 70, dy 45), der startede en anelse vandret, skiftede side midt i en liste | P1 | **Gjort.** Aksen låses ved de første 14 px. Et side-skift kræver vandret start, ≥ 60 px og ≤ ~30° (\|dx\| ≥ 1,7·\|dy\|). En gestus, der starter lodret, skifter aldrig side, heller ikke på HJEM |
| O6 | **Ingen tilbage-affordance** i detalje- og system-visningen ud over ANNULLER | P1 | **Gjort.** En **luk-chevron** øverst (tryk-felt 88 × 46 px) og **stryg ned** (≥ 70 px, lodret, ikke startet på −/+). Begge har samme semantik som ANNULLER, så intet gemmes |
| O2 | **Touch-mål** | P1 (måling) | **Var allerede i orden.** Det er nu målt gennem `hit_test()` i selftesten: ikoner 60 × 61, −/+ 85 × 85, rækker 352 × 58, ANNULLER/GEM 136 × 64, tandhjul 64 × 46, chevron 88 × 46. Eneste undtagelse var DIAGNOSE's GRAF-pille på 300 × 40, som nu er **300 × 44** |
| O8 | **HJEM under bryg:** chart-teksten *TRYK ≈BAR · EST.* gentog stat-feltets *≈BAR · EST.* | P2 | **Gjort.** Chartet hedder **TRYK**, og stat-feltet hedder **≈BAR EST.**. Grundlaget (anker 4 bar = rå 358) står fortsat i toasten ved tryk på chartet og på DIAGNOSE |
| O7 | **DIAGNOSE:** 16 celler i ét blok-gitter, rå tællere før link-status. Boot, net og tryk kom først efter 8 rækker | P2 | **Delvist.** DIAGNOSE er nu delt i sektioner: **STATUS** øverst (link, fase, E1/E2/E3, vågen, net, version, boot-push), derefter BOOT · SLOT · SWITCHER, NET, GRAF og TRYK · ESTIMAT, så **RÅ TÆLLERE · SERVICE** (rx, crc, work_sta, cmd6, strøm, frame, spejl, md5) og OM nederst. Cellernes indhold er uændret. *Udskudt:* at folde de rå tællere sammen bag en toggle (se 17.2) |
| O10 | **Tryk-feedback** | P2 | **Var allerede konsekvent:** ripple på alle mål, tryk-farve på rækker, −/+, pills, ikoner (+ hold-ring) og GRAF-pillen. De nye mål (tandhjul, chevron) har samme tryk-tilstand. Previews `70_home_gear_pressed` og `71_detail_back_pressed` |
| E3 | Fejl- og offline-tilstand havde ingen vej videre | P2 | **Gjort** via hintet: *KODE E2 · SE DIAGNOSE* og *DETALJER UNDER DIAGNOSE* |
| E4 | Underlinjen i ro (den vigtigste besked: mål, offline, fejl) stod i den mindste skrift | P2 | **Gjort.** F_S, når teksten kan stå i feltet. Under aktiviteter er den stadig F_XS |
| E5 | De fire aktivitets-ikoner har ingen tekst under sig | P3 | **Udskudt.** Der er 20 px mellem ikon-rækken og prikkerne. Fire labels i F_XS ville stå klemt mod tandhjulet. Hold-hintet i chart-feltet og *HOLD · BRYG 36,0 G* i underlinjen, mens man holder, dækker behovet |
| E6 | Detalje-fodlinjerne *CONTROLLER STRAKS + CONFIG / STOCK-UI: EFTER GENSTART* er teknisk sprog | P3 | **Udskudt.** De beskriver en reel to-UI-semantik (PARITY §1.4), og E2E-scenarierne citerer toasts med samme ordlyd (*GEMT · CONTROLLER + CONFIG*). Det bør omformuleres sammen med test-agenten |
| E7 | *HOLD ET IKON …* står også, mens man holder et ikon nede (underlinjen viser da *HOLD · BRYG …*) | P3 | **Accepteret.** Det er redundant, men ikke forkert. At skjule hintet ville give et 650 ms blink |
| E8 | Hero'en *--.-* offline læses som streger | P3 | **Uændret.** Den er nu i "deaktiveret"-farven, og underlinjen siger klart *INGEN FORBINDELSE TIL MASKINEN* |
| E9 | Side-prikkerne viser ikke navne | P3 | **Uændret** (se O1: titlen står på siden) |

### 17.2 Udskudt — og hvorfor

* **De rå tællere foldet sammen bag en toggle (O7, anden halvdel).** Det ændrer DIAGNOSE's scroll-geometri dynamisk og flytter GRAF-pillens tryk-felt, som selftests, previews (`DG_SC_*`) og E2E-profilen regner med. DIAGNOSE er en ekspert-side, så en egen sektion længere nede giver det meste af gevinsten uden den risiko.
* **Labels under ikonerne (E5)** og **omformulering af detalje-fodlinjerne (E6):** se tabellen.
* **Etiketter på side-prikkerne (O1):** titlen står allerede på siderne. Et ekstra lag tekst ved y 430 ville konkurrere med tandhjulet.

### 17.3 Bevidst uændret

Fase-farverne og gaugen er byte-identiske i alle 140 sammenlignede previews (ringen r ≥ 200 over y 380). Det samme gælder visnings-konditioneringen (v6.9), tryk-estimatet (v6.10), vægt-latch og dødbånd, EN/DA, circle-clip, frame-politikken, RÅ-toggle, boot- og skift-flows og den fjernede REVERT. 47 styrings- og protokol-stykker er **token-identiske** med v6.10 (build-tjekket). Det omfatter frame-politik, write-kø, aktivitets-recepter og -vagt, fase-logik, GEM-, luk- og skift-vejene, boot-push, net-vagt, RX, CRC, flag, konditionering og tryk-estimat.

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

## CHANGES-v6.1

Hver afstemnings-delta, nummereret efter opgavens punkter.

**1 · Grænser, labels, tiers**
| Række | v6.0 (antaget) | v6.1 (spec) |
|---|---|---|
| KVÆRN-DOSIS | 70..250 / 1 | **10..309 / 1** (1,0–30,9 g) |
| BRYG-VÆGT | 150..800 / 5 | **100..990 / 10** (10–99 g) |
| BRYG-TEMPERATUR | 800..980 / 10 | **850..960 / 10** (85–96 °C) |
| MÆLK-TEMPERATUR | 400..750 / 10 | **450..700 / 10** (45–70 °C) |
| VARMT VAND | 200..4000 / 50 | **300..5000 / 100** (30–500 g, trin 10 g) |
| DAMP-NIVEAU | 1..4, tal | 1..4, **navne** BLID/MILD/MEDIUM/STÆRK |
| PRE-INF. TRYK | 3..6 | **3..4** (MCU kender kun 3 og 4) |
| PRE-INF. TID | 5..15 (antaget) | 5..15, nu **mærket SKØN**, loft 60 |
| GRUPPE-TEMP | 30..90 / 10 | **30..80 / 10**, mærket SKØN, loft 100 |
| KVÆRN-TILSTAND | 0 manuel / 1 auto | **1 VÆGT / 2 TID** |
| VAND-TILSTAND | 0 manuel / 1 auto | **1 AUTO / 2 MANUEL** |
| TIDSFORMAT | 0 = 12 t / 1 = 24 t | **0 = 24 t / 1 = 12 t** (bevist fra `update_time`) |
| LYSSTYRKE | 1..10 (antaget) | 1..10 — spec siger 0..10; 0 bevidst udeladt |
| `def`-kolonnen | nuværende config-værdier | stock's `getNumber`-defaults |

* Sider omdøbt/omfordelt: BRYG (9) · **DAMP · VAND** (5, før KOMFORT) · SYSTEM (10). 26 → 24 rækker. Hver række bærer nu spec'ens tier (`ptier`).
* **Udvid-altid-reglen er fjernet.** v6 udvidede ethvert interval til at rumme en indlæst værdi. v6.1 udvider kun de to skønnede max (op til loftet); alt andet uden for spec vises rødt og spærrer aktivitets-start.
* Ny synlig hint: *"MAX = SKØN"* / *"SLIDER-SKALA · VÆLGER UKENDT"* i detaljen og `≈` i listen.
* Stepperen snapper nu til trin-gitteret (en °F-gemt værdi som 856 går til 850/860).

**2 · Fjernet / låst**
* `brewMode`, `sleepMode`, `sleepModeSetting` fjernet fra tabellen (og bryg-vagten på `brewMode` med dem).
* Ny række **VAND-TEMP · SENSOR** (ord 40, live, ingen editor) — varmt-vand-temperatur er en måling, ikke en indstilling.
* `hotWaterShortcut` flyttet til SYSTEM (spec tier 2).

**3 · breakTime-quirk** — PAUSE-TID er **låst** med rav note *"INGEN EFFEKT · MCU BRUGER PRE-INF. TID"*. Valgt frem for en editor med advarsel, fordi en knap uden virkning lover noget, maskinen ikke gør, og fordi max er ukendt. Værdien pakkes fortsat i ord113 lo. Demo i §11 L.

**4 · Pakning** — v6's `pack112`/`pack113` var korrekte; ingen ændring i bytes. Nyt: tryk begrænset til 3/4, re-sync før bryg sammenligner kun ord113's hi-byte, og en halv sync-sekvens køes aldrig.

**5 · Sikkerhed** — ingen af v6's frames havde ≠ 0 i ord 26/27/30 (audit i §7a). Nyt: central `frame_policy()` på hver cmd6-frame (trigger-ord, start-intent, pr.-ord interval), 3 `_Static_assert`, `BLOK`-tæller på DIAGNOSE, STOP DAMP kan ikke afvises, rollback kan ikke hænge gem-flowet. 24 nye selftest-cases for dette.

**6 · Boot-push** — UI-tekst *"VIRKER STRAKS"* → *"CONTROLLER STRAKS + CONFIG"* / *"STOCK-UI: EFTER GENSTART"*; config-only-toast *"GEMT · STOCK-UI: EFTER GENSTART"*; `RESTART_NEEDED`-teksten og en stdout-påmindelse ved exit forklarer genstart + boot-push; §5 og REVERT.md omskrevet.

**7 · Persistens** — skrive-mønstret er uændret (det var allerede token-udskiftning i stock's eget format). Nyt er **beviset**: `cjson_canon.py` + fuld tabel-rundtur (21 felter, min → max → original) med md5 tilbage på baseline.

**8 · Sprog / tidszone / presets** — SPROG oplåst (0..10, bevist). TIDSZONE fortsat låst (ingen tabel i spec'en). Presets/NFC fortsat udskudt (§9).

**9 · Kørsler** — selftest 33 → **87 checks**; previews 30 → **39**; nyt `parity_check.py` og `cjson_canon.py` i `build_previews.sh`; statisk build 0 warnings.

**Rettelser fundet undervejs (ikke i opgaven):**
* **Opslag uden for array:** v6's udvid-regel kunne give et enum-interval større end navne-tabellen (`grindMode=2` → `OPT_GRINDM[2]`). Væk sammen med reglen; en enum uden for tabellen vises nu som `(tal)`.
* `timemode`-mappingen var vendt om (se pkt. 1).
* Tryk på −/+ i en låst række gav en ripple uden virkning; låste rækker har nu kun TILBAGE.

**Uændret:** run.sh-kontrakten, alle 6 observerede frame-vektorer, keep-awake, fase-detektion, design-system, rendering, `EVIOCGRAB`, miljøvariable.

---

## CHANGES-v6.2

Boot-slot-integration: ourui kan nu selv vælge boot-UI, skifte til stock-UI'et uden genstart og sige ærligt til, når stock-UI'et skal genstartes. Kravene kommer fra opgaven og fra kollegaens `boot/BOOT.md` **§9 (UI-krav)**, som er fulgt punkt for punkt. Protokol, frame-politik, keep-awake, `EVIOCGRAB`, circle-clip og `run.sh` er uændrede (35 af de 39 v6.1-previews er byte-identiske; de fire der afviger er splash/DIAGNOSE (versionstekst) og de to SYSTEM-lister).

> **Om rækkefølgen:** BOOT.md fandtes ikke, da arbejdet begyndte, og blev skrevet undervejs. Runtime-skiftet var først bygget som opgaven beskrev det (en `/tmp`-shell-hjælper, der venter på ourui's exit og så starter stock). Da BOOT.md §9.2 kom, blev det lagt om til dens sekvens, fordi §UI-krav har forrang. **Der findes derfor ingen hjælper-proces og intet script i `/tmp` i v6.2.**

**1 · SYSTEM-række BOOT-UI** (BOOT.md §9.1)
* Nederst på SYSTEM, under overskriften *OPSTART · UI-VALG*: række **BOOT-UI** med værdien **OURUI** / **ORIGINAL** og underlinjen *"GÆLDER NÆSTE GENSTART"*.
* Tap åbner en visning med −/+ (− = ORIGINAL, + = OURUI), ANNULLER og GEM. Øverst står den aktuelle tilstand, læst live fra disken hvert sekund: *"NU: VORES UI (B) VED BOOT"* eller *"NU: ORIGINAL (A) VED BOOT"*. Hjælpeteksten er §9.1's, ordret: *"GÆLDER NÆSTE GENSTART"* · *"OTA KAN OVERSKRIVE DETTE VALG — SE BOOT.md §7"*.
* GEM gør præcis ét: OURUI = `open("/customer/new", O_CREAT|O_WRONLY, 0644)` + `sync()`; ORIGINAL = `unlink("/customer/new")` + `sync()`. Intet andet i `/customer` røres. Kaldene er idempotente. Ingen genstart fra knappen.
* **Fejl** (fx skrivebeskyttet filsystem): toast *"FLAG-FEJL · INTET ÆNDRET"*, visningen bliver stående med den gamle tilstand.
* **Vagt** (opgave-krav, strengere end BOOT.md): rækken er kun redigerbar, når `/new_customer` står i `/proc/mounts`, og `/new_customer/demo_v9` er kørbar og **er ourui**. Ellers er den låst med årsagen: *"SLOT B IKKE MOUNTET"*, *"SLOT B UDEN demo_v9"* eller *"SLOT B ER IKKE OURUI"*. Vagten gælder begge retninger: ligger der en vendor-binær i slot B, tilhører flaget vendor'ens OTA-mekanik, og ourui rører det ikke.
* Genkendelsen af ourui er et `what(1)`-mærke i binæren (`@(#)ourui …`); en v6.1-binær genkendes på en streng fra dens exit-besked. `build_previews.sh` afprøver det mod de ægte filer: `ourui_v6` og `ourui_v6_1.bin` = ourui; vendor-`demo_v9` (begge kopier) og `boot/switcher` = ikke ourui.
* Værdien der vises, er hvad `demo.sh` faktisk vil starte: *OURUI* kun hvis flaget findes **og** slot B er mountet **og** indeholder ourui; *SLOT B* hvis flaget findes, men binæren er en anden; ellers *ORIGINAL*.

**2 · SYSTEM-handling SKIFT TIL ORIGINAL NU** (BOOT.md §9.2 — runtime B→A, ingen genstart)
Rækken åbner en bekræftelse (ANNULLER / SKIFT NU). SKIFT NU kører §9.2's seks trin i den foreskrevne rækkefølge:

| §9.2 | Hvad ourui gør | Hvor i kilden |
|---|---|---|
| — | tegner sidste frame *"SKIFTER TIL ORIGINAL"* (bliver stående, til stock-UI'et tegner) | `do_switch()` |
| 1 | stopper egen aktivitet: UART tømmes og **lukkes**, touch-grebet slippes og input lukkes, fb unmappes og **lukkes** | `dev_release_impl()` |
| 2 | `fork()`; i barnet `setsid()`, `chdir("/customer")`, `setenv("LD_LIBRARY_PATH", …, 1)`, stdio → `/dev/null`, alle øvrige fds lukket, `execl("/customer/demo_v9", "./demo_v9", NULL)` | `switch_launch()` |
| 3 | venter **400 ms** (ur-styret), afslutter derefter. Imens læses UART ikke, og fb tegnes ikke — fd'erne er lukket | `switch_launch()` + hoved-løkken |
| 4 | flaget `/customer/new` røres ikke. **Ingen proces får et signal** — kilden indeholder 0 `kill()`-kald (tjekkes af `build_previews.sh`) | — |
| 5 | exit; alt hvad der måtte være tilbage, slippes af kernen | `main()` |
| 6 | log-linje i `cfgbak/boot_ui.log` med tid, `aarsag=bruger-knap`, stock-pid og den anvendte sti | `boot_log()` |

* **Én tilføjelse til trin 2:** barnet lukker *alle* arvede fds, ikke kun stdio. En arvet touch-fd ville ellers holde `EVIOCGRAB` i live inde i stock-processen, og stock-UI'et ville aldrig få et tryk. Selftesten måler det indefra: mock-"stock" melder `extra_fds=0`.
* **Én afvigelse fra §9.2's bogstav — `LD_LIBRARY_PATH`:** §9.2 skriver `"/customer/libs:/config/wifi"`. `demo.sh`'s A-gren sætter `"$LD_LIBRARY_PATH:/customer/libs:/config/wifi"`, dvs. boot-miljøets præfiks bevares. ourui gør det samme: tager sin egen arvede sti, fjerner slot-komponenterne og hægter A's på. Uden præfiks (fx når switcher'en har startet ourui) er værdien **præcis** §9.2's; med præfiks er den, hvad en ægte A-boot ville give stock-UI'et. Begrundelse: ligger et af stock-binærens biblioteker i en præfiks-mappe, ville den rene §9.2-værdi få stock til at dø ved opstart. Om præfikset findes på enheden, er ikke målt — se on-device-punkt N3. Ønskes den rene værdi, er det én linje i `ld_path_for_a()`.
* **Stock-UI'et kommer ikke op** (barnet dør inden for de 400 ms): ourui afslutter **ikke**, åbner UART/fb/touch igen, viser *"SKIFT FEJLEDE · …"* og logger det.
* **Spærret under aktivitet:** kører kværn/bryg/vand/damp, eller venter en write på ACK, afvises SKIFT NU med en toast. Stock-UI'ets boot-push (`cmd6 sub1 len30`) skriver bl.a. start-ordene til 0, og det skal ikke ske midt i et bryg. Det er en tilføjelse til §9.2.
* **Låst**, hvis `/customer/demo_v9` mangler eller ikke er kørbar (kun TILBAGE).
* Stock-UI'et starter frisk og læser selv `config.json`, så `RESTART_NEEDED` fjernes ved et vellykket skift.
* Skærmen viser vejen tilbage: *"TILBAGE HERTIL: 3 LANGE TRYK · ØVERST TIL VENSTRE (1,5 S)"* (switcher-gestussen, BOOT.md §2), eller — hvis `pidof switcher` er tom — *"SWITCHER-DAEMON KØRER IKKE"* og *"TILBAGE HERTIL: GENSTART"* / *"KUN TELNET"*, alt efter om flaget er sat.
* `OURUI_SWITCH_DRY=1`: SKIFT NU slipper intet og starter intet; loggen siger, hvad der ville være sket.

**Kendt konsekvens (BOOT.md §9.2, §1.2):** watchdog'ens slot er fastlagt ved boot. Er maskinen bootet i B, kører `test.sh new_customer`; dør stock-UI'et efter et runtime-skift — også hvis man selv kører `kill $(pidof demo_v9)` — starter watchdog'en **ourui** igen, ikke stock. Næste genstart følger flaget. Det er en sikker fallback og kræver ingen handling.

**3 · Den omvendte vej (kun dokumenteret — ikke en del af ourui)**
Stock → ourui uden genstart laves af switcher-daemonen ved gestus (BOOT.md §3.2): SIGSTOP stock → start `/new_customer/demo_v9` med `LD_LIBRARY_PATH=/new_customer/libs:/config/wifi` → verificér exe-linket → SIGKILL stock; kommer ourui ikke op, SIGCONT'es stock. Til test uden gestus findes samme mønster i `boot_install.py` (`launch_a_stopfirst` er retningen B→A; `boot-test` trin c er A→B via `tinj_hold`). Mens ourui kører, holder det touch eksklusivt, så switcher'en ser ingen tryk og gør intet.

**4 · Exit-påmindelse** (v6.1-integrationsnoten)
* REVERT viser nu en helskærms-påmindelse **før** exit, når den er sand: *"CONFIG ER ÆNDRET"* · *"STOCK-UI KRÆVER GENSTART AF demo_v9 FOR NYE VÆRDIER"* · kommandoen `kill $(pidof demo_v9)` i et felt for sig · *"KØR VIA TELNET · WATCHDOG STARTER DEN IGEN"* · *"ELLERS SENDER STOCK-UI GAMLE VÆRDIER VED BRYG"*. Knapper: TILBAGE / AFSLUT. Den skal være en skærm og ikke en toast: efter exit ejer stock-UI'et skærmen.
* **Sand** betyder: `RESTART_NEEDED` findes, **og** der kører en `/customer/demo_v9`, som blev startet før seneste GEM i samme boot. `RESTART_NEEDED` får derfor et stempel ved hver GEM (`boot_id=… mono_s=…`), som sammenlignes med processens starttid i `/proc/<pid>/stat`. Påmindelsen vises altså **ikke** efter en maskin-genstart, **ikke** når stock-UI'et er genstartet siden GEM, og **ikke** i slot B-boot, hvor der intet pauset stock-UI er. Et v6.1-flag uden stempel regnes for gældende.
* stdout-beskeden ved exit følger samme regel og bruger samme ordlyd.
* Stier og kommandoer vises med små bogstaver (shell skelner). Stroke-fonten har fået a–z, `$`, `'` og `§`; de bruges kun i markerede kode-spænd, resten af UI'et er versalt som før.

**5 · REVERT-knappen afhænger nu af, hvordan ourui er startet**
* **run.sh-tilstand** (der findes et pauset stock-UI): uændret — exit, `run.sh` SIGCONT'er. Eneste forskel er påmindelsen i pkt. 4, hvis den er sand.
* **Slot B-boot** (ourui *er* `/new_customer/demo_v9`, intet stock-UI): en ren exit ville blot få watchdog'en til at starte ourui igen. REVERT åbner derfor bekræftelsen fra pkt. 2. Det står ikke i BOOT.md; det er valgt, fordi knappen ellers ville være virkningsløs.
* SKIFT TIL ORIGINAL NU i run.sh-tilstand gør det samme som REVERT (intet startes — der må aldrig køre to `demo_v9`).

**6 · DIAGNOSE**
* Siden kan nu **scrolles lodret** (samme gestus og indikator som listerne); de 14 eksisterende felter står uændret øverst.
* Ny blok *BOOT · SLOT · SWITCHER*: `FLAG /customer/new` (FINDES / FINDES IKKE) · NÆSTE BOOT (OURUI / ORIGINAL / SLOT B) · OURUI STARTET FRA (SLOT B / RUN.SH / SLOT A, fra `/proc/self/exe`) · SLOT B (OURUI FUNDET / IKKE MOUNTET / UDEN DEMO_V9 / IKKE OURUI) · SWITCHER-DAEMON (KØRER / KØRER IKKE, `comm == switcher` i `/proc`) · STOCK-PROCES (INGEN / PAUSET / **KØRER SAMTIDIG!** i rødt) · PID-linje (ourui / stock-UI / switcher) · den fulde exe-sti · seneste skift-hændelse fra `/tmp/switcher.log` (sidste linje med `skift` eller `GESTUS`, med klokkeslæt; *"LOG IKKE LÆSBAR"* / *"INGEN SKIFT-HÆNDELSE I LOGGEN"* ellers). Lange stier og hændelser afkortes med `…`.
* Boot-tilstanden læses kun, mens den vises (SYSTEM, DIAGNOSE, system-visningerne), én gang i sekundet.

**7 · Nye persistente skrivninger** — se `REVERT.md`: `/customer/new` (kun ved GEM af BOOT-UI) og `cfgbak/boot_ui.log` (én linje pr. flag-ændring og pr. runtime-skift). `RESTART_NEEDED` har fået stempel-linjen. Intet i `/tmp`.

**8 · Lokal verifikation**
* **Selftest: 87 → 152 checks, 0 fejl** (`round_v6/selftest.txt`). Nyt, alt mod en mock-rod (`/tmp/ourui_v6_selftest_*/root` med `/customer`, `/new_customer`, `/proc`, `/tmp`):
  * *Flag-logik (25):* alle fire låse-årsager, vendor-flag urørt i begge retninger, touch/rm/idempotens, intet andet i `/customer` ændret, skrivebeskyttet mappe, live aflæsning, slot B der forsvinder efter flaget er sat, v6.1-genkendelse, mærke hen over en 64 KiB-læsegrænse, den ægte vendor-binær, exe-slot, proces-scan (pauset / kørende / `(deleted)` / slot B-`demo_v9` / egen pid), `switcher.log` (mangler / uden skift / stor fil), `LD_LIBRARY_PATH`-afledning.
  * *Påmindelsens sandhed (7):* de seks kombinationer af flag, stempel, boot-id og starttid + at GEM stempler flaget.
  * *Runtime-skift (14):* hele §9.2-sekvensen kørt for alvor, med en **kopi af test-binæren** som mock-"stock" i `<rod>/customer/demo_v9`. Den melder selv tilbage: `argv0=./demo_v9`, egen session, cwd `/customer`, stdio = `/dev/null`, **0 arvede fds**, startet **efter** trin 1, stien den fik. Desuden: målt ventetid (ca. 400 ms; 403–412 ms i kørslerne), flaget urørt, log-linjen, `RESTART_NEEDED` fjernet, stock der dør straks (ourui bliver og tager skærmen igen), dry-run, run.sh-tilstand (intet startes), manglende binær. Ingen rigtig UI-proces startes, og intet signal sendes.
  * *UI-ruter (15):* REVERT i alle tre situationer, SKIFT NU under damp, påmindelse → AFSLUT/TILBAGE, låste visninger i hit-testen, BOOT-UI gennem rigtige touch-events (−, +, GEM, ANNULLER, skrivefejl).
  * *Layout (4):* alle tekster i alle tilstande målt mod gauge-ringens inderkant, felt-bredderne og afstanden til gauge-enderne; rækker og DIAGNOSE-celler mod deres kolonner (også med 5-cifrede PID'er og 80 tegn lange stier).
* **Previews: 39 → 51**, alle re-renderet; **circle-tjek 51/51, 0 violations**. Nye: `39_list_system_boot_locked` · `40_bootui_detail` · `41_bootui_changed` · `42_bootui_saved_toast` · `43_bootui_locked` · `44_switch_confirm_boot` · `45_switch_confirm_runsh` · `46_switch_blocked_activity` · `47_switching_bye` · `48_exit_banner` · `49_diag_boot` · `50_diag_boot_runsh`. `35_list_system_scrolled` viser nu bunden af SYSTEM med de to nye rækker.
* **Spec-afstemning:** uændret, **ALT AFSTEMT** (de nye rækker er system-handlinger og ligger uden for `SET[]`; tabellen er stadig 24 rækker).
* **Statisk build:** 0 warnings (også 0 nye med `-Wall -Wextra`).
* `round_v6/boot_check.txt`: binær-genkendelsen mod de ægte filer, 0 `kill()`-kald, `run.sh` uændret.

**Rettelser fundet undervejs**
* En gentegning af hero-feltet kunne viske kanten af +-knappen ud på BOOT-UI-skærmen (felterne overlappede med 30 px). Fundet ved gennemsyn af previewet i fuld størrelse; hero-feltet er indsnævret.
* Sidste frame før skiftet frøs med en halv touch-ripple; ripple og toast ryddes nu, før den tegnes.
* Ved 5-cifrede PID'er blev tre DIAGNOSE-celler bredere end kolonnen; PID'erne har fået deres egen linje.

**Ikke verificeret (kræver enheden)** — se §11 N.

---

## CHANGES-v6.3

Produktnavn og sprog. UI'et hedder nu **Meraki+**, og hver synlig tekst findes på engelsk (default) og dansk. Der er ikke rørt ved protokol, frame-politik, keep-awake, `EVIOCGRAB`, circle-clip, boot-logik eller `run.sh` — det er eftervist, se pkt. 9.

**1 · Produktnavn: Meraki+**
* **Splash:** ordmærket *MERAKI* med et "+"-badge (crema-skive med mørkt plus, hævet som et superscript), tagline *"ADVANCED BREWING INTERFACE"* / *"AVANCERET BRYGGE-INTERFACE"* og linjen *"V6.3 · CONFIG LOADED"* / *"… INDLÆST"*.
* **HJEM:** en diskret titellinje *MERAKI+* øverst inden for gauge-ringen (plusset i accent-farven). Den ligger i det statiske lag og koster intet pr. frame.
* **DIAGNOSE:** VERSION-cellen viser *MERAKI+ V6.3*; nederst er der en ny *OM*-blok med UI-sprog, ordmærke, version og tagline.
* **Alle tekster, der før sagde OURUI,** siger nu MERAKI+: BOOT-UI-valget (MERAKI+ / ORIGINAL), *"NU: MERAKI+ (B) VED BOOT"*, *"MERAKI+ LUKKER · STOCK-UI STARTER"*, *"MERAKI+ ER AFSLUTTET"*, låse-årsagerne, DIAGNOSE-cellerne og noterne i settings.
* **Intet generations-navn** i nogen UI-streng — samme software kører på flere maskin-generationer. Binærens `what(1)`-mærke er ændret fra `… merakitech-gen1 display-ui` til `@(#)ourui V6.3 meraki-plus display-ui`; BOOT-UI-vagten genkender kun de første 10 tegn (`@(#)ourui `), så v6.1-, v6.2- og v6.3-binærer genkendes alle (`boot_check.txt`).
* **Kode-navnet er uændret `ourui`:** kildefil, binær, proces-navn, `/new_customer/ourui/`, `OURUI_*`-variablerne, `cfgbak/`-logs og stdout-præfikset `== ourui:`. `run.sh` og `boot_install.py` afhænger af dem. I UI'et står `ourui` kun inde i viste stier (fx `ourui/lang`).
* **Afvigelse fra BOOT.md §9.1's ordlyd:** hjælpeteksten *"Vores UI (B) ved boot"* er blevet *"MERAKI+ (B) VED BOOT"*. Betydningen er den samme; produktnavnet har forrang.

**2 · UI-sprog: ny SYSTEM-række UI LANGUAGE / UI-SPROG**
* **Øverst på SYSTEM**, så den kan findes uden at kunne læse resten. Valgmulighederne står altid som *ENGLISH* og *DANSK* (endonymer, ens i begge sprog).
* **Live:** et tryk på −/+ skifter hele UI'et med det samme (som lysstyrke-previewet). ANNULLER går tilbage til det gemte sprog; GEM gør det varigt. Kvitteringen kommer på det nye sprog: *"SPROG GEMT · DANSK"* / *"LANGUAGE SAVED · ENGLISH"*. Ingen genstart.
* **Gemmes i ourui's egen fil `/new_customer/ourui/lang`:** ren tekst, præcis `en` eller `da` (2 bytes). Skrives atomisk: tmp-fil i samme mappe → `fsync` → genlæs og sammenlign → `rename()` → `fsync` af mappen.
* **Læses én gang ved opstart**, før første frame. Mangler filen, kan den ikke læses, eller står der andet end `en`/`da`: **engelsk**. Læsningen tåler blanktegn, linjeskift og store bogstaver, så `echo da > /new_customer/ourui/lang` også virker. Alt andet (`de`, `dansk`, tom fil, binært, for langt) giver engelsk.
* **Kan filen ikke skrives** (fx skrivebeskyttet filsystem), gælder sproget alligevel resten af sessionen, og toasten siger det: *"SPROG AKTIVT · FIL IKKE GEMT"*. Det er en bevidst undtagelse fra alt-eller-intet-reglen for settings: en bruger, der ikke kan læse engelsk, skal ikke låses ude af sit sprog af en skrivefejl.
* **Rører ikke stock-UI'et:** rækken har ingen config-nøgle og intet MCU-ord. GEM skriver ikke til `config.json`, sender ingen cmd6-frame, laver ingen backup og intet `RESTART_NEEDED`. Selftesten måler alle fire dele.
* **Stock-UI'ets sprog er en anden række:** *STOCK-SPROG* (`config.language`, 11 sprog uden dansk) virker som i v6.1. Den er omdøbt fra *SPROG*, så de to ikke kan forveksles, og dens note peger på UI-SPROG.
* **DIAGNOSE / OM** viser det aktive sprog og hvor det kommer fra: *FRA FIL* · *STANDARD (INGEN FIL)* · *UKENDT VÆRDI I FIL* (rav) · *IKKE GEMT* (rav).
* `OURUI_LANGFILE=<fil>` flytter filen (til test og til v6.4's SIM-kørsel).
* I koden er rækken en almindelig `SET[]`-række med flaget `SF_LOCAL`. `--dump-table` udelader den, så spec-afstemningen stadig ser 24 rækker.

**3 · Streng-tabellen**
* **Ét sted:** `STR_TABLE` i `ourui_v6.c` — 324 id'er, hver med engelsk og dansk tekst og en klasse. Ingen tegne-kode indeholder længere en tekst; `SET[]` henviser til id'er for label, enhed, note, hint og enum-navne.
* **Opslag pr. render:** `TR(id)` slår op i det aktive sprog, hver gang en tekst tegnes. Intet caches hen over et sprogskift; et skift markerer blot det statiske lag til gentegning. Ukendt sprog giver engelsk.
* **Klasse = det felt teksten står i** (toast, note, fodlinje, knap, titel, DIAGNOSE-celle …). Selftesten måler hver klassificeret streng i begge sprog mod feltets pixel-budget; tekster med pladsholdere måles i deres sammenhæng af layout-testene.
* **Regler, som selftesten håndhæver for hele tabellen:** begge sprog udfyldt · samme printf-konverteringer i samme rækkefølge · samme tal og decimaltegn · enheder ens · hvert tegn findes i stroke-fonten · intet generations-navn · kode-navnet `ourui` kun i viste stier og på konsollen.
* **Appendiks A er genereret** fra binærens egen tabel (`--dump-strings` → `strings.tsv` → `i18n_appendix.py`); `build_previews.sh` fejler, hvis de to ikke stemmer.
* **Ikke oversat (med vilje):** log-filerne `cfgbak/config_writes.log` og `cfgbak/boot_ui.log`, indholdet af `RESTART_NEEDED` og stderr-diagnostik — de læses af tests og værktøj og er uændrede. *SENESTE SKIFT* på DIAGNOSE viser switcher-daemonens log-linje ordret (kollegaens tekst, dansk). stdout-linjerne ved exit **er** oversat (ASCII, samme `== ourui:`-præfiks).

**4 · Tal og enheder er ens i begge sprog**
* Decimalpunktum, `°C`/`°F`, `G`, `S`, `BAR`, `V`, `HZ` og alle tal er identiske; kun ord er oversat. Enhederne står i tabellen med samme tekst i begge kolonner, og selftesten sammenligner dem.
* Én dansk tekst havde decimalkomma: *"ØVERST TIL VENSTRE (1,5 S)"* er nu *"(1.5 S)"*, som alle andre tal i UI'et.
* TIDSFORMAT viste *24 T / 12 T*; nu *24-TIMERS / 12-TIMERS* (engelsk *24-HOUR / 12-HOUR*), så det er ord og ikke en enhed, der skifter.

**5 · Danske tekster, der er ændret i v6.3**

| Før (v6.2) | Nu | Hvorfor |
|---|---|---|
| OURUI (alle steder) | MERAKI+ | produktnavn |
| NU: VORES UI (B) VED BOOT | NU: MERAKI+ (B) VED BOOT | produktnavn |
| SPROG | STOCK-SPROG | skelnes fra UI-SPROG |
| GRUPPE-TEMP · E1 · GRUPPE OVER 110° · E2 · GRUPPE-SENSOR · ORD 114 · BRYGHOVED-SETPOINT | GRUPPEHOVED · E1 · GRUPPEHOVED OVER 110° · E2 · GRUPPEHOVED-SENSOR · ORD 114 · GRUPPEHOVED-SETPOINT | korrekt term (group head = gruppehoved); enheden °C viser, at det er en temperatur. Stat-labelen på HJEM er fortsat den korte *GRUPPE* — *GRUPPEHOVED* kan ikke stå i en 94 px-kolonne |
| BRYG-TEMPERATUR | BRYG-TEMP | label + værdi stødte sammen i °F (pkt. 7) |
| MÆLK-TEMPERATUR · MÆLK-TEMP · E2 · MÆLK-SENSOR | MÆLKETEMP · MÆLKETEMP · E2 · MÆLKESENSOR | korrekt sammensætning + plads i °F |
| VAND-TEMP · SENSOR | VAND-SENSOR | label + værdi stødte sammen ved ≥ 100,0° |
| SLIDER-SKALA · VÆLGER UKENDT (hint) | TRIN = SKØN (hint) + noten *"… · SLIDER 0-100"* | hintet ragede ind over gauge-ringen (pkt. 7) |
| 24 T / 12 T | 24-TIMERS / 12-TIMERS | pkt. 4 |
| ØVERST TIL VENSTRE (1,5 S) | ØVERST TIL VENSTRE (1.5 S) | pkt. 4 |
| OURUI STARTET FRA · SPEJL 112/113/114 (DIAGNOSE) | STARTET FRA · SPEJL 112-114 | plads i kolonnen på begge sprog |
| SKIFT FEJLEDE · *(fri fejltekst)* | SKIFT FEJLEDE · INGEN STOCK-BINÆR / FORK-FEJL / STOCK-UI STOPPEDE | faste, oversatte årsager; den fulde fejltekst står fortsat i `boot_ui.log` |
| KUN STOCK-UI · OURUI ER DANSK (note) | KUN STOCK-UI · MERAKI+: SE UI-SPROG | passer ikke længere |
| splash: OURUI · MERAKITECH … · V6.2 | ordmærke MERAKI+ · tagline · V6.3 | pkt. 1 |

Alle øvrige danske tekster er ordret som i v6.2.

**6 · Layout-ændringer**
* SYSTEM har én række mere; BOOT-UI og SKIFT TIL ORIGINAL NU er række 12 og 13 (skærm-positionen ved fuld scroll er uændret).
* DIAGNOSE er 151 px længere (OM-blokken); max scroll 431.
* HJEM-underlinjens felt er udvidet fra 296 til 336 px (plads til °F-værdier og de engelske tekster); ringen tillader det.
* Liste-rækker med lange enum-navne vælger skrift i tre trin (stor → mellem → lille) og afkorter først derefter med `…`. Det sker ét sted: *STOCK-SPROG = KINESISK FORENKL.* vises på dansk som *KINESISK FOR…* i listen; detalje-visningen viser hele navnet.
* Toasts afkortes nu med `…`, hvis en tekst er bredere end pillen (før blev den klippet af feltets kant).
* Interval-linjen for rækker uden enhed havde et dobbelt mellemrum før hintet; rettet.

**7 · Fejl fundet af de nye layout-tjek (alle fandtes også i v6.2)**
* **°F:** med TEMP-ENHED = °F stødte *BRYG-TEMPERATUR* sammen med værdien i listen (11 px), og HJEM-underlinjen under damp var 6 px bredere end sit felt. Layout-testen måler nu temperatur-rækker i begge enheder.
* **LYDSTYRKE-detaljen:** interval + hint var 336 px bredt, hvor der kun er 296 px inden for gauge-ringen; teksten lå ind over ringen i begge sider. Der var intet preview af netop den skærm, og den gamle test målte mod cirklens korde i stedet for ringens inderkant.
* **VAND-SENSOR:** ved kedel-temperatur ≥ 100,0° stødte værdien sammen med labelen.
* **SKIFT FEJLEDE-toasten** kunne blive ca. 460 px bred i en 356 px pille.

**8 · Lokal verifikation**
* **Selftest: 152 → 193 checks, 0 fejl**, kørt to gange: UI-sprog engelsk (`selftest.txt`) og dansk (`selftest_da.txt`). Nyt:
  * *Tabellen (9):* alle 324 id'er har EN + DA · samme printf-konverteringer · samme tal/enheder · intet generations-navn (heller ikke i binær-mærket) · `ourui` kun i stier/konsol · alle tegn har en glyf · 196 klassificerede strenge × 2 sprog inden for pixel-budgettet · endonymer · `TR()` pr. kald med fallback.
  * *Sprog-filen (12):* mangler → engelsk · `da`/`en` · tolerant læsning · 13 ugyldige værdier → engelsk · tom fil · sti der er en mappe · ingen læserettighed · `lang_save` skriver præcis 2 bytes uden efterladt tmp-fil · ugyldigt sprog afvises · manglende mappe · skrivebeskyttet mappe (gammel fil intakt).
  * *UI-SPROG-rækken (13), gennem rigtige touch-events:* første række på SYSTEM · åbner med det aktive sprog · **+** skifter live uden at skrive · ANNULLER fortryder · GEM skriver `da` og kvitterer på dansk · **config.json-md5, `config.language`, write-tæller, `RESTART_NEEDED` og backup-mappe uændret** · **0 cmd6-frames, 0 bytes til UART** · huskes efter "genstart" · tilbage til engelsk · STOCK-SPROG ændrer ikke UI-sproget · skrivefejl → sproget gælder sessionen ud, advarsel vist · fase-navn, fejltekst og HJEM-tekster følger sproget uden genstart.
  * *Layout (alle eksisterende tjek kører nu i begge sprog, + nye):* detalje-visningen for alle 25 rækker (label og interval mod **ringens inderkant**, temperatur-rækker i °C og °F, liste-række med label + bredeste værdi + enhed) · system-visningerne i alle tilstande · DIAGNOSE (label-par og celle-par må ikke røre hinanden, også med worst-case-værdier; OM-blokken) · HJEM (stat-label-trioer mod kolonnerne, chart-tekster, knapper, titellinjen mod fremskridts-ringen) · splash.
* **Previews: 51 → 56 scenarier, i to sprog (112 billeder)**; **circle-tjek 56/56 + 56/56, 0 violations**. Nye: `51_detail_ui_language` · `52_ui_language_live_preview` (ét tryk senere: hele skærmen på det andet sprog) · `53_ui_language_saved_toast` · `54_home_other_language` · `55_diag_about`. `49`/`50` viser boot-blokken som før (scroll 280). Kontakt-ark: `engelsk`, `dansk`, `EN | DA side om side`.
* **Spec-afstemning:** uændret, **ALT AFSTEMT** — 24 rækker (UI-SPROG er ikke en spec-parameter).
* **Statisk build:** `zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2` → **0 warnings**. Med `-Wall -Wextra`: samme to kategorier som v6.2 og ingen nye (den ene ekstra linje er den nye tabel-række).
* `round_v6/i18n_check.txt`: kilden har 0 linjer med et generations-navn uden for selftestens forbuds-liste; device-binæren har 0 sådanne strenge (ud over build-mappens sti i debug-info, som ikke kan vises) og indeholder begge taglines; appendikset er i sync; samme 56 scenarier på begge sprog.
* `artifacts/config.json` er urørt (md5 `<config-md5-redacted>`); config-kopierne efter den engelske og den danske preview-kørsel er byte-identiske.

**9 · Uændret — og hvordan det er eftervist**
* **53 funktioner er byte-identiske med v6.2** (sammenlignet tekstuelt mod `ourui_v6_2.c`), bl.a. `crc16`, `build_write`, hele write-køen, `rx_feed`, `word_ok`, `frame_policy`, `setting_mcu_push`, `act_push`, `act_push_steam_stop`, `proto_keepawake`, `phase_raw`, hele `cfg_*`-persisten, `boot_flag_set`, `boot_refresh`, `restart_pending`, `ld_path_for_a`, `bin_is_ourui`, `inp_open`/`inp_poll`, `dev_release_impl`, `fb_open`/`fb_present`, `pset`/`hspan`, `hit_test`, `ui_touch`, `revert_request`. `TRIG_NEVER`, `TRIG_START`, intent-asserts og timing-konstanterne er de samme linjer.
* De seks berørte logik-funktioner (`do_switch`, `sys_confirm`, `save_step`, `edit_step`, `settings_load`, `switch_launch`) er kun ændret ved streng-opslag, en årsags-kode til den oversatte toast og de to sprog-kroge.
* Selftestens frame-vektorer (de 6 observerede frames byte for byte), trigger-ord-audit (ord 26/27/30 altid 0), boot-push-ækvivalens og config-persist er de samme tests som i v6.2 og er grønne i begge sprog.
* `run.sh`: md5 `3d799859549d2b8ba32245453022c20d` (uændret). `boot/` og `sim/` er ikke rørt.

**10 · Til v6.4 (SIM-HAL — ikke lavet i denne runde)**
* SIM-HAL'en mod `sim/CONTRACT.md` er bevidst ikke påbegyndt. Det, v6.3 stiller til rådighed for den: `OURUI_LANGFILE` (sprog-fil uden for `/new_customer`), `--lang en|da` på preview-binæren (previews, selftest, `--textw`), og at hver tekst kan slås op på id i `strings.tsv`.

**Ikke verificeret (kræver enheden)** — se §11 O.

---

## CHANGES-v6.4

SIM-build og E2E. UI'et kan nu køres uden hardware mod test-agentens simulerede MCU, og hele E2E-suiten er grøn mod det **rigtige** UI på engelsk og dansk. Enheds-binæren gør det samme som v6.3: kun versions-strengen og én toast-tekst er ændret (pkt. 2). Brugsvejledning, HAL, state-felter og kroge står i [§13](#13-sim-build-og-e2e-v64); afvigelser fra kontrakten står desuden i [sim/CONTRACT.md](../../sim/CONTRACT.md) (TILLÆG v6.4).

**1 · Resultat**

| Hvad | Resultat |
|---|---|
| `sim/selftest.py` (protokol-niveau, uændret) | **107 PASS / 0 FAIL** |
| 22 scenarier, rigtigt UI, **engelsk**, med UI-profil | **22/22 scenarier · 553/553 checks** · settings_save_matrix **188/188** |
| 22 scenarier, rigtigt UI, **dansk**, med UI-profil | **22/22 scenarier · 553/553 checks** · settings_save_matrix **188/188** |
| Replay-scenarierne (v2 / v3 / session) | grønne i begge sprog — og også i den rå kørsel |
| De samme 22 scenarier **råt** (uden UI-profil) | 17/22 scenarier · 527/553 checks · matrix 172/188 — de 26 checks, der fejler, er præcis dem i pkt. 4 |
| ourui's egne touch-scenarier (4 stk., ægte FIFO-touch) | engelsk **4/4 · 87/87 checks**, dansk **4/4 · 87/87 checks** |
| SKIFT TIL ORIGINAL NU uden dry-run (BOOT.md §9.2 i sandkassen) | **9/9** |
| Enheds-build: selftest | **199 checks, 0 fejl** — engelsk og dansk (v6.3: 193; de 6 nye dækker SIM-boot-pushen) |
| Enheds-build: previews | 56 × 2 sprog re-renderet; **circle-tjek 56/56 + 56/56, 0 violations**; **100 af 112 byte-identiske med v6.3**, de 12 andre er de tilsigtede (pkt. 2) |
| Spec-afstemning · config-format · boot-tjek · i18n-tjek | **ALT AFSTEMT** · 5/5 · 10/10 · 6/6 |
| Statisk build | enhed, SIM-ARM og SIM-vært: **0 warnings** (talt med frisk compiler-cache) |

Rapporter, PNG-snapshots og kontakt-ark: `sim/reports/v6.4/` — start i `SUMMARY.md`; ét billede pr. scenarie i `overview_en.html` / `overview_da.html`.

**2 · Enheds-build'et er v6.3 plus to tekster — sådan er det eftervist**
* **Kildetekst efter preprocessoren** (`build_sim.sh`): enheds-build'et af v6.3 og v6.4 er sammenlignet token for token. 65 838 tokens i begge; **10 forskelle**: 8 × `"V6.3"` → `"V6.4"` og toasten på engelsk og dansk. Intet andet — ingen ny gren, intet nyt kald, ingen ny konstant.
* **Sådan holdes det væk:** alt SIM-specifikt står i `#ifdef SIM_RT` eller i en håndfuld makroer (`UART_DEV`, `INP_READ`, `SIM_TICK` …), som i enheds-build'et er tomme eller lig v6.3-udtrykket.
* **Binæren:** `strings ourui_v6` indeholder ingen `OURUI_SIM`, `ourui-sim` eller `boot_push`. `-Wall -Wextra` giver præcis de samme advarsler som v6.3 (25 × SET[]-initializer, 1 × ubrugt `fill_rect`).
* **Previews:** v6.3 er bygget fra `ourui_v6_3.c` og renderet i en tmp-mappe; alle 112 billeder er sammenlignet byte for byte (`v63_compare.txt`). De 12, der afviger: `00_splash`, `26_diag`, `49`/`50_diag_boot*` og `55_diag_about` (versions-strengen; i 49/50 ses VERSION-cellens underkant øverst i det scrollede felt) og `25_save_fail_no_ack` (toasten) — hver på begge sprog.
* **Toasten:** GEM uden ACK viser nu *GEM FEJLET · INGEN ACK · INTET GEMT* / *SAVE FAILED · NO ACK · NOT SAVED* (før: *INGEN ACK · INTET GEMT* / *NO ACK · NOT SAVED*). Teksten siger nu, hvad der gik galt, før den siger hvorfor; den fylder 304 px af pillens 312 på dansk. Scenariet `no_ack` asserter *GEM FEJLET*.
* **Uændret:** protokol, frame-politik (ord 26/27/30 = 0, spec-intervaller, eksplicit start-intent), keep-awake ord1 = 0, `EVIOCGRAB`, circle-clip, BOOT-UI-/skift-logik (BOOT.md §9.2), aktivitets-spærret skift, exit-påmindelse, EN/DA-tabellerne (323 af 324 id'er uændrede), `run.sh` (md5 `3d799859…`), `boot/`.

**3 · SIM-HAL (kun i `-DOURUI_SIM`)**
* **Samme main-loop.** SIM-build'et bruger enhedens `main()`: poll-rotation, `wq_step`, keep-awake, `inp_poll`, render-kadence 33/100 ms. Tre kroge (`SIM_TICK`, `SIM_FRAME`, `SIM_EXIT`) er sat ind og er tomme på enheden.
* **Framebuffer:** én 480×480 BGRA-buffer i hukommelsen. `fb_present()` kopierer kun de rects, compositoren melder som ændret — præcis som enheden — så en fejl i dirty-rect-logikken også ville kunne ses i snapshots.
* **Touch:** rå 16-byte evdev-records fra en FIFO, afkodet af enhedens egen `inp_poll()`. Records afvikles efter deres tidsstempler, så et 700 ms-hold skrevet på én gang også varer 700 ms.
* **UART:** `uart_open()`/`uart_flush()` er enhedens; kun stien er skiftet til pty'en.
* **Stier:** alt går gennem sandkasse-roden. `OURUI_SIM_ROOT` er påkrævet, og `/` afvises.
* **State-JSON og ctrl-socket:** §13.3–13.4. Krogene kalder de samme funktioner som et tryk.
* **Boot-push:** kun i SIM (pkt. 6).

**4 · Hvor reference-klienten og det rigtige UI er forskellige (UI-profilen)**

Scenarierne blev skrevet mod `sim/ref_client.py`. Fem af dem har forventninger, der kun kan opfyldes af reference-klienten: dens syntetiske layout, eller at den hverken har ourui's vagter eller ourui's bevidste afvigelser fra `parity_spec.json`. At bøje UI'et efter dem ville betyde at sende værdier uden for spec, starte bryg under en aktiv fejl, låse PAUSE-TID op eller tillade en mørk skærm. Scenarie-filerne er ikke rørt. I stedet læser runneren `sim_ui_profile.json`, når klienten er SIM-build'et, og erstatter de berørte forventninger 1:1 med det, ourui gør — antallet af checks er uændret, hver override skal ramme præcis én forventning, og rapporten lister dem alle.

| Scenarie | Reference-klienten | ourui (bevidst, dokumenteret) | Overrides |
|---|---|---|---|
| `idle` | side-farvet bånd øverst; fase-farvet baggrund | gauge-bue i fase-gradient; mørk baggrund med hvid hero-tekst (§2) | 2 farve-prøver |
| `grind` | KVÆRN-knap i (130,160) | KVÆRN i (150,372) (§3) | 1 touch-koordinat |
| `fault_e1` | starter bryg under aktiv fejl; MCU'en stopper det | start spærres: *FEJL AKTIV · START SPÆRRET* (§6) | 1 |
| `out_of_spec` | sender værdier uden for spec; sim'en logger dem | frame-politikken afviser blokken; BLOK ≥ 1 (§7a) | 1 |
| `settings_save_matrix` | parity_spec ord-for-ord | PRE-INF. TID = `sub113 len1` · KVÆRN-TILSTAND = `sub2 len1` · GEM af uændret værdi skriver intet · PAUSE-TID låst · LYSSTYRKE 0 udeladt · `init`/`patch` ikke eksponeret (§4.1–4.5) | 16 |

Dertil kommer **én ren timing-tolerance** (`replay_v3`): vinduet for pre-infusions-plateauet slutter 10–20 ms før trykspringet ved 8× afspilning og består kun, når klientens state-strøm er grov (reference-klienten: 10 Hz). ourui skriver en linje pr. ændret frame; vinduet er forkortet fra 15,0 til 14,9 s. Alle andre tids-vinduer i suiten har mindst 170 ms margin (målt over tre kørsler).

De fem matrix-rækker er desuden kørt gennem den ægte stepper + GEM med touch (`sim_scenarios/ui_rows_real_frames.json`). `run_e2e.py --no-ui-profile` kører scenarierne råt.

**5 · Scenarier der krævede noget af UI'et — og hvad**

| Scenarie | Hvad der manglede | Rettelse | Hvor |
|---|---|---|---|
| alle (universal-check *boot-push*) | ourui boot-pusher ikke | boot-push `sub1 len30` + `sub112 len3` gennem frame-politikken | **kun SIM-build** |
| `no_ack` | toasten indeholdt ikke *GEM FEJLET* | ny toast-tekst (pkt. 2) | **enhed + SIM** |
| `no_ack`, `settings_save_matrix` | en `save`-krog, der ramte en keep-awake i flugt, blev afvist med *VENTER PÅ CONTROLLER* | krogen venter op til 600 ms på en tom write-kø | kun SIM-krog |
| `boot_flag_toggle`, `runtime_switch` | runnerens sandkasse mangler det, BOOT-UI-vagten læser → LÅST | SIM-build'et supplerer sandkassen (mounts, slot B-mock med ourui-mærke, mock-stock) | kun SIM-build |
| `runtime_switch` | dry-run viste kun en toast; scenariet kræver farvel-skærmen | dry-run viser farvel-skærmen i 1 s | kun SIM-build |
| `sleep_wake` | frame-politikken afviser ord1 = 1, og ourui vækker en sovende controller inden for 3 s | `raw_write` tillader ord1 ∈ {0,1} og sætter keep-awake på pause | kun SIM-krog |
| `grind`, `gesture_switcher` | en gestus skrevet på én gang til FIFO'en varede 0 ms | touch-kø styret af records'enes tidsstempler | kun SIM-HAL |
| `brew`, `hotwater`, `steam_*` | chippen viser FÆRDIG i 8 s; scenarierne venter READY | `phase` = rå fase, `phase_ui` = chippen | kun state-JSON |
| `language` | `lang` skal være `config.language`, ikke UI-sproget | `lang` + nyt felt `ui_lang` | kun state-JSON |
| `no_ack`, `out_of_spec`, `fault_e1`, matrix (tekst-asserts) | asserts er danske; UI'et kan stå på engelsk | `toast.text` = dansk tabel-tekst, `toast.shown` = skærmens | kun state-JSON |
| `idle`, `grind`, `fault_e1`, `out_of_spec`, matrix, `replay_v3` | pkt. 4 | UI-profil (runner), ingen UI-ændring | — |

`crc_garbage`, `fault_e2`, `fault_e3`, `offline`, `replay_v2` og `replay_session` bestod med HAL'en alene.

**6 · Fund og åbne punkter**
* **Boot-push på enheden — kræver en beslutning.** ourui har aldrig boot-pushet; kontrakten antog det. I run.sh-tilstand er det ligegyldigt: det pausede stock-UI har gjort det. Ved **slot B-boot** (v6.2) starter stock-UI'et aldrig. MCU'ens registerfil ligger i RAM og initialiseres til faste startværdier (REGISTERMAP §2: ord 11 = 920, ord 114 = 50, ord 14 = 4, ord 25 = 550 …), så efter en kold start står controlleren med **bryg-setpoint 92,0 °C og gruppehoved 50 °C, hvor config siger 87,0 og 70** — indtil ourui skriver ordene. Det sker ved GEM, i start-blokkene (bryg/vand medsender ord 11, 14 og 17; damp ord 25; kværn ord 4) og, for bryg-vægt og pre-infusion/gruppehoved, lige før BRYG. Efter den statiske analyse varmer kedlen altså mod den forkerte temperatur frem til første bryg; det er **ikke målt på maskinen** (testplan P3). HJEM ville vise det ærligt (*BRYG-TEMP SAT TIL 92.0°*), men det er næppe ønsket. Koden findes (`boot_push_step()`, kun kompileret i SIM/preview) og er testet i SIM og selftest: gennem frame-politikken, udskudt mens en aktivitet kører. At bruge den på enheden kræver to kald i `main()` (start + step) og `OURUI_BOOTPUSH` defineret i enheds-build'et. Det er **ikke** gjort i v6.4, fordi det er ny skrive-adfærd på den rigtige MCU — 30 ord i én blok — som ikke er prøvet på maskinen, og fordi enheds-build'et skulle være uændret i denne runde. Testplan P3 måler først, om problemet er der.
* **`rx_feed` kan gå i stå i ca. 3,5 s (kode-læsning, ikke reproduceret).** Ser parseren `AA 55` fulgt af et længde-felt > 129 i en ikke-special-frame, bryder den ud uden at rykke frem og står der, til 8 kB-bufferen løber over. UI'et ville vise OFFLINE og derefter komme sig selv. Kræver en bit-fejl i netop længde-byten; `crc_garbage` rammer det ikke. Rettelsen er én linje (`if(L>REGN){ i++; continue; }`), men er holdt ude af v6.4, fordi enheds-build'et skulle være uændret.
* **Værtens billeder er ikke bit-identiske med ARM-kode.** Samme kilde oversat til ARMv7 og kørt emuleret i en lokal container giver 56/56 previews, der afviger fra værtens i gennemsnit 72 pixels (0,03 %) med ±1 i én farvekanal — og ét sted et 3×8 px glyf-fragment. Selftesten er grøn på ARM i begge sprog (198 af 199 checks; den sidste kræver vendor-binæren, som ikke ligger i containeren). Usynligt, men previews og SIM-snapshots er altså ikke pixel-eksakte forudsigelser af enheden.
* **UI-profilen bør blive overflødig.** Dens 22 overrides hører hjemme i `gen_scenarios.py`/`ref_client.py` (som `hotwater_mode` allerede er). Forslaget står i kontrakt-tillægget.
* **`steam_auto` er ustabilt uanset klient** (ca. 1 kørsel af 9): sim'ens efter-damp-top kan stå på 650 i et enkelt 10 ms-tick, og så ser ingen klient den (`word48 ≥ 650` fejler med 649). Set én gang i de fulde kørsler; årsag og forslag står i kontrakt-tillægget (E), beviset i `sim/reports/v6.4/flake_steam_auto/`.
* **`sim/play.py`** (test-agentens play-mode, kom til undervejs) kører allerede mod SIM-build'et og bruger den nye `layout`-krog til de rigtige tryk-mål.

**7 · Frame-tid**

Målt som på enheden — kun `ui_frame()` — over alle renderede frames i E2E-kørslerne:

| Kørsel | Frames | Snit | Værste frame |
|---|---|---|---|
| 22 scenarier, engelsk | 10505 | 0.219 ms | 4.16 ms (`settings_save_matrix`) |
| 22 scenarier, dansk | 10495 | 0.221 ms | 5.19 ms (`settings_save_matrix`) |
| egne touch-scenarier, engelsk | 1904 | 0.194 ms | 5.93 ms (`ui_rows_real_frames`) |
| egne touch-scenarier, dansk | 1892 | 0.227 ms | 7.96 ms (`ui_rows_real_frames`) |

**Værts-tal (Apple-silicium), ikke enhedens — og de kan ikke ganges op.** I realtid sover processen 2 ms mellem hver omgang, så hver frame starter med kolde cacher; derfor er snittet her ca. 10× bench-tallet i §8 (0,03 ms, varm løkke), som er det §8's ekstrapolation bygger på. Den værste frame i `settings_save_matrix` er en GEM-frame: `cfg_set_int()` med backup, `fsync` og rename kører inde i `ui_update()` — det gør den også på enheden, hvor flash-skrivningen vil koste mere. Forbeholdet fra §8 står uændret: enhedens frame-tid skal aflæses på enheden (DIAGNOSE, `FRAME MS NU/MAX`), særligt lige efter et GEM. Den emulerede ARM-kørsel (bench under qemu: 1,0 ms pr. bryg-frame, 4,2 ms for en fuld genopbygning) er heller ikke et enheds-tal.

**8 · Ændringer uden for `ourui_v6.c`**
* `sim/run_e2e.py` — additivt: `OURUI_LANGFILE` + `--ui-lang`, UI-profil (`--ui-profile` / `--no-ui-profile`), `--run-name`, `--scen-dir`, sprog/check-tal/frame-tid i rapporten. Reference-kørsler er uændrede.
* `sim/CONTRACT.md` — TILLÆG v6.4 tilføjet nederst; kontrakt-teksten er urørt. Ingen andre filer i `sim/` er ændret; scenarierne er uændrede.
* `round_v6/build_previews.sh` — sammenligner nu alle previews med v6.3 og fejler ved en utilsigtet forskel.
* Nye: `build_sim.sh`, `sim_e2e.py`, `sim_ui_profile.json`, `sim_scenarios/`.
* `boot/` er ikke rørt. `artifacts/config.json`: md5 `<config-md5-redacted>` (uændret).

**Ikke verificeret (kræver enheden):** alt. v6.4 ændrer to tekster på enheden; O1 (splash viser nu *V6.4*) og E (GEM uden ACK viser den nye toast) dækker dem.

---

## CHANGES-v6.5

To rettelser i **enheds-build'et**, begge fra fundene i CHANGES-v6.4 pkt. 6: (1) ourui boot-pusher nu selv config til controlleren — også på enheden — og (2) RX-parseren kan ikke længere gå i stå. Dertil to nye DIAGNOSE-oplysninger. Protokol, frame-politik, keep-awake, gem-sti, aktivitets-recepter, boot-/skift-logik, tekster og tegne-kode er uændrede; SIM-build'ets adfærd er uændret. Toasten *GEM FEJLET · INGEN ACK · INTET GEMT* / *SAVE FAILED · NO ACK · NOT SAVED* fra v6.4 står, som den er.

**1 · Resultat**

| Hvad | Resultat |
|---|---|
| Enheds-koden: selftest | **241 checks, 0 fejl** — engelsk og dansk (v6.4: 199; de 42 nye: 23 boot-push på enheden, 19 RX-resync — pkt. 5) |
| Mutations-prøve: de nye RX-tests mod **v6.4's** parser | **11 af 19 fejler** — fundet er dermed reproduceret, ikke kun læst (pkt. 3) |
| Previews | 57 × 2 sprog re-renderet; **circle-tjek 57/57 + 57/57, 0 violations**; **102 af 112 byte-identiske med v6.4**, de 10 andre er DIAGNOSE + versions-strengen (`v64_compare.txt`); 1 nyt scenarie (`56_diag_bootpush`) |
| Spec-afstemning · config-format · boot-tjek · i18n-tjek | **ALT AFSTEMT** · 5/5 · 11/11 · 6/6 |
| Statisk build | enhed, SIM-ARM og SIM-vært: **0 warnings** (frisk compiler-cache); enheds-binæren reproducerbar; `-Wall -Wextra` = præcis v6.4's advarsler |
| Enheds-build mod v6.4, pr. funktion/data-objekt efter preprocessoren | 1326 stykker uændrede · **14 ændrede · 14 nye · 0 fjernede** — alle på listen i pkt. 7 (`sim_build_check.txt`) |
| `sim/selftest.py` (protokol-niveau) | **107 PASS / 0 FAIL** |
| Boot-push i realtid mod den simulerede MCU (`bootpush.txt`) | **23/23** — "slot B-boot" (MCU på 920/50 → 870/70 på 138 ms), genstart midt i et bryg med og uden vagt |
| RX-stall-repro (test-agentens `sim/rx_stall_repro.py`) | **v6.5: aldrig OFFLINE, RESYNC = 1** · v6.4-binær som baseline: ca. 4 s uden RX (3,98 og 4,01 s i to kørsler), OFFLINE vist |
| Målrettede scenarier (`idle`, `no_ack`, `crc_garbage`, `out_of_spec`, `replay_v3`, `brew`) | **6/6** |
| Hele suiten, rigtigt UI, **engelsk** | **23/23 scenarier · 589/589 checks** · settings_save_matrix 188/188 |
| Hele suiten, rigtigt UI, **dansk** | **23/23 scenarier · 589/589 checks** · settings_save_matrix 188/188 |
| ourui's egne touch-scenarier · SKIFT NU uden dry-run | engelsk **4/4 · 87/87**, dansk **4/4 · 87/87** · **9/9** |

Rapporter: `sim/reports/v6.5/` — start i `SUMMARY.md`.

Suiten har nu 23 scenarier (v6.4: 22; `replay_steam_live` er test-agentens nye). **Den blev kørt to gange:** under den første kørsel rettede test-agenten et tidsvindue i `replay_steam_live`, og den engelske kørsel, som allerede havde kørt scenariet med det gamle vindue, fik 22/23 (vinduet sluttede 9 ms efter et trykspring — en timing-margin i scenariet, ikke UI-adfærd). Tallene ovenfor er fra genkørslen på det endelige scenarie-sæt, hvor ingen fil i `sim/` ændrede sig undervejs. Detaljer: SUMMARY.md, *Kørselsnoter*.

**2 · Boot-push på enheden**

*Hvad.* Ved opstart — både slot B-boot og run.sh — sender ourui de to blokke, stock-UI'ets `fuart_thread` sender, når det starter (PARITY §1.3–1.4):

```
cmd6 sub1   len30   ord 1..30      67 bytes
cmd6 sub112 len3    ord 112..114   13 bytes
```

Koden er v6.4's SIM-boot-push (`boot_push_step()`), nu kompileret i alle build-typer; `OURUI_BOOTPUSH`-flaget er væk, og write-køens blok-grænse er 30 ord overalt (v6.4-enheden: 24).

*Hvorfor.* Ved slot B-boot starter stock-UI'et aldrig. Controllerens registerfil ligger i RAM og får faste startværdier ved hver opstart (REGISTERMAP §2), så uden en boot-push står den med **bryg-setpunkt 92,0 °C og gruppehoved 50 °C, hvor config siger 87,0 og 70** — indtil første GEM eller første start-blok. Det var det åbne punkt i v6.4.

*Ord-layoutet er stock's*, ord for ord:

| Ord | Værdi | Kilde |
|---|---|---|
| 2 · 4 · 11 · 14 · 17 · 23 · 25 | `grindMode` · `autoGrindWeight` · `brewTemper` · `steamGears` · `autoHotWaterTarget` · `autobrewWeight` · `AtuoSteamTarge` | config.json, rå værdier (`model_data_init` 0xcef40) |
| 5 · 13 · 15 | 60 | stock's faste opskrifts-ord (`fuart_thread` 0x146810) |
| 1 | 0 | vågen |
| alle andre i 1..30 — også start-ordene 3/6/9/16/22/24 og 26/27/30 | 0 | stock's spejl er nul her ved opstart |
| 112 | `(preInfusionSwitch ? 0x100 : 0) \| preInfusionPressure` | pakket |
| 113 | `(preInfusionTime << 8) \| breakTime` | pakket |
| 114 | `headTemper` | hele °C |

Med den nuværende config er de to frames på linjen `aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 … 66 03 … 8a 02 … 62 3e` og `aa 55 06 70 03 04 01 05 06 46 00 c7 02`. Selftesten sammenligner begge — og de tilsvarende for en fixture-config med helt andre værdier — med vektorer, der er beregnet uden for programmet med en uafhængig CRC.

*Rækkefølge.* Uændret først: `sub0 = [1]` (PowerOn) og `sub1 = [0]` (vågen). Når linket er oppe, og work_sta er læst: `sub1 len30` → ACK → `sub112 len3` → ACK. I SIM er begge kvitteret 138 ms efter UI-start, altså under splashen.

*Gennem frame-politikken, som alt andet.* Blokkene køes med `wq_push()` uden intent: start-ord og ord 26/27/30 kan ikke være andet end 0, hvert ord skal ligge i sit spec-interval, og hver blok får write-køens ACK-vent med **én** retransmit (150 ms + 300 ms). En config-værdi uden for spec får **hele** `sub1 len30` afvist og talt (BLOK +1) — den sendes aldrig; `sub112 len3` sendes stadig.

*Aktivitets-vagten.* `sub1 len30` skriver start-ordene til 0. Kører der allerede en aktivitet, når ourui starter — run.sh startet midt i et bryg, eller ourui genstartet under ét — kunne blokken afbryde den. Derfor:

* Afgørelsen træffes **én gang**: første gang linket er oppe, **og** ord 33 (work_sta) er læst. Før det sendes intet — heller ikke hvis linket kun er "oppe", fordi controlleren har spurgt om versionen.
* Kører kværn, bryg, varmt vand eller damp i det øjeblik (`phase_active()`: work_sta-bit 6 · bits 1+2 · bits 1+3 · bits 5+14 — de samme mønstre, som spærrer aktivitets-start og SKIFT NU), **springes pushen over**. Den udskydes ikke og gentages ikke af et senere GEM. Controlleren har da de værdier, aktiviteten blev startet med; hvert GEM og hver start-blok skriver fortsat sine egne ord.
* Er maskinen ledig — `0101` (vågen), `0001` (sleep/drain) eller `0000` — sendes pushen.
* En aktivitet, der starter **efter** afgørelsen (mellem de to blokke), **udskyder** den blok, der mangler, til maskinen er ledig.
* Vagten er bevidst bundet til de fire kendte aktivitets-mønstre og ikke til "enhver bit ud over `0101`". En ukendt bit under fx kold opvarmning eller vandpåfyldning ville ellers få pushen sprunget over netop ved den kolde slot B-start, den er til for — og stock-UI'et boot-pusher selv uden at se på work_sta.
* SIM-build'et har som standard **ikke** vagten: det udskyder, som i v6.4 (kontrakt-adfærden er uændret). `OURUI_SIM_BOOTPUSH_GUARD=1` giver SIM-build'et enhedens vagt, så den kan køres mod den simulerede MCU (pkt. 6).

*Udfald* — vises på DIAGNOSE (pkt. 4):

| DIAGNOSE | Betyder | Gentages efter et GEM? |
|---|---|---|
| OK (grøn) | blokken er sendt og kvitteret | nej |
| INGEN ACK (rav) | sendt + én retransmit, ingen kvittering; næste blok er sendt alligevel | ja |
| AFVIST (rav) | frame-politikken afviste blokken: en config-værdi er uden for spec | ja — når config er rettet |
| INGEN CONFIG (rav) | en boot-push-nøgle mangler i config.json; intet sendes (ingen gættede værdier) | ja |
| SPRUNGET OVER | aktivitets-vagten | nej |
| VENTER / SENDER | ikke færdig endnu (intet link, kø optaget, aktivitet i gang) | — |
| FRA | `OURUI_NOBOOTPUSH=1` eller `OURUI_READONLY=1` | — |

*I run.sh-tilstand* har det pausede stock-UI allerede sendt blokkene; ourui sender de samme værdier igen. To ting er værd at vide: ord 113's lo-byte får igen `breakTime` (controlleren overskriver den selv ved næste bryg, §4a), og opskrifts-ord, som stock-UI'et måtte have sat fra et NFC-kort eller et preset (13, 15, 18), går tilbage til boot-værdierne — præcis som en genstart af stock-UI'et ville gøre.

*Slå fra:* `OURUI_NOBOOTPUSH=1` giver v6.4-adfærden. `OURUI_READONLY=1` sender som hidtil ingen cmd6 overhovedet.

**3 · RX-resync-watchdog**

*Fundet.* v6.4's `rx_feed()` blev stående, når en header-kandidat havde et længde-felt over 129 ord, til 8 kB-bufferen løb over. Under arbejdet viste det sig at være lettere at ramme end "en bit-fejl i længde-byten":

* Et løst `AA 55` foran en gyldig frame er nok: `AA 55 AA 55 83 …` læses som kommando `AA` med længde `83` = 131 ord. Samme stall.
* Kommandoerne 3 og 8/0x88 blev sprunget over uden CRC (7 hhv. 15 bytes). `AA 55 03` i støj lige foran en gyldig frame slugte dens første 4 bytes, og framen var tabt.

Begge dele er reproduceret: de nye selftests fejler mod v6.4's parser (11 af 19), og i E2E står en v6.4-SIM-binær ca. 4 s uden RX og viser OFFLINE efter test-agentens injektion af én frame med ødelagt længde-byte — v6.5 viser aldrig OFFLINE.

*Parseren nu.* En kandidat (`AA 55 …`) bliver kun til en frame, når hele framen er modtaget, og CRC'en stemmer. Ellers rykkes én byte frem, og der søges efter næste `AA 55`. Ingen bytes forbruges uden en CRC-dom. Det eneste, der kan holde parseren fast, er en kandidat, hvis længde-felt lover flere bytes, end der er kommet — og den stilling har to værn:

1. **Umulig længde** (over 129 ord): forkastes straks.
2. **Watchdog:** en ufuldstændig kandidat, der har ventet `RX_RESYNC_MS`, forkastes. Resten af bufferen søges igennem, så gyldige frames, der er kommet *bagved*, ikke tabes. Bytes, hvis seneste ankomst er lige så gammel, kan ikke være en frame på vej og forkastes på samme måde. `main()` kalder parseren i hver omgang (hvert 2. ms), så værnet også virker, når der intet kommer.

*Vinduet: `RX_RESYNC_MS` = 400 ms.* Valgt ud fra det, der er målt:

| Måling | Værdi | Kilde | Forhold til 400 ms |
|---|---|---|---|
| Længste gyldige frame på linjen (265 bytes, 57600 8N1) | 46 ms | PROTOCOL §2 | 8,7× under |
| Poll-kadence, stock (ourui poller hvert 50 ms) | ~100–130 ms | UARTLIVE / BREW / HOTWATER / SESSION §2 | ophold *mellem* frames — rører ikke watchdog'en |
| Længste poll-hul | 456 ms (HOTWATER), 451 (UARTLIVE), 318 (BREW), 145 (SESSION) | samme | do. |
| ACK-latens, normalt | 12–25 ms | HOTWATER, BREW §3.2 | — |
| **Længste MCU-tavshed** (første ACK efter en start-write) | **249–279 ms** | BREW §3.2 | 400 ms = 1,4× |
| Write-køens ACK-budget (150 + 300 ms) | 450 ms | §7 | vinduet er **under**: en fastlåst kandidat foran en ACK bliver ikke i sig selv til *GEM FEJLET* |
| OFFLINE-grænsen | 1500 ms | §7 | en resync kan aldrig vise OFFLINE |

Watchdog'en måler kun en kandidat, der venter på resten af sig selv. Mellem frames er bufferen tom, så poll-huller og ACK-ventetid — uanset længde — tæller ikke. Controlleren sender en frame i ét stræk; et ophold *midt* i en frame er ikke set i nogen capture, men et ophold på op til 398 ms tåles (testet), altså også de 279 ms.

*Hvor hurtigt kommer den sig?*

| Tilfælde | Næste gyldige frame modtages |
|---|---|
| Umulig længde (v6.4-fundet), løst `AA 55` | i samme kald — 0 ms |
| For lang, men mulig længde; trafikken fortsætter | når kandidatens byte-antal er kommet, og CRC'en fælder den: 100 ms i testen (to poll-svar senere) |
| Samme, og derefter stilhed | præcis 400 ms efter kandidaten |
| Afkortet frame fulgt af trafik | straks (CRC), eller ved ankomst, hvis stumpen er over 400 ms gammel |
| ACK fanget bag en fastlåst kandidat, ingen anden trafik | 400 ms — før write-køen giver op ved 450 ms; GEM lykkes |
| 4107 bytes tilfældig støj med 20 indlagte "værste" præfikser | 0 falske frames, buffer højst 262 bytes, de fem frames lige efter alle modtaget, den første straks |

*Tællere.* `rx_resync` tæller forkastede kandidater: umulig længde, watchdog (én pr. kald) og buffer-overløb (som nu ikke længere kan nås). En CRC-fejl på en hel frame tælles som før i `rx_bad`. Kommando 3 og 8/0x88 med forkert CRC tæller nu som CRC-fejl; før blev de sprunget over i stilhed. Gyldige frames behandles nøjagtigt som før.

**4 · DIAGNOSE**

* Cellen *RX CRC-FEJL* hedder nu **RX CRC / RESYNC** og viser `CRC-fejl / resync` (rav, når en af dem er over 0). På en sund maskine står der `0 / 0`.
* Ny række nederst i den øverste blok: **BOOT-PUSH 1-30** og **BOOT-PUSH 112-114** med udfaldet fra pkt. 2. Rækken ligger lige under folden; boot-blokken er rykket én række ned. Preview: `56_diag_bootpush`.
* Intet andet på skærmen er ændret: ingen toast, intet på HJEM. Boot-pushen og en resync er usynlige, medmindre man åbner DIAGNOSE.
* 9 nye streng-id'er (Appendiks A); selftestens layout-tjek måler den nye række i alle udfald på begge sprog.

**5 · Selftest: 42 nye checks**

*Boot-push på enheden (23)* — vagten slået til, ACK'er leveret som rigtige `0x86`-frames gennem `rx_feed()`:
* frames på linjen = uafhængige vektorer, for den ægte config og for en fixture-config, hvor alle 12 felter afviger fra både den ægte config og controllerens startværdier;
* uafhængig audit af blokken: ord 26/27/30 = 0, start-ord = 0, ord 5/13/15 = 60, alle øvrige ukendte ord = 0;
* intet sendes uden link eller før work_sta er læst; kun versions-svaret er gået ud;
* ACK → næste blok → færdig; controller-spejlet = config bagefter; pushen sendes én gang (10 s senere: intet nyt);
* ingen retransmit før 150 ms, præcis én ved 150 ms med samme bytes; ingen ACK → INGEN ACK efter 450 ms, og næste blok sendes alligevel;
* **vagten:** work_sta `0141` / `1107` / `1307` / `030B` / `4121` ved opstart → 0 bytes sendt, heller ikke 25 s efter at aktiviteten er slut eller efter et GEM; `0101` / `0001` / `0000` → pushen sendes; aktivitet mellem de to blokke → blok 2 udskydes 30 s og sendes så;
* config uden for spec → `sub1 len30` afvist (BLOK +1, 0 bytes), `sub112 len3` sendt; efter et GEM, der retter værdien, gentages pushen; en gennemført push gentages ikke;
* manglende config-nøgle → intet sendes; read-only og `OURUI_NOBOOTPUSH` → intet sendes; DIAGNOSE-teksterne for hvert udfald.

*RX-resync (19):* korrupt længde-byte (umulig; mulig men for lang, med og uden efterfølgende trafik) · v6.4-scenariet over 3 s poll-trafik (alle 60 svar, største hul 50 ms) · afkortet frame i fem varianter · støj-byge · hvert af 9 "værste" præfikser foran gyldige frames · 40 gyldige svar + 1 ACK flettet med støj, CRC-fejl, umulige og for lange længder, afkortede frames og `AA 55 03` (alle 41 modtaget én gang, i rækkefølge) · ophold *mellem* frames på 100–1400 ms og *midt* i en frame på 20–398 ms (ingen resync) · 450 ms midt i en frame (den frame opgives, den næste modtages) · versions-håndtryk og gyldig kommando 3 · ACK bag en fastlåst kandidat · vinduets tre grænser.

**6 · SIM-build og E2E**

* **SIM-adfærden er uændret:** boot-pushen udskydes under en aktivitet (ingen vagt), og `OURUI_SIM_NOBOOTPUSH=1` slår den fra. Gentagelsen efter et GEM ligger nu i `save_step()` for alle build-typer i stedet for i SIM-krogen — samme betingelse.
* **Nyt i state-JSON'en:** `rx_resync`, og `boot_push.sub1_len30` / `.sub112_len3` kan være `skipped`. **Ny miljø-variabel:** `OURUI_SIM_BOOTPUSH_GUARD=1` (enhedens vagt). Tillæg i [sim/CONTRACT.md](../../sim/CONTRACT.md).
* **Boot-push mod den simulerede MCU** (`sim_e2e.py --only bootpush`, 23/23) — samme main-loop og samme `boot_push_step()` som enheden:
  * *"slot B-boot":* controlleren sættes på sine startværdier (ord 11 = 920, ord 114 = 50 …); UI'et starter med vagten og bringer ord 1..30 + 112..114 på config (870 / 70 …). Skrive-rækkefølgen er `sub0 len1`, `sub1 len1`, `sub1 len30`, `sub112 len3` — og ikke andet.
  * *Genstart midt i et bryg, med vagten:* UI nr. 2 sender kun PowerOn/keep-awake; brygget stopper selv på vægt-målet; pushen sendes heller ikke bagefter.
  * *Samme uden vagten (kontrakt-tilstanden):* pushen sendes, når brygget er slut.
* **RX-stall:** test-agentens `sim/rx_stall_repro.py` køres mod v6.5 og mod en SIM-binær bygget fra `ourui_v6_4.c` (`rx_stall.txt`).
* **UI-profilen er ude af brug:** test-agentens scenarier asserter nu ourui's vogtede adfærd direkte, og runneren kører SIM-build'et med sin egen, tomme standard-profil. `sim_ui_profile.json` fra v6.4 læses ikke længere.

**7 · Hvad der er ændret i enheds-build'et — hele listen**

`build_sim.sh` sammenligner enheds-build'et af v6.4 og v6.5 efter preprocessoren, stykke for stykke (funktion, data-objekt, type), og fejler ved alt, der ikke står her:

| | Stykker |
|---|---|
| **Ændret (14)** | `rx_feed()` · `main()` (start + step af boot-pushen, watchdog-kaldet, `OURUI_NOBOOTPUSH`) · `save_step()` (gentag pushen efter et GEM) · `diag_top_text()`, `draw_diag()`, `scroll_max()`, `DG_LAB` (DIAGNOSE) · `WFrame`, `wq_push_t()`, `wq_tx()` (blok-grænse 24 → 30 ord) · streng-tabellen og dens id'er · `splash_status()`, `OURUI_MARK` (versions-strengen) |
| **Nyt (14)** | `boot_push_words()`, `boot_push_cfg_ok()`, `boot_push_start()`, `boot_push_step()`, `boot_push_resave()` (+ prototype), `bp_failed()`, `bp_text()` · tilstanden `bp`, `bp_on` og de to enums · `rx_resync`, `rx_pend_ms` / `rx_last_ms` |
| **Uændret** | de øvrige 1326 — herunder frame-politikken, write-køens ACK-/retransmit-logik, keep-awake, gem-stien, aktivitets-recepterne, fase-detektionen, boot-/skift-logikken og al tegne-kode |

**8 · Åbne punkter og forbehold**

* **Ord-layoutet er aldrig set på linjen.** Stock's boot-push er fundet ved statisk analyse (PARITY §1.3–1.4) og er ikke fanget i en capture. ourui sender det, analysen siger; testplan K + Q2 sammenligner med stock-UI'ets egen.
* **En 30-ords blok er ny på den rigtige controller set fra ourui** (største hidtil: 18 ord). Stock sender den ved hver opstart, og controllerens write-handler er en `memcpy` med bånd-tjek (`sub + len < 33`), men ACK-latensen for 67 bytes er ikke målt. Bliver den over 450 ms, viser DIAGNOSE *INGEN ACK* — værdierne er da med stor sandsynlighed skrevet alligevel, og `SPEJL 112-114` + HJEM viser det.
* **INGEN ACK giver intet nyt forsøg af sig selv** — først ved næste GEM eller næste start af ourui. Det følger opgavens "ACK-vent + én retransmit". Et begrænset antal automatiske gentagelser, eller en afstemning af spejlet mod config, er en mulig næste runde; det er ikke lavet, fordi det er ny skrive-adfærd, der ikke kan prøves uden maskinen.
* **Genstarter controlleren, mens ourui kører,** står den igen på sine startværdier, og ourui pusher ikke på ny. Stock-UI'et har samme hul. Ikke i denne runde.
* **Vagten kender kun de fire aktiviteter.** Drain, back-flush og selvtest, startet fra stock-UI'et lige før run.sh, er ikke `phase_active()`; pushen ville da skrive ord 8/20/28/29 = 0. ourui's keep-awake (ord 1 = 0) afbryder i forvejen en drain ved opstart, så rækkefølgen "service-rutine i stock → run.sh" var heller ikke sikker før.
* **Build-scripts kunne skjule en fejl.** Både `build_sim.sh` og `build_previews.sh` sendte deres tjek gennem `tee`, og en fejl i et indlejret Python-tjek afbrød blot blokken med exit 0. Begge fejler nu højlydt. (Opdaget, da i18n-appendikset var ude af sync, og scriptet alligevel meldte 0.)
* **Værtens SIM-binær har ikke en stabil md5** (Mach-O; hver linkning giver en ny). Enheds-binæren og ARM-SIM-binæren er reproducerbare.
* **Uden for denne runde, men relevant for en permanent slot B-boot: uret er 32-bit.** `now_ms()` returnerer `long`, som er 32 bit på enheden (målt i den lokale ARMv7-container: `sizeof(long)` = 4, `time_t` = 8). Millisekund-uret løber derfor rundt **24,86 døgn efter opstart**. Tidsforskelle (`now − x`) overlever omløbet, men main-loopets timere sammenlignes absolut (`now >= next_poll`, `next_render`, `next_slow`, `next_ext`, keep-awake): en timer, der er sat til et af de sidste millisekunder før omløbet, og som loopets 2 ms-skridt springer over, bliver først sand igen 49,7 døgn senere — poll eller rendering står da stille, til ourui genstartes, og watchdog'en ser kun på PID'en. Skønnet risiko: nogle få procent pr. timer pr. omløb. Fejlen er ældre end v6.5 (v4-arv) og er ikke rettet her; v6.5's nye kode bruger kun forskelle. Rettelsen er en 64-bit tidsbase gennem hele filen — et forslag til næste runde.

**9 · Ændringer uden for `ourui_v6.c`**
* `build_sim.sh` — sammenligner nu mod v6.4 pr. stykke (pkt. 7); fejler højlydt.
* `round_v6/build_previews.sh` — sammenligner alle previews med v6.4 (`v64_compare.txt`); samler alle tjek op til sidst og fejler, hvis ét af dem gør.
* `sim_e2e.py` — `sim/reports/v6.5/`; nye dele `bootpush`, `rxstall`, `targeted`.
* [sim/CONTRACT.md](../../sim/CONTRACT.md) — TILLÆG v6.5 tilføjet nederst. `sim/scenarios/`, `sim/mcu_sim.py`, `sim/run_e2e.py` og `boot/` er **ikke** rørt.
* `ourui_v6_4.c` / `ourui_v6_4.bin` — v6.4 bevaret uændret. `run.sh` (md5 `3d799859…`) og `artifacts/config.json` (md5 `<config-md5-redacted>`) er uændrede.

**Ikke verificeret (kræver enheden):** alt — testplan Q.

## CHANGES-v6.6

Tre ting: (1) fase-semantikken følger nu det, live-sessionen i `STEAMLIVE.md` har bevist — to damp-veje og samtidige faser; (2) uret er 64 bit — til og med v6.5 løb det rundt efter 24,86 døgn på enheden; (3) genstarter controlleren, mens ourui kører, sendes boot-pushen igen. Protokol, frame-politik, write-kø, keep-awake-kadence, gem-sti, aktivitets-recepter, boot-/skift-logik og config-format er uændrede. State-kontraktens felt `phase` er uændret.

**1 · Resultat**

| Hvad | Resultat |
|---|---|
| Enheds-koden: selftest | **304 checks, 0 fejl** — engelsk og dansk (v6.5: 241; de 63 nye: 29 fase-semantik, 18 ur-omløb, 16 re-push — pkt. 5) |
| Samme selftest som **ægte 32-bit ARM-binær** (lokal ARMv7-container, `long` = 32 bit som på enheden) | **303 checks, 0 fejl** på engelsk og dansk (ét check kræver stock-binæren ved siden af config og springes over i containeren). Samme kilde med v6.5's tidstype (`long`): **10 af de 18 omløbs-checks fejler** (`selftest_arm32.txt`) |
| Mutations-prøver: de nye tests mod v6.5's adfærd | 32-bit-ur: **10 af 18** fejler · damp først på HJEM: **6 af 29** · al damp som latchet: **13 af 29** · ingen re-push: **11 af 16** — og 0 fejl i andre afsnit (`mutation_check.txt`) |
| Previews | **62 × 2 sprog** re-renderet (57 fra v6.5 + 5 nye); **circle-tjek 62/62 + 62/62, 0 violations**; **102 af 114 byte-identiske med v6.5** — de 12 andre er versions-strengen (4 billeder pr. sprog) og de to damp-tekster (2 pr. sprog) (`v65_compare.txt`) |
| Spec-afstemning · config-format · boot-tjek · i18n-tjek | **ALT AFSTEMT** · 5/5 · 12/12 · 6/6 |
| Statisk build | enhed, SIM-ARM og SIM-vært: **0 warnings** (frisk compiler-cache); enheds-binæren reproducerbar; `-Wall -Wextra` = præcis v6.5's advarsler |
| Enheds-build mod v6.5, pr. funktion/data-objekt efter preprocessoren | 1354 stykker i v6.5: **1274 uændrede · 52 hvor eneste forskel er tidstypen (`long` → `ms_t`) · 28 ændrede · 31 nye · 0 fjernede** — alle på listen i pkt. 7. Frame-politik, write-køens push/ACK, aktivitets-recepter, boot-push-ordene og CRC er token-identiske (`sim_build_check.txt`, 21 PASS) |
| `sim/selftest.py` (protokol-niveau; test-agentens) | **120 PASS / 0 FAIL** (v6.5: 107 — test-agenten har udvidet den undervejs) |
| Hele suiten, rigtigt UI, **engelsk** | **25/25 scenarier · 644/644 checks** · settings_save_matrix 188/188 (v6.5: 23 scenarier; nye hos test-agenten: `steam_wand_flush`, `steam_auto_overshoot`) |
| Hele suiten, rigtigt UI, **dansk** | **25/25 scenarier · 644/644 checks** · settings_save_matrix 188/188 |
| Fase-semantik, linje for linje (`phases.txt`) | **20/20** — LIVE-optagelsen i replay (4×) + sim'ens egen flush under bryg og overshoot, på begge sprog |
| Ur-omløb i realtid (`wrap.txt`) | **15/15** — 2^31 og 2^32 ms; 32-bit-baseline: 227 keep-awake-writes på 6 s, FÆRDIG-chip uden aktivitet, ingen state-linjer efter omløbet |
| MCU-genstart → re-push (`repush.txt`) | **14/14** — controlleren tilbage på config 0,40 s efter genstarten; v6.5-baseline: bliver på 920 / 50 |
| Boot-push (v6.5-testen) · RX-stall-repro · SKIFT NU uden dry-run | **23/23** · v6.6 aldrig OFFLINE, RESYNC = 1 (v6.4-baseline: 4,0 s uden RX) · **9/9** |
| ourui's egne scenarier (4 touch + det nye replay-scenarie) | engelsk **5/5 · 110/110**, dansk **5/5 · 110/110** |
| Målrettede scenarier (`idle`, `brew`, `grind`, `hotwater`, `steam_auto`, `steam_manual_stop`, `replay_steam_live`, `no_ack`, `out_of_spec`, `offline`, `sleep_wake`) | **11/11** |

Rapporter: `sim/reports/v6.6/` — start i `SUMMARY.md`.

**Suiten blev kørt to gange.** Test-agenten lagde sim'ens damp-model om til STEAMLIVE, mens denne runde blev verificeret. Første fulde kørsel (19:05–19:36) ramte det: `sim/` skiftede under 7 af 12 dele, sim'ens egen selftest stod på 105 / 2, og engelsk og dansk gav 20/23 (581/589) — `brew`, `steam_auto` og `steam_manual_stop`, alle på sim'ens timing eller damp-model. De samme tre bestod enkeltvis få minutter senere på et `sim/`, der stod stille. Tallene ovenfor er fra genkørslen (19:38–20:12) på det endelige sæt; under den skiftede `sim/` én gang, under den engelske suite, som alligevel gav 25/25. `sim_e2e.py` noterer nu selv et fingeraftryk af `sim/` før og efter hver del. Detaljer: SUMMARY.md, *Kørselsnoter*; første kørsel er gemt i `race_run1/`.

**2 · Fase-semantik efter STEAMLIVE**

*Hvad live-sessionen viste.* Damp er work_sta bits 5+14 (`0x4020`) — men der er **to veje** dertil, med hver sin stop-regel, og damp kan køre **samtidig** med et bryg:

| | LATCHET auto-damp | MOMENTAN wand / flush |
|---|---|---|
| Start | UI-knap: `cmd6 sub24 len8`, ord 24 = 1 | den fysiske wand-kontakt; **ingen UART-frame** |
| Kendetegn | ord 24 = 1, mens bits 5+14 er sat; ord 44 bit0 = 0, ord 46 = 0 | bits 5+14 **uden** ord 24 = 1; ord 44 bit0 = 1 og ord 46 = 1 følger ca. 0,4 s efter |
| Stop | **MCU'en selv**, når mælken (ord 48) når målet (ord 25): rydder bits'ene og derefter ord 24. Ingen stop-write | når wand'en slippes. **Ingen auto-stop** — heller ikke over målet (proben nåede 69,5 °C med mål 65,0 °C) |
| Efter stop | proben stiger ca. 3,9 °C videre på ~11 s (termisk inerti) | — |

*Hvad ourui viser nu.*

| Tilstand | Chip | Label / under-linje | Gauge | Knap-række |
|---|---|---|---|---|
| Latchet damp | **DAMP·AUTO** | *MÆLK · MÅL 65.0°* / *AUTO-STOP VED 65.0° · ELLER STOP* — målet er **ord 25** (controllerens eget), ikke config | mælk m. mål-mærke | **STOP DAMP** |
| Wand / flush | **DAMP·WAND** | *MÆLKEPROBE · INTET MÅL* / *INGEN AUTO-STOP · SLIP WAND* | mælk, **uden** mål-mærke; ingen mål-linje i grafen | **ingen STOP-knap** — de fire knapper, DAMP markeret som aktiv |
| Bryg + damp samtidig (`5327`) | **EKSTRAKTION** + mærket **DAMP·WAND** på label-linjen | *AUTO-STOP VED 36.0 G* (bryggets) | bryggets tryk + fremskridts-ring; **vægten** i hero | BRYG **og** DAMP markeret som aktive |
| … stat-rækken | | *TID · ≈BAR\* · **MÆLK*** — mælke-temperaturen står, hvor FLOW ellers står | | |
| Efter auto-stop | FÆRDIG | *FÆRDIG PÅ 41.3 S · STIGER ≈4° TIL* (≈7° i °F) — hero følger proben videre | | |
| Efter wand | FÆRDIG | *FÆRDIG PÅ 3.1 S* | | |
| Damp bestilt, endnu ikke i gang | KLAR | *STARTER DAMP…* (før: *DAMP ARMERET · ÅBN DAMPHANEN*) | | |

Previews: `07` (latchet) · `57` (wand) · `58` (wand over målet) · `59` (bryg + damp) · `60` (efter auto-stop).

*Sådan afgøres det* (`steam_track()`, kaldt én gang pr. frame):

1. Er bits 5+14 sat, og er ord 24 ≠ 0: **AUTO**. Klassen er **klæbende**, til bits'ene ryddes. Grunden: efter en manuel STOP står ord 24 = 0 i spejlet med det samme (ACK-echoet), mens MCU'en først rydder bits'ene et øjeblik senere — uden klæbningen ville chippen blinke *DAMP·WAND*.
2. Er bits 5+14 sat, og er ord 24 = 0 (og ikke allerede AUTO): **WAND**. Det gælder fra første svar; ord 44 bit0 / ord 46 kommer ca. 0,4 s senere og noteres som bekræftelse (`steam.wand_seen` i state-JSON'en), men klassen venter ikke på dem — MCU'en har kun de to start-veje.
3. Er ord 24 endnu ikke læst (kun et `sub33`-svar er nået frem): **ukendt** — chippen er det neutrale *DAMP*, under-linjen er tom, og STOP-knappen vises. STOP er altid en sikker frame; en manglende STOP-knap under latchet damp er det ikke.

*Hoved-aktivitet og vagter.* `phase_raw()` er uændret (damp først) og er fortsat det, state-kontraktens `phase` og **alle vagter** bruger: enhver aktivitet — også en wand-flush — spærrer start, GEM af MCU-værdier, SKIFT NU og boot-push. Nyt er `phase_main()`: kværn / bryg / vand går forud for damp. Den styrer chip, gauge, hero og tids-felt på HJEM. Bryg-uret løber uforstyrret, mens dampen kommer og går; slutter brygget, mens wand'en stadig er åben, skifter HJEM til *DAMP·WAND* uden en FÆRDIG-visning imellem, og TID viser da dampens tid.

*Det, der ikke er ændret:* STOP-writen (`sub24 len8`, ord 24 = 0), start-blokken, 15 s-grænsen for *INGEN START*, og ctrl-krogen `action steam_stop` (den sender som før).

*Overstyret tekst.* *DAMP ARMERET · ÅBN DAMPHANEN* byggede på den statiske læsning, at display-damp kræver wand-kontakten. Live startede dampen 0,22 s efter writen med ord 44 bit0 = 0. Teksten er derfor *STARTER DAMP…*, som for de tre andre aktiviteter; streng-id'et `TX_H_STEAM_ARMED` er fjernet.

**3 · 64-bit-ur**

*Fejlen.* `now_ms()` returnerede `long`. På enheden er `long` 32 bit, så millisekund-uret løb rundt 2^31 ms = **24,86 døgn** efter opstart. CHANGES-v6.5 pkt. 8 beskrev følgen for main-loopets timere. Gennemgangen til denne runde viste, at det var større end det:

| Hvad | Med 32-bit-uret | Set i |
|---|---|---|
| Poll-, slow-, ext- og render-timeren (`now >= next_…`) | en frist i urets sidste millisekunder, som et langt loop-skridt sprang over, blev først sand igen 49,7 døgn senere: **ingen poll / frossen skærm**, og watchdog'en ser kun på PID'en | selftest (mutant): 60 + 15 + 3 queries på 8 s i stedet for 160 + 40 + 8 — intet efter omløbet |
| Keep-awake (`now >= ka_next`) | de sidste 30 s før omløbet lå fristen "i fortiden": **en keep-awake-write i hver ledig loop-omgang** | E2E (32-bit-baseline): 227 writes på 6 s, hvor der skulle være 1 |
| Stepper-gentagelsen (`now >= rep_next`) | hold på − / + hen over omløbet gik i stå | selftest: 400 i stedet for 3200 |
| Alle forskelle over 24,86 døgn (`now − x < grænse`) blev **negative** | et link, der havde været tavst så længe, så "oppe" ud; FÆRDIG-chippen kom igen 24,86 døgn efter seneste aktivitet og blev stående; CRC-advarslen og den seneste toast ligeså | selftest + E2E (chippen stod på FÆRDIG fra start) |
| `/proc/<pid>/stat` felt 22 læst med `atol` | mættet ved 2^31 ticks = **248 døgns oppetid**: exit-påmindelsen ("stock-UI'et har gamle værdier") ville altid — eller aldrig — være sand | selftest på 32-bit ARM |
| RX-tællerne (`long`, `++`) | signed overflow efter ca. 2,6 års uafbrudt drift | selftest |
| Puls-animationen `sinf(now/260.f)` | en `float` har 24 bit: efter 4,7 timer var opløsningen 2 ms, efter 24 døgn 256 ms — ringen om den aktive knap hakkede | gennemgang |

*Rettelsen.* Én type, `ms_t` = `int64_t`, for **alle** tidsværdier — tidspunkter, frister og forskelle — i alle build-typer. Uret løber ikke rundt (292 mio. år). `now_ms()` danner summen i 64 bit, før den tildeles (`ts_ms()`).

| Område | Nu `ms_t` |
|---|---|
| Ur | `now_ms()`, `ts_ms()` |
| Main-loop | de seks timere er flyttet fra lokale variable i `main()` til `lt` og drives af `loop_poll()`, `loop_graph()`, `loop_render_due()`, `loop_rendered()` — så selftesten kører **samme kode** som enheden |
| Protokol | keep-awake-fristen (`ka_next`), write-køens ACK-ur (`wq.sent_ms`: 150 + 300 ms), RX-resync-vinduet (`rx_pend_ms`, `rx_last_ms`: 400 ms), `last_rx_ms` (OFFLINE efter 1,5 s), `last_bad_ms` |
| Touch | hold-for-start (`tch.t0`: 650 ms), stepper-gentagelse m. acceleration (`tch.rep_next`) |
| UI | toast, ripple, side-overgang (`tr.t0`), splash, fase-ure (`act.t_act0`, `t_done`, `done_ms`, `t_req`, `t_col`), boot-poll (`bs.t`), graf-tidsstempler (`g_t[]`), frame-tids-statistikken |
| Boot | sekunder siden boot (`mono_s()`, stemplet i `RESTART_NEEDED`), proces-starttid (`proc_stat()`, nu `atoll`), ventetiden i runtime-skiftet |
| SIM | `t_ms`, touch-køens forfaldstider, fb-/state-kadence, krog-frister |

Dertil: RX-tællerne er `uint32_t` (løber veldefineret rundt), poll-skifteren er `unsigned`, og puls-fasen tages af urets nederste 24 bit (billedet er bit-identisk med v6.5 de første 4,7 timer — og derefter lige så jævnt). I kilden er der ingen `long` tilbage som tidstype; `build_sim.sh` tjekker det.

*Hvad er ikke rørt:* tids-**værdierne** (alle grænser og kadencer er de samme tal), og de 52 funktioner, hvor eneste forskel er parameterens type.

*Prøvet på tre måder.*
1. **Selftest (18 checks)** med et mock-ur hen over 2^31 ms og 2^32 ms: main-loopet med ét 40 ms-skridt, der springer over alle tre poll-frister; render-timeren; keep-awake (præcis 2 writes på 65 s, ved B + 10 000 og B + 40 000 ms); ACK-budgettet (retransmit ved præcis +150 ms, opgivet +300 ms senere); resync-vinduet (præcis 400 ms); hold-for-start (intet ved 649 ms, start ved 650 ms); stepper-gentagelsen (samme værdi som langt fra grænsen); toast; side-overgang; frame-tids-statistik; forskelle på 24,86 døgn; 2^45 ms; `/proc`-starttid over 2^31 ticks; tællernes omløb.
2. **Som ægte 32-bit binær.** Selftesten er cross-kompileret statisk til ARMv7 og kørt i den lokale ARMv7-container (`getconf LONG_BIT` = 32): grøn på begge sprog. Samme kilde med `typedef long ms_t` fejler 10 af omløbs-checkene på samme platform — og 0 på værten, hvor `long` er 64 bit. Det er præcis grunden til, at fejlen ikke kunne ses i nogen tidligere lokal kørsel.
3. **Det rigtige main-loop i realtid** (`sim_e2e.py --only wrap`): SIM-build'et startes med uret 6 s før 2^31 hhv. 2^32 ms (`OURUI_SIM_UPTIME_MS`, findes kun i SIM-build'et) og kører 44 s mod den simulerede MCU — resultater i pkt. 6.

**4 · Re-push efter MCU-genstart**

*Hullet.* Controllerens registerfil ligger i RAM. Genstarter MCU'en, mens ourui kører, står den igen på sine startværdier (bryg 92,0 °C, gruppe 50 °C, pre-infusion nulstillet), og v6.5 pushede ikke igen. Stock-UI'et har samme hul.

*To tegn på en genstart:*

**(a) Versions-forespørgslen kommer igen.** MCU'en sender `cmd 0x89` hvert 20. ms fra sin opstart, til den har fået svar — og kun dér (0 stk. i 12 minutters live-trafik). ourui svarer som hidtil på hver. En `0x89`, der ankommer mere end 200 ms efter, at boot-pushen forlod VENTER, er en MCU-opstart *efter* vores push. Det første håndtryk ligger før pushen og tæller ikke; en efternøler fra det ligger inden for de 200 ms og tæller heller ikke. Når runden har været stille i 300 ms, og en evt. igangværende push er færdig, startes boot-pushen forfra.

**(b) Startværdi-signaturen: ord 112 = 0 og ord 113 = 0.** Det er MCU'ens startværdier, og de kan aldrig være config-værdier inden for spec (ord 112's lo-byte er pre-inf.-trykket 3..4; ord 113's hi-byte er pre-inf.-tiden ≥ 5). Signaturen siger derfor entydigt, at ord 112..114 ikke er skrevet siden MCU'ens opstart. Den skal stå i **3 friske læsninger i træk** (poll hvert 1 s), mens maskinen er ledig, linket er oppe, write-køen er tom, og config er indlæst og i spec. Den fanger en genstart, hvor `0x89`-runden blev tabt — og en boot-push, som vagten sprang over, mens controlleren faktisk stod på startværdierne (slot B-boot med wand'en åben).

*Re-pushen er boot-pushen.* Samme to blokke (`sub1 len30` + `sub112 len3`), samme ord-layout, samme port (`wq_push()` uden intent → frame-politikken), samme ACK-vent med én retransmit — og **samme aktivitets-vagt**, afgjort på en work_sta, der er læst *efter* genstarten. Kører en aktivitet i det øjeblik, springes den over; står signaturen der bagefter, tager (b) den, når maskinen er ledig. I SIM-build'et uden `OURUI_SIM_BOOTPUSH_GUARD` udskydes den, som boot-pushen.

*Begrænsninger* — så en controller, der ikke tager imod, ikke giver en strøm af writes:

| Regel | Værdi |
|---|---|
| Afstand mellem to re-push | mindst 5 s |
| Signatur-udløste re-push | højst 3 pr. 10 min. (glidende) |
| Config uden for spec / mangler | ingen signatur-udløst re-push (blokken ville blive afvist igen) |
| `OURUI_NOBOOTPUSH=1`, `OURUI_READONLY=1` | ingen re-push |

*DIAGNOSE.* Efter en re-push hedder boot-push-rækken **RE-PUSH 2 · 1-30** (accent-farve) og **112-114**, hvor tallet er antallet af re-push siden ourui startede; cellerne viser udfaldet af den seneste push (OK, INGEN ACK, AFVIST, SPRUNGET OVER …). Uden re-push er rækken uændret *BOOT-PUSH 1-30* / *BOOT-PUSH 112-114*. Preview: `61_diag_repush`. Intet andet på skærmen: ingen toast, intet på HJEM.

*Følge for et åbent punkt i v6.5:* en boot-push, der endte i INGEN ACK, og hvor ord 112..113 faktisk ikke blev skrevet, får nu op til 3 nye forsøg af sig selv gennem (b). Blev blokkene skrevet, og var det kun ACK'en, der udeblev, sker der intet — som før.

**5 · Selftest: 63 nye checks**

*Fase-semantik (29)* — alle gennem `rx_feed()` med rigtige `0x83`-frames:
* latchet: chip, mål fra **ord 25** (fixture: 60,0° mod config'ens 65,0°), auto-stop-tekst, gauge-mærke, STOP-knappen vises og rammes; vedligehold-pulser (`6121` / `4521`) ændrer intet; klæbning efter STOP-ACK;
* auto-stop **række for række fra STEAMLIVE §5.1** (`6121` → `2101` m. ord 24 = 1 → `0501` m. ord 24 = 0): FÆRDIG, STOP-knappen væk, **intet sendt**; efterstignings-noten; hero følger proben 65,2 → 68,9;
* wand alene: *DAMP·WAND* fra første svar, bekræftet af ord 44/46 senere; ingen mål-tekst, intet mål-mærke, ingen STOP-knap, start spærret;
* **wand over målet:** proben 56,0 → 69,5° med mål 65,0° over 4 s — hele vejen *DAMP·WAND*, aldrig FÆRDIG, og et tryk dér, hvor STOP-knappen ville være, sender ingen frame;
* samtidig **række for række fra STEAMLIVE §6.2** (`1307` → `5327` → wand til → 66,9° → 69,5° → `1307`): `phase_raw()` = STEAM, hoved-fasen = EKSTRAKTION, chip + mærke, vægt i hero, mælk i stat-rækken, bryg-uret uforstyrret; brygget slutter med FÆRDIG for brygget og hele bryg-tiden;
* varianter: bryg + latchet damp (STOP vises), ord 24 ulæst, kværn + wand (grafen tager fortsat kværn-vægten), brygget slutter med wand'en åben, linket falder ud under damp, *STARTER DAMP…*, mærkets bredde på begge sprog.

*Ur-omløb (18)* — pkt. 3.

*Re-push (16)* — vagten slået til, en "MCU" der svarer status hvert 50. ms, ord 112..114 hvert 1. s og ACK'er efter 20 ms:
* det første håndtryk (3 × `0x89` før pushen + en efternøler) giver ingen re-push; alle 4 er besvaret;
* genstart midt i sessionen (5 × `0x89`): intet sendes, før runden har været stille i 300 ms og en frisk work_sta er læst; derefter `sub1 len30` = den uafhængige vektor fra v6.5 og `sub112 len3` = `… 04 01 05 06 46 00 c7 02`; controlleren tilbage på config; DIAGNOSE-rækken;
* to genstarter tæt på hinanden: præcis 5000 ms mellem de to re-push;
* **vagten:** genstart med work_sta `4121` → sprunget over, 0 bytes; signaturen udløser intet, så længe aktiviteten kører; når maskinen er ledig, sendes pushen (årsag: startværdier);
* genstart **uden** `0x89`: signaturen udløser efter 3 friske læsninger (ca. 4 s); 2 + 1 (config) + 2 læsninger udløser intet;
* budget: en controller, der ACK'er, men ikke beholder værdierne: 3 re-push, derefter stille; 10 min. efter den første gives der én ny;
* genstart midt i en push (blok A ACK'et, blok B uden ACK, `0x89`): pushen gøres færdig, og begge blokke sendes forfra;
* uden vagten (SIM-kontrakten): udskudt og sendt, når maskinen er ledig;
* ingen re-push: config uden for spec, read-only, `OURUI_NOBOOTPUSH` — `0x89` besvares stadig.

*Mutations-prøverne* køres af `build_previews.sh` ved hver kørsel og fejler, hvis en mutant slipper igennem.

**6 · SIM-build og E2E**

* **SIM-adfærden er uændret** for alt, test-agentens scenarier måler: `phase`, `work_sta`, `regs`, toasts, writes.
* **Nyt i state-JSON'en:** `phase_main`, `phases` (alle aktive, hoved-aktiviteten først), `steam{on, kind, last_kind, concurrent, wand_sw, wand_seen, ord24, target, milk, stop_btn}`, `shown.badge`, `repush{count, ver, defaults, last, armed, ver_reqs, defaults_sig}`; `regs` har også ord 44 og 46. `shown.chip` er *STEAM·AUTO* / *STEAM·WAND* under damp.
* **Ny miljø-variabel, kun SIM:** `OURUI_SIM_UPTIME_MS=<n>` — uret starter på n ms.
* **Ny adfærd, synlig for sim'en:** efter `reboot` (og efter `link closed` → `up`, som også nulstiller versions-håndtrykket) sender ourui boot-pushen igen. Ingen af de 25 scenarier gør nogen af delene.
* **Nyt scenarie hos ourui:** `sim_scenarios/ui_replay_steam_live_x4.json` — LIVE-optagelsen afspillet 4×. `sim_e2e.py --only phases` gennemgår derefter **hver state-linje** mod de nye felter — også i test-agentens `steam_wand_flush` og `steam_auto_overshoot`, når de findes i suiten. Tillæg i [sim/CONTRACT.md](../../sim/CONTRACT.md).
* **Fase-semantik mod LIVE-optagelsen** (`--only phases`, 20/20): i replayet har hver af de 182 (EN) / 191 (DA) linjer med latchet damp `steam.kind` = auto, chippen *STEAM·AUTO* / *DAMP·AUTO* og STOP-knap; efter auto-stoppet står noten *RISES ≈4° MORE*, og proben går fra 64,6° til 68,9°. Hver linje med work_sta `5327` har `kind` = wand, `phase` = STEAM (kontrakten), `phase_main` = EXT, chip + mærke, vægten i hero (21,6 → 27,9 g) og mælken i 3. stat-felt (51,4° → 66,7°); proben når 69,5° uden STOP-knap og uden FÆRDIG. Ingen linje er klassificeret forkert, og ourui sendte 0 stop-writes.
* **Samme felter mod sim'ens egen model** (test-agentens to nye scenarier, kørt i suiten): `steam_wand_flush` — 141 linjer med `5327`, heraf 48 med proben over målet, alle som ovenfor; efter slip fortsætter EKSTRAKTION uden FÆRDIG. `steam_auto_overshoot` — 663 linjer latchet hele vejen, FÆRDIG med noten, proben 64,9° ved stop og 68,8° bagefter.
* **Ur-omløb** (`--only wrap`, 15/15): med uret startet 6 s før grænsen og 44 s i realtid: state-linjer hele vejen (største hul 171 ms), RX 25,6 svar/s før og efter, præcis 2 keep-awake-writes (0,06 s og 30,03 s), et hold-for-start, der begynder før og fuldføres efter omløbet, starter brygget, som stopper selv på sit vægt-mål; toasten fra før omløbet forsvinder til tiden. **Baseline** (samme kilde med `typedef int32_t ms_t`): chippen står på FÆRDIG fra start, 227 keep-awake-writes på de første 6 s, RX 130/s (poll-strøm), og ingen state-linjer efter omløbet.
* **Re-push** (`--only repush`, 14/14): efter sim'ens `reboot` + startværdier svarer ourui på `0x89`, og 0,40 s efter genstarten står ord 11 / 114 / 112 igen på 870 / 70 / `0104`; skrivningerne er præcis `sub1 len30` + `sub112 len3`; sim'en melder ikke `boot_push_missing`. Nulstilles registerfilen uden `0x89`, udløser signaturen re-pushen efter 4,1 s. **Baseline** (SIM-binær bygget fra `ourui_v6_5.c`): ingen writes, controlleren bliver på 920 / 50, og sim'en melder `boot_push_missing`.
* **A/B-trin** (`--only ab`): scenarier, der fejler i suiten, køres igen mod v6.6 og mod den uændrede v6.5-binær på samme sim. I genkørslen var der intet at sammenligne.

**7 · Hvad der er ændret i enheds-build'et — hele listen**

`build_sim.sh` sammenligner enheds-build'et af v6.5 og v6.6 efter preprocessoren, stykke for stykke, og fejler ved alt, der ikke står her. Stykker, hvor eneste forskel er `long` → `ms_t`, tælles for sig.

| | Stykker |
|---|---|
| **Ændret (28)** | *Ur:* `now_ms()`, `mono_s()`, `mono_fake`, `proc_stat()`, `restart_pending()`, `cfg_set_int()` (stemplets format), `act` (felt-typer), `rx_ok`, `rx_resync`, `main()` (timerne via `loop_*()`, kaldet til `repush_step()`) · *Faser:* `ui_update()`, `home_model()`, `HomeM`, `STAT_SET` + dens enum, `draw_home()`, `draw_chart()`, `hit_test()` · *Re-push:* `rx_feed()` (tæller `0x89` og læsninger af ord 112..113), `bp`, `boot_push_start()`, `boot_push_step()` (tidspunktet, pushen forlod VENTER), `diag_top_text()`, `draw_diag()` · streng-tabellen og dens id'er · `splash_status()`, `OURUI_MARK` (versions-strengen) |
| **Nyt (31)** | *Ur:* `ms_t`, `ts_ms()`, `lt`, `loop_poll()`, `loop_graph()`, `loop_render_due()`, `loop_rendered()` + en `_Static_assert` · *Faser:* `stm` + enum, `steam_bits()`, `wand_sw()`, `steam_track()`, `steam_stop_visible()`, `steam_target()` (+ prototype), `phase_main()`, `steam_label()` + en `_Static_assert` · *Re-push:* `rps` + enum, `bp_guard_cfg`, `ver_n`, `ver_ms`, `repush_count()` (+ prototype), `repush_cfg_sane()`, `mcu_defaults_sig()`, `repush_begin()`, `repush_step()`, `diag_label()` |
| **Kun tidstypen (52)** | bl.a. `wq_step()`, `wq_tx()`, `proto_keepawake()`, `link_up()`, `phase_raw()`, `act_guard()`, `act_request()`, `save_begin()`, `save_step()`, `ui_touch()`, `ui_frame()`, `do_switch()`, `switch_launch()` — hele listen står i `sim_build_check.txt` |
| **Uændret (1274)** | herunder `frame_policy()`, `word_ok()`, `wq_push_t()`, `wq_push()`, `wq_on_ack()`, `wq_pop()`, `act_push()`, `act_push_steam_stop()`, `setting_mcu_push()`, `boot_push_words()`, `boot_push_resave()`, `build_write()`, `frame_query()`, `frame_version()`, `crc16()`, `boot_flag_set()`, `settings_load()` og al øvrig tegne-kode |

**8 · Åbne punkter og forbehold**

* **Intet af det er kørt på enheden** — testplan R.
* **Ur-omløbet kan ikke prøves på enheden på rimelig tid** (24,86 døgn). Beviset er de tre lokale prøver i pkt. 3, heraf én på 32-bit ARM. `OURUI_SIM_UPTIME_MS` findes bevidst ikke i enheds-binæren.
* **`0x89` er kun kendt som opstarts-forespørgsel.** Kommer den også i andre situationer (det er ikke set i nogen capture), giver hver runde én re-push: de samme værdier skrevet igen, med vagten. Tælleren på DIAGNOSE viser det — R7 beder om at aflæse den efter en lang session.
* **En MCU-genstart kan ikke fremkaldes sikkert på maskinen.** ourui sender aldrig ord 30 (reboot), og MCU'en deler strøm med displayet. (a) og (b) er derfor kun prøvet mod den simulerede MCU og i selftesten.
* **Signaturen ser kun ord 112..113.** En genstart, hvor (a) blev tabt, **og** hvor blok B alligevel blev skrevet bagefter (fx af en bryg-start, som synker ord 112..114), men blok A ikke, opdages ikke: ord 11 kunne stå på 92,0 °C. Bryg-starten sender selv ord 11 i sin blok, så et bryg får den rigtige temperatur; kedlen ville blot holde 92,0 °C imellem. En fuld afstemning af spejlet mod config er ikke lavet.
* **Wand-flush spærrer start fra ourui.** Mens bits 5+14 er sat, svarer start-knapperne *AKTIVITET I GANG*. I live-sessionen blev brygget startet først og wand'en åbnet bagefter; den omvendte rækkefølge er ikke prøvet på maskinen, og ourui tillader den ikke.
* **Ord 24 = 1 uden at dampen starter.** Giver controlleren ikke bits 5+14 inden 15 s, viser ourui *INGEN START · TJEK MASKINEN* — men lader ord 24 stå. Om MCU'en da kan starte dampen senere, er ukendt. En STOP-write ved timeout ville lukke det; den er ikke lavet, fordi det er ny skrive-adfærd uden for denne runde.
* **Efterstigningen (≈4°) er ét målt tal** fra én session (kold mælk 12 → 65 °C). Den er en oplysning i under-linjen, ikke en beregning.
* **Værtens SIM-binær har fortsat ikke en stabil md5** (Mach-O). Enheds-binæren og ARM-SIM-binæren er reproducerbare.
* **Ny ground-truth fra enheden, kommet til under denne runde** (boot-agentens `boot/ondevice/GROUND-TRUTH.md`, læst — ikke rørt): stock-UI'ets egen boot-push er nu fanget på linjen. `sub1 len30` er **byte-identisk** med ourui's (67 bytes, `… 62 3e`) — det åbne punkt fra CHANGES-v6.5 pkt. 8 er dermed lukket for den blok. `sub112 len3` afviger i **én byte**: stock sender ord 113 = `0600` (lo-byten 0), ourui sender `0605` (lo-byten = `breakTime` 5): `… 04 01 00 06 46 00 c7 ce` mod ourui's `… 04 01 05 06 46 00 c7 02`. Lo-byten overskriver controlleren selv ved næste bryg (§4a), og den har ingen kendt virkning. **Ikke ændret i v6.6:** fundet ligger uden for rundens tre punkter og kom efter, at verifikationen var kørt; rettelsen er én linje i `boot_push_words()` (`pack113(tid, 0)`) plus de to test-vektorer, og den gælder også re-pushen. Testplan Q2/Q3's forventede frame er ourui's, ikke stock's. Samme capture viser, at stock ikke sender en separat PowerOn / keep-awake før blokkene; ourui gør (uændret siden v6).

**9 · Ændringer uden for `ourui_v6.c`**
* `build_sim.sh` — sammenligner mod v6.5; tidstype-forskelle for sig; tjek af, at de sikkerheds-kritiske stykker er token-identiske; selftesten som 32-bit ARM-binær.
* `round_v6/build_previews.sh` — sammenligner alle previews med v6.5 (`v65_compare.txt`); mutations-prøver (`mutation_check.txt`).
* `sim_e2e.py` — `sim/reports/v6.6/`; nye dele `phases`, `wrap`, `repush`; noterer for hver del, om test-agentens filer ændrede sig undervejs.
* `sim_scenarios/ui_replay_steam_live_x4.json` — nyt (additivt).
* [sim/CONTRACT.md](../../sim/CONTRACT.md) — TILLÆG v6.6 tilføjet nederst. `sim/scenarios/`, `sim/mcu_sim.py`, `sim/run_e2e.py` og `boot/` er **ikke** rørt.
* `ourui_v6_5.c` / `ourui_v6_5.bin` — v6.5 bevaret uændret. `run.sh` (md5 `3d799859…`) og `artifacts/config.json` (md5 `<config-md5-redacted>`) er uændrede.

**Ikke verificeret (kræver enheden):** alt — testplan R.

---

*— design sub-agent, 2026-11-17 (v6.6: fase-semantik efter STEAMLIVE + 64-bit-ur + re-push efter MCU-genstart). Kun lokalt arbejde: kilde, builds, simuleret MCU, previews og selftest; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` og `sim/run_e2e.py` er ikke rørt (i `sim/` er kun kontrakt-tillægget og `reports/v6.6/` nye), og enheden er ikke kontaktet.*

*— design sub-agent, 2026-11-17 (v6.5: boot-push + RX-resync i enheds-build'et). Kun lokalt arbejde: kilde, builds, simuleret MCU, previews og selftest; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` og `sim/run_e2e.py` er ikke rørt (i `sim/` er kun kontrakt-tillægget og `reports/v6.5/` nye), og enheden er ikke kontaktet.*

*— design sub-agent, 2026-11-17 (v6.4: SIM-build + E2E). Kun lokalt arbejde: kilde, builds, simuleret MCU, previews og selftest; `boot/` er ikke rørt, i `sim/` er kun `run_e2e.py` (additivt) og kontrakt-tillægget ændret, og enheden er ikke kontaktet.*

*— design sub-agent, 2026-11-17 (v6.3: produktnavn + i18n). Kun lokalt arbejde: kilde, cross-build, offline previews i to sprog og selftest; `boot/` og `sim/` er ikke rørt, og enheden er ikke kontaktet.*

*— design sub-agent, 2026-11-17 (v6.2: boot-slot-integration). Kun lokalt arbejde som nedenfor; `boot/` er ikke rørt, og enheden er ikke kontaktet.*

*— design sub-agent, 2026-11-17 (v6.1-afstemning). Kun lokalt arbejde: kilde, cross-build, offline previews, selftest og statisk opslag i `demo_v9.dis`. Maskinen er ikke rørt; `artifacts/config.json` er uændret (md5 `<config-md5-redacted>`).*

---

## CHANGES-v6.7

**Net-vagt (wifi ved opstart + selv-heal) og ord 113 i boot-pushen.** Ved slot B-boot startede intet wifi (INSTALL-REPORT §3.1). Stock-UI'et bringer selv wifi op via `libmi_wlan.so`; `demo.sh` gør det ikke. Mekanismen er revers-bevist (`net_re/`, kommentaren "NET" i kilden).

1. **Hvad ourui gør — kun når stakken er NEDE:**
   * Init-scriptet `/config/wifi/ssw01bInit.sh` køres kun, hvis `wlan0` mangler.
   * `/appconfigs/wpa_supplicant.conf` skrives fra `config.json` i præcis `save_config`-formatet (md5-bevist) og kun, hvis indholdet afviger.
   * Så startes `wpa_supplicant -i wlan0 -Dnl80211 -c …` og 1 s senere `udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1` — begge med stock's argumenter. Har `wlan0` stadig IP og default-rute, beholdes de (samme IP).
   * Lever supplicanten, men `wlan0` er nede: `ifconfig wlan0 up`. Ingen IP (8 s) eller ingen default-rute trods carrier (3 s): `udhcpc` med backoff.
   * **En levende wpa_supplicant røres aldrig** (0 `kill()`-kald i kilden). Et init-script, der hænger, logges efter 25 s og dræbes ikke.
   * Fejlende starter får backoff 5 → 10 → 20 → 40 → 60 s.
   * `wifionoff = 0` eller ugyldig config (tomt ssid, password < 8): vagten er FRA. `OURUI_NONET=1` slår den helt fra.
2. **Synligt:** DIAGNOSE → **NETVÆRK · WPA-PID / IP / TILSTAND** (fx `841 / <display-ip> / OPPE`, grøn). Log i `/tmp/ourui_net.log` (kun overgange; passwordet skrives aldrig). `ourui --net-status` (kun læsning) og `--net-watch [s]`.
3. **Ord 113 i boot-pushen** sendes nu som stock's optagede boot-push, `0x0600`: lo-byten (breakTime) er 0 (GROUND-TRUTH.md). GEM-vejen pakker fortsat `tid<<8 | breakTime` (fx `0605`). Selftest-vektor: sub112 len3 = `aa 55 06 70 03 04 01 00 06 46 00 c7 ce`. Med breakTime 0 / 9 / 255 er boot-pushens ord 113 stadig `0600`. `0x0600` er inden for frame-politikken og er ikke MCU'ens startværdi-signatur.
4. **Bevis (lokalt):**
   * Selftest-afsnittet *v6.7 net-vagten*: 20/20 checks mod en model af wlan0/wpa_supplicant/udhcpc. Det dækker koldstart, sund stak (0 indgreb), kill af supplicanten, IP væk, wlan0 nede, AP væk i 2 min, ingen default-rute, init-script der hænger, backoff og config-vagten.
   * E2E i realtid mod SIM-build'ets wifi-model (`sim_e2e.py --only net`): 10/10.
   * Mutations-prøverne `net0` og `dhcpall` fanges.
5. **Bevis på enheden:** `net_heal_proof.py` er klar. Det er read-only telnet plus ét kontrolleret kill af stock's wpa_supplicant, mens demo_v9 er SIGSTOP'et. Der er ingen reboots og ingen skrivning til flag, slots eller config (`/tmp/ourui_t/` på tmpfs), og et fallback griber ind efter 150 s. **Status 2026-11-17 kl. 23:01:** ikke kørt. Enheden var ikke på netværket (se [CHANGES-v6.9](#changes-v69) pkt. 6). Ingen reboots og ingen skrivning til flag, slots eller config i denne runde.

## CHANGES-v6.8

**REVERT-knappen på HJEM er fjernet.** Den var en fase-2-sikring.
* HJEM = gauge + stats + aktivitets-ikoner + side-prikker (y 420). Der er **ingen tryk-zone** tilbage, hvor pillen sad. Selftest: hele feltet 150..329 × 404..479 giver `T_NONE`, både ledig og under bryg. Ingen UI-streng (EN/DA) nævner REVERT. Mutanten `pille` fanges.
* Flugtvejene er uændrede:
  * SYSTEM → **SKIFT TIL ORIGINAL NU** (i run.sh-tilstand en ren exit; run.sh SIGCONT'er demo_v9)
  * BOOT-UI-rækken
  * switcher-gestussen
  * run.sh's trap
* Skift-skærmens run.sh-tekst er nu *RUN.SH-TILSTAND · REN EXIT*.
* Intet nyt på enheden ud over HJEM-foden. Se `REVERT.md` "v6.8".

## CHANGES-v6.9

**Visnings-konditionering af graf og hero, stabil vægt-kilde og vægt-dødbånd.** Kun det, der tegnes, er ændret. Detaljer og konstanter står i [§14](#14-visnings-konditionering-og-stabil-vægt-v69).

1. **Graf:**
   * median-3 + EMA 0,18 pr. sample
   * puls-gate på trykket
   * aktivitets-skift følger median-3 i 2 samples
   * skala = p90 af aktive samples × 1,15, med streger under loftet for samples over toppen
   * tryk tegnes kun under bryg/vand; pulser i ro er streger på grundlinjen
   * chartet er "aktivt" i ro kun, mens et bryg/vand er i vinduet
2. **Hero, gauge og BAR:** median-3 + EMA 0,25 i 10-Hz-takt, snap til rå ved fase-, kilde- og set-punkt-skift.
3. **Vægt:**
   * Kilden er latchet: ord 39 som standard, ord 38 kun under en ægte kværning og dens FÆRDIG.
   * Skift kræver 300 ms; et UI-startet skift sker straks.
   * Dødbånd < 0,25 g → 0 i hero, chart og stat-feltet.
   * Signed (int16) hele vejen.
4. **DIAGNOSE → GRAF:** glat (standard) / rå, kun for denne session. RÅ-chartet er mærket **RÅ**.
5. **Forgængerens v6.9-mellemstand** (gemt som `ourui_v6_9wip.c` / `.bin`) havde konditioneringen, men:
   * ingen v6.9-selftests og 2 røde v6.6-selftests
   * p90 over hele vinduet var fravalgt til fordel for en "hold"-skala
   * en snap til rå ved fase-skift: et bryg, der sluttede i en deadhead-puls, gav en pukkel, der klang af over 2 s
   * ingen gate: 2-sample-pulser gik igennem median-3 som en 1,5 s pukkel i loftet
   * vægt-kilden fulgte fasen pr. sample
   * intet dødbånd
   * idle-chartet blev stadig tændt af vedligehold-varmecyklusserne

   Alt det er rettet her. Undervejs viste før/efter-kørslen to fejl i min egen første udgave, som også er rettet og har selftests:
   * ét blink af kværn-vægten (18,7 g) i stat-feltet, når kilden skiftede 38 → 39
   * en 2 s lang hale, når koppen blev løftet af i ro
6. **Verifikation (lokalt):**

| Hvad | Resultat |
|---|---|
| Selftest (enhedens kode, preview-build) | **354 checks, 0 fejl** på EN og DA (v6.8: 329). Heraf v6.9-afsnittet: **25/25** — spike-tog, trin, fase-snap, deadhead-puls ved skift, set-punkt-snap, ro efter bryg, vægt-kilde under vedligehold-bits + bit-6-glitch, ægte/UI-startet kværning, kilde-skift uden blink, kop af/på, dødbånd (ro + bryg), signed tara |
| Selftest som ægte 32-bit ARM-binær (lokal ARMv7-container) | **353 / 0** (EN + DA; ét vært-specifikt tjek mindre); med `long` som tidstype fejler 10 omløbs-checks (som før) |
| Mutations-prøver | **10/10 fanget** — 4 fra v6.7/v6.8 + 6 nye: fase-kilde, intet dødbånd, unsigned vægt, ingen gate, p90 over hele vinduet, tryk-kurve i ro |
| Enheds-build mod v6.8 | 0 warnings (enhed, SIM-ARM, SIM-vært; -Wall -Wextra = samme to kategorier som v6.6). Reproducerbar. 17 ændrede + 23 nye stykker, alle visning. **29 styrings-/protokol-stykker token-identiske**: frame-politik, write-kø, aktivitets-recepter + -vagt, fase-logik, GEM-pakning, boot-push, net-vagt, RX, CRC, flag |
| Previews mod v6.8 | **90 af 126 byte-identiske** (HJEM i ro, lister, detaljer, BOOT-UI, skift-sider). 36 tilsigtede: 20 HJEM under aktivitet + versions-streng + DIAGNOSE's GRAF-række. **4 nye scenarier × 2 sprog**: `63_home_brew_spikes`, `64_home_brew_spikes_raw`, `65_home_idle_after_brew_pulses`, `66_diag_graph_raw` |
| Circle-tjek | 67/67 + 67/67, 0 violations |
| Spec-afstemning · i18n · boot-/config-tjek | ALT AFSTEMT · appendiks i sync (349 streng-id'er) · grønne |
| E2E mod den simulerede MCU (hurtig, jf. [§15](#15-udviklings-iteration-dev-iteration-policy)) | `sim/selftest.py` **120/120** · `run_e2e.py --all --quick --parallel 4` (EN): **5/5 scenarier, 97/97 checks på 44 s** (`quick_en/report.md`). Den fulde suite er **ikke** kørt i denne runde; den venter til CI/CD eller release. Inden politikken kom, nåede forgængeren at køre EN, DA og REF mod samme SIM-binær (`fbd8c89c…`): 25/25 scenarier og 644/644 checks i hver, plus UI-scenarierne på EN med 6/6. UI-scenarierne på DA blev afbrudt og er ufuldstændige |
| Før/efter (graf + vægt) | `ui_calm_graph` mod v6.8 og v6.9 med samme sim og seed: `calm.txt` **3/3**, `calm_stats.txt` **8/8**. Vægten i ro viste −0,2 / 0,0 / 0,2 g i v6.8 og viser nu 0,0 g. Der er 0 vægt-spikes, og kilden skifter præcis 2 gange (kværning ind og ud). Tryk-grafen i ro er inaktiv trods 4 + 24 vedligehold-pulser (rå op til 16 240). Efter brygget holder y-toppen sig på 1555..1585 trods pulser op til 15 152 og springer aldrig. ≈BAR-feltet skifter 21 gange under brygget, mod 148 i v6.8. Billeder (venstre v6.8, højre v6.9: ro, ekstraktion, FÆRDIG, ro efter bryg, damp, tara): `calm_key_v68_v69.png` · `calm_key_chart_v68_v69.png` |
| Selv-heal på enheden | **Ikke kørt.** Enheden svarede ikke 2026-11-17 kl. 23:01: <display-ip> gav "Host is down", og ingen vært i LAN'ets ARP-tabel havde telnet-port 23 åben. `net_heal_proof.py` er klar og kræver kun, at enheden er på netværket. Lokalt er net-vagten bevist med selftest 20/20 og SIM-wifi-modellen 10/10 |

7. **Filer:**

| Fil | md5 |
|---|---|
| `ourui_v6.c` (v6.9) | `440be5124c0d986ec23e4c3b08f84149` |
| `ourui_v6` (enheds-binær) | **`2dce4eba0b4ce68dcd0369e955cf3b77`** |
| `ourui_v6_sim_arm` · `ourui_v6_sim` | `cbebc8b128f413c1d40a83de2bd6343a` · `fbd8c89c236f508ec725505c519b7b91` (vært; skifter ved hver linkning) |
| `ourui_v6_9wip.c` / `.bin` (stand ved rundens start) | `048a40ea7894dab5b57a58f6d9af8ead` / `bcb1b50c913700c64a6cfc39bb5a5776` |
| `ourui_v6_8wip.c` / `.bin` (v6.8 = den hidtidige enheds-binær) | `5cdcc14b0503c67b939e27b6579655ff` / `75887fb154ffef20cc72696064e16c39` |

   Rapport: `sim/reports/v6.9/SUMMARY.md`. Nye/ændrede værktøjer:
   * `build_sim.sh`: reference v6.8 (v6.8-udgaven bevaret som `round_v6/build_sim_v68.sh`)
   * `round_v6/build_previews.sh`: `v68_compare.txt`, 6 nye mutanter
   * `round_v6/fbcrop.py`: chart-udsnit, før/efter-ark
   * `round_v6/calm_stats.py`: før/efter-tal pr. segment (vægt + tryk-graf) fra state-linjerne
   * `sim_e2e.py`: dele `ref` og `calm`
   * `sim_scenarios/ui_calm_graph.json`: UI-side, additivt

   `sim/` er ikke rørt, ud over et tillæg i `sim/CONTRACT.md` og rapporterne under `sim/reports/v6.9/`.

8. **Afsluttende runde (2026-11-17, efter DEV-ITERATION-POLICY):** `ourui_v6.c` er **ikke ændret** (md5 `440be512…` = standen ved rundens start). Begge binærer er bygget igen fra kilden og er byte-identiske med de leverede: `ourui_v6` `2dce4eba…` og `ourui_v6_sim_arm` `cbebc8b1…`, med 0 warnings i alle tre builds (frisk zig-cache). I runden er kørt `build_previews.sh` (1 min 13 s), `sim/selftest.py`, quick-kørslen og calm-delen. Det gav udfyldelsen af tabellen i pkt. 6 og [§15](#15-udviklings-iteration-dev-iteration-policy). `ourui_v6_9wip.c` / `.bin` er forgængerens standpunkt og er bevaret uændret. Kilden er ikke ændret, så der er ingen ny "pre-round"-kopi.


## CHANGES-v6.10

**Tryk som ≈bar-estimat i stedet for "UKALIBRERET".** Kun det, der vises, er ændret. Detaljer, tabeller og forbehold: [§16](#16-tryk-estimat-v610).

1. **Anker og skala:** P0 = **358 rå** (pre-infusion-plateauet i optagelsen, BREW.md §6) = config `preInfusionPressure` (**4 bar**) → k = 4/358 = **0,011173 bar/rå**. Ankeret læses fra config, så en ændret `preInfusionPressure` ændrer skalaen (uden for 1..15 → 4). Konstanter: `P_ANCHOR_RAW` 358, `P_ANCHOR_BAR_DEF` 4, `P_EST_MAX` 20. v6.9's hypotese (`P_HYP_NUM/P_HYP_DEN` = 9 bar ved 1346) er fjernet.
2. **Beregnede værdier:** pre 358 → **4,0 bar** · ekstraktion 1346 → **15,0 bar** (1379 → 15,4) · deadhead 16 670 → 186 bar lineært → **vises ikke**: *PULS* på HJEM, *VEDLIGEHOLD-PULS* (ro) / *PULS, IKKE TRYK* (aktivitet) på DIAGNOSE. Grænsen er 20 bar = 1790 rå.
3. **Tekster:** `≈BAR*` → **≈BAR · EST.**; `TRYK* UKALIBRERET` / `PRESSURE* UNCAL.` → **TRYK ≈BAR · EST.** / **PRESS. ≈BAR · EST.**. Ingen streng indeholder længere UKALIB/UNCAL (selftest).
4. **Fodnoten/grundlaget:** tap på chartet (når det viser tryk) eller BAR-feltet (under bryg) → toast *PRÆ-INF. 4 BAR = RÅ 358 · LINEÆR*. Ingen frames, intet faseskift (selftest). DIAGNOSE har en ny blok **TRYK · ESTIMAT**: anker-linjen (`4 BAR = RÅ 358 · k 0.01117`), nu-linjen (rå ord 51 → ≈bar eller puls, rav) og to forbeholds-noter.
5. **RÅ-visningen er uændret rå:** med DIAGNOSE → GRAF = RÅ viser BAR-feltet ord 51 som rå tal under labelen **TRYK RÅ** (v6.9 viste den ukonditionerede værdi omregnet til bar).
6. **Versions-strengen** er V6.10. `TX_DV_VERSION`/`_RO` er flyttet fra klasse-budgettet (168 px) til den eksplicitte celle-layout-test (højre kolonne, 228 px): *MERAKI+ V6.10 RO* er 174 px og passer i cellen.
7. **Verifikation (lokalt, jf. [§15](#15-udviklings-iteration-dev-iteration-policy) — den fulde E2E-suite er ikke kørt):**

| Hvad | Resultat |
|---|---|
| Selftest (preview-build, enhedens kode) | **368 checks, 0 fejl** på EN og DA (v6.9: 354). Nyt afsnit *v6.10 tryk-estimat* **14/14**: skala (358 → 4,0; 1346 → 15,0; 1379 → 15,4), HJEM under pre og ekstraktion, billedtekst + ingen UKALIB/UNCAL, toast fra chart og BAR-felt (ingen frames), toast ikke i ro uden bryg, config-anker 3 bar (config-kopi → 11,3) og tilbage til 4, anker uden for 1..15, puls-grænsen, vedvarende deadhead → PULS, DIAGNOSE puls/vedligehold, ro uden puls, RÅ-visning. Layout: TRYK · ESTIMAT-linjerne og toasten passer for anker 1..15 og 5-cifrede rå værdier |
| Selftest som 32-bit ARM-binær (lokal ARMv7-container) | **367 / 0** (EN + DA); med `long` som tidstype fejler 10 omløbs-checks (som før) |
| Mutations-prøver | **14/14** — de 10 fra v6.7–v6.9 + 4 nye: `hyp9` (v6.9-skalaen), `ankerfast` (anker ikke fra config), `ingenklem` (186 bar vises), `raabar` (RÅ viser bar) |
| Enheds-build mod v6.9 | 0 warnings (enhed, SIM-ARM, SIM-vært; frisk zig-cache), reproducerbar, statisk. 12 ændrede + 6 nye stykker, alle tilsigtede. **37 stykker token-identiske** (frame-politik, write-kø, aktivitet, fase, GEM, boot-push, net-vagt, RX, CRC, flag + v6.9's konditionering, chart og vægt-kilde) |
| Previews mod v6.9 | **100 af 134 byte-identiske**. 34 tilsigtede: 16 HJEM-billeder **kun** i chart-/stat-feltet (x 96..384, y 240..340), resten versions-streng / DIAGNOSE. **3 nye scenarier × 2 sprog**: `67_diag_pressure_ext`, `68_diag_pressure_maint_pulse`, `69_home_brew_pinfo_toast` |
| Circle-tjek | 70/70 + 70/70, **0 violations** |
| Spec-afstemning · i18n · boot-/config-tjek | **ALT AFSTEMT** · appendiks i sync (361 streng-id'er) · grønne |
| `sim/selftest.py` · `run_e2e.py --all --quick --parallel 4` (EN) | **120/0** · **5/5 scenarier, 97/97 checks, 44 s** (`quick_en/report.md`). I bryg-scenariets state-linjer: PRE `4.0`, EXT `14.9` (rå 1318), en deadhead-sample efter stop (rå 4901) har `pulse: true`, `bar: null` |
| Før/efter-billeder | `labels_before_after.png` (chart + stats, v6.9 \| v6.10, EN og DA: pre, ext, FÆRDIG, RÅ) · `home_ext_before_after.png` · `new_v610_screens.png` |
| Enheden | **Ikke kontaktet** i denne runde (ingen telnet var nødvendig). Ingen reboots, flag-, slot- eller config-writes |

8. **Filer:**

| Fil | md5 |
|---|---|
| `ourui_v6.c` (v6.10) | `23672df8557a1fbb4d54cdb6b220c244` |
| `ourui_v6` (enheds-binær, statisk ARMv7) | **`29bc801c1c777d9cb3484bfa960c66bc`** |
| `ourui_v6_sim_arm` · `ourui_v6_sim` (vært) | `14f58597ae605f642e37026bd97b66b3` · `0ce8d412f20d3a07b507b89566fb2605` |
| `ourui_v6_9.c` / `ourui_v6_9.bin` (v6.9, rollback) | `440be5124c0d986ec23e4c3b08f84149` / `2dce4eba0b4ce68dcd0369e955cf3b77` |
| `ourui_v6_9_sim` / `ourui_v6_9_sim_arm` (v6.9 SIM) | `fbd8c89c236f508ec725505c519b7b91` / `cbebc8b128f413c1d40a83de2bd6343a` |

   Værktøjer: `build_sim.sh` sammenligner nu med v6.9 (v6.9-udgaven bevaret som `round_v6/build_sim_v69.sh`); `round_v6/build_previews.sh` skriver `v69_compare.txt` og har 4 nye mutanter. `boot/`, `sim/mcu_sim.py`, `sim/scenarios/` og `sim/run_e2e.py` er ikke rørt. Rapport: `sim/reports/v6.10/SUMMARY.md`.


---

## CHANGES-v6.11

**UX-review og -optimering.** Kun visning og touch-ruter er ændret. Fund, prioritering og det, der er udskudt, står i [§17](#17-ux-review-v611).

1. **Klart sprog (O4).** Ord-numre er fjernet fra alle noter (14 noter). Sove-, offline- og fejl-tekster på HJEM er klart sprog, og E-koden står nu i chart-hintet. 8 toasts er omskrevet. DIAGNOSE og §4 har fortsat ord-numrene. Toast-teksterne, som E2E-scenarierne citerer (*GEM FEJLET*, *FEJL AKTIV*, *UDEN FOR SPEC*, *HOLD KNAPPEN NEDE FOR START*, *GEMT · …*), er bevaret.
2. **HJEM (O3, O8, E1, E2, E4):**
   * hint i det tomme chart-felt (`TX_C_HINT`, `TX_C_HINT2`, `TX_C_ERRCODE`, `TX_C_DIAGREF`); *INGEN AKTIVITET* (`TX_C_NOACT`) er fjernet
   * label *BRYGKEDEL · NU* og underlinjen *MÅL 87,0°* i F_S
   * stat-labels *DAMP* / *KEDEL* / *TRIN* / *RÅ*, med ≥ 16 px luft mellem labels
   * chartet hedder *TRYK*, og BAR-feltet hedder *≈BAR EST.*
3. **Navigation (O1, O5, O6):**
   * tandhjul på HJEM (→ BRYG)
   * kant-tryk også på listerne
   * swipe-vagt med akse-lås, ≤ ~30°
   * luk-chevron og stryg-ned i detalje- og system-visningen
4. **Kontrast (O9).** Tekst 2 er nu 176,183,196 (≥ 7:1) og tekst 3 er nu 140,147,162 (≥ 4,5:1 også på listerækker). Den nye farve "deaktiveret" (98,105,120) bruges til slukkede elementer. Fremskridts-ringen har beholdt sin farve.
5. **DIAGNOSE (O7).** Siden er delt i sektionerne STATUS, BOOT, NET, GRAF, TRYK, *RÅ TÆLLERE · SERVICE* og OM. Siden er 38 px længere. GRAF-pillens tryk-felt er nu 44 px højt.
6. **Selftest-afsnittet "v6.11 UX-review" (19 checks):**
   * touch-mål målt via `hit_test()`
   * WCAG-kontrast
   * swipe-vagt: skrå gestus, vandret gestus og lodret start (liste og HJEM)
   * luk-chevron og stryg-ned i detalje og BOOT-UI, herunder at en ændret værdi ikke gemmes, og at en gestus startet på − ikke lukker
   * tandhjul og kant-tryk uden frames
   * hint og underlinje i ro, ved fejl, offline og sover
   * ingen ord-numre, ACK eller frame-politik i brugerrettede strenge
   * DIAGNOSE-sektionernes rækkefølge og en ét-til-ét placering af cellerne

   To ældre checks er opdateret til v6.11's hensigt, uden at blive svækket:
   * REVERT-zonen tillader nu kun tandhjulet (side-skift, ingen frame), og mutanten `pille` fanges stadig
   * chart-tekst-checket kræver nu *TRYK* / *≈BAR EST.*
7. **Verifikation (lokalt; DEV-ITERATION-POLICY [§15](#15-udviklings-iteration-dev-iteration-policy)):**

| Hvad | Resultat |
|---|---|
| Selftest (enhedens kode, preview-build) | **387 checks, 0 fejl** på EN og DA (v6.10: 368). Heraf v6.11-afsnittet **19/19** |
| Selftest som ægte 32-bit ARM-binær (lokal ARMv7-container) | EN + DA **ALT OK**. Med `long` som tidstype fejler 10 omløbs-checks (som før) |
| Mutations-prøver | **20/20 fanget**. Seks er nye: `swipe610` (v6.10's swipe-regel), `noback`, `lavkontr` (v6.10's tekst-3), `ordnote`, `ingenhint`, `stat12` (*STEAM BOILER* igen) |
| Enheds-build mod v6.10 | 0 warnings i alle tre builds (frisk zig-cache). Reproducerbar. 34 ændrede og 8 nye stykker, alle visning eller touch. **47 styrings-/protokol-stykker token-identiske** |
| Previews mod v6.10 | 140 sammenlignet. Alle afviger tilsigtet (farver og tekster), men **gauge-ringen og tick-ringen er byte-identiske i alle**. Undtaget er sider uden ring (lister, DIAGNOSE) og toast-pillen. **3 nye scenarier × 2 sprog**: `70_home_gear_pressed`, `71_detail_back_pressed`, `72_diag_raw_counters` |
| Circle-tjek | 73/73 + 73/73, 0 violations |
| Spec-afstemning · i18n · boot-/config-tjek | ALT AFSTEMT · appendiks i sync (365 streng-id'er) · grønne |
| E2E mod den simulerede MCU (hurtig) | `sim/selftest.py` **120/120**. `run_e2e.py --all --quick --parallel 4` (EN): **5/5 scenarier, 97/97 checks på 44 s**. Målrettet, fordi touch-ruterne er ændret (DA): `ui_touch_navigate_save_start` **26/26** og `ui_rows_real_frames` **27/27**. Den fulde suite er **ikke** kørt; den venter til CI/CD eller release |
| Enheden | **Ikke kontaktet.** Ingen reboots og ingen flag-, slot- eller config-writes |

8. **Filer:**

| Fil | md5 |
|---|---|
| `ourui_v6.c` (v6.11) | `e9eea8caebfa5d832884d96996dbd2c7` |
| `ourui_v6` (enheds-binær) | **`725386ccadade594275f20cc998885eb`** |
| `ourui_v6_sim_arm` · `ourui_v6_sim` | `13b5ad2e374d5a79e097c008a581530e` · `3e12be0568223925c7c0ccd28f13c9d1` (vært; skifter ved hver linkning) |
| `ourui_v6_10.c` / `.bin` (v6.10 bevaret) | `23672df8557a1fbb4d54cdb6b220c244` / `29bc801c1c777d9cb3484bfa960c66bc` |
| `ourui_v6_10_sim_arm` · `ourui_v6_10_sim` | `14f58597ae605f642e37026bd97b66b3` · `0ce8d412f20d3a07b507b89566fb2605` |

   Rapport: `sim/reports/v6.11/SUMMARY.md` med før/efter-ark (`EN` · `DA`; v6.10 til venstre, v6.11 til højre).

   Værktøjer:
   * `build_sim.sh`: reference v6.10 (v6.10-udgaven er bevaret som `round_v6/build_sim_v610.sh`)
   * `round_v6/build_previews.sh`: `v610_compare.txt` via det nye `round_v6/v610_compare.py`, før/efter-ark og 6 nye mutanter (v6.10-udgaven er bevaret som `round_v6/build_previews_v610.sh`)

   `sim/` er ikke rørt, ud over rapporterne under `sim/reports/v6.11/`.

## CHANGES-v6.13

**Manuel STOP for VAND, BRYG og KVÆRN.** Brugerfejl: varmt vand kunne ikke stoppes fra Meraki+-skærmen. Kun damp havde en STOP-knap. Stop-ordene og beviserne står i [§18](#18-manuel-stop-v613). Auto-stop, start-recepterne, frame-politikken og fase-logikken er uændrede.

1. **Stop-frames, reverset fra MCU-firmwaren** og krydstjekket mod stock-UI'ets egne stop-kald:
   * VAND: `aa 55 06 10 01 00 00 c0 0f` (ord 16 = 0), plus `aa 55 06 13 01 00 00 c0 4b` (ord 19 = 0), hvis ord 19 ≠ 0
   * BRYG: `aa 55 06 16 01 00 00 c0 87` (ord 22 = 0)
   * KVÆRN: `aa 55 06 06 01 00 00 c4 47` (ord 6 = 0)

   I alle tre tilfælde er det det samme ord, MCU'ens egen auto-stop nulstiller. Ord 9 og ord 3 er tare-triggere og ikke stop-ord. REGISTERMAP §2.2 har byttet om på ord 3 og ord 6 (§18.1).
2. **STOP-knappen på HJEM** står samme sted og har samme stil som STOP DAMP: *STOP WATER / STOP BREW / STOP GRIND* og *STOP VAND / STOP BRYG / STOP KVÆRN*, tryk-felt 180 × 56 px. Kører en hoved-aktivitet samtidig med latchet damp, står der to knapper á 148 × 56 px. Damp-pillen er 160 px bred (før 150): "STOP WATER" / "STOP KVÆRN" skal have 14 px luft i hver side.
3. **Feedback:**
   * *STOPPER VAND…* straks ved tryk
   * *VAND STOPPET* (grøn), når work_sta-bits'ene er ryddet
   * *STOP: INTET SVAR · PRØV IGEN* (rød), hvis der intet ACK kommer efter retransmit
   * *STOP IKKE BEKRÆFTET · PRØV IGEN* (gul), hvis bits'ene står efter 5 s

   Et ACK alene tæller ikke som bekræftelse. Et dobbelt-tryk køer ikke en frame mere. Underlinjen er *AUTO-STOP VED 40.0 G · ELLER STOP*.
4. **Nye strenge (EN/DA, 13):**
   * `TX_B_STOP_GRIND/BREW/WATER`
   * `TX_T_STOP_GRIND/BREW/WATER`
   * `TX_T_STOPPED_GRIND/BREW/WATER/STEAM`
   * `TX_T_STOP_NOACK/UNCONF/FAIL`

   `TX_H_AUTOSTOP_G` har fået *· OR STOP* / *· ELLER STOP*. Appendiks A er genereret igen.
5. **SIM-flade (additiv):**
   * state-JSON `stop: {btns, pending, last, req, ok, fail}`
   * ctrl-handlingerne `grind_stop` / `brew_stop` / `water_stop`
   * layout-mål med samme navne

   `steam_stop` virker som før.
6. **Verifikation (lokal dev-iteration, §15):**

| Hvad | Resultat |
|---|---|
| Selftest | **413 / 0** EN og **413 / 0** DA. Nyt afsnit "v6.13 manuel STOP" **26/26**: frames byte for byte; politik; synlig pr. fase; tryk-felt ≥ 48 px; hele forløbet tryk → STOPPER → frame → retransmit → ACK → bits ryddet → STOPPET for vand, bryg og kværn; intet ACK; ikke bekræftet; dobbelt-tryk; to knapper; auto-stop uændret; READONLY |
| Selftest som 32-bit ARM-binær | **412 / 0** EN + DA (ARMv7-container) |
| Mutations-prøver | **25/25**. 5 nye: `nostop`, `stop9`, `stop19`, `noconfirm`, `ackok` |
| Builds | enhed, SIM-ARM og SIM-vært: **0 warnings**. Enheds-build'et mod v6.12: 13 nye og 12 ændrede stykker, alle på listen over tilsigtede. 47 styrings-/protokol-stykker er **token-identiske** |
| Previews | 78 scenarier × 2 sprog; **circle 0 violations** (156 billeder). Mod v6.12: 146 sammenlignet, **128 byte-identiske**, resten kun i aktions-rækken, underlinjen eller toasten. 5 nye scenarier (73–77) |
| Spec-afstemning | **ALT AFSTEMT** |
| `sim/selftest.py` | **120 / 0** |
| `run_e2e.py --all --quick --parallel 4` (EN) | **5/5, 97/97**, 44 s |
| Målrettet SIM (DA, `sim_scenarios_v613/`) | `ui_stop_water` / `ui_stop_grind` / `ui_stop_brew` **3/3, 51/51**: ægte FIFO-touch → start → STOP → `write_words` sub16 / sub6 / sub22 = [0] + ACK + toast *STOPPER …*. `mcu_sim.py` stopper endnu ikke på disse ord; se `SIM_HANDOFF` |
| On-device | **ikke kørt: maskinen var offline** (ping og ARP uden svar, telnet lukket). `verify_stop_v613.py` er klar (§11 S), og dens analysator er testet mod en syntetisk log (3/3) |

7. **Filer og md5:**

| Fil | md5 |
|---|---|
| `ourui_v6.c` (v6.13) | `eec5085ddc1861c212d396c70771e9dc` |
| `ourui_v6` (statisk ARMv7; reproducerbar) | `163692b5b3e11d8294471c5a0eae578b` |
| `ourui_v6_sim_arm` | `ed287001bc34d132a0da78026d1c806e` |
| `ourui_v6_sim` (vært; skifter ved hver linkning) | `84f194c731b7230201c102538d236195` |

   v6.12 er bevaret som `ourui_v6_12.c` (`e9eea8ca…`) / `ourui_v6_12.bin` (`725386cc…`, den udrullede) og `ourui_v6_12_sim` / `ourui_v6_12_sim_arm`. De gamle build-scripts er bevaret som `round_v6/build_previews_v611.sh` og `round_v6/build_sim_v611.sh`.
8. **Ikke rørt:** `boot/`, `sim/mcu_sim.py`, `sim/scenarios/`, `sim/run_e2e.py`, `config.json`, flag og slots. Der er ingen reboots, og enheden er ikke kontaktet ud over to forbindelsesforsøg (ping og TCP-connect til :23), som begge fejlede.

## CHANGES-v6.14

**Den fysiske tænd/sluk-knap.** Brugerfejl: knappen gjorde intet, mens Meraki+ kørte. ourui læste kun touch-noden (`event0`); knappen er k62091-noden (`event1`). Stock's semantik er reverset fra demo_v9 og MCU-firmwaren og spejlet; tabel og adresser i [§19](#19-tænd-sluk-knappen-v614). Start-recepter, STOP-vejen, write-køen, fase-logikken og boot-pushen er token-identiske med v6.13.

1. **Knap-noden:** k62091 (`/dev/input/event1`) åbnes read-only **uden EVIOCGRAB** (stock griber den heller ikke). Auto-valg efter navn → EV_KEY uden EV_ABS → `event1`; `OURUI_KBD=<sti>` overstyrer. Touch-noden gribes som før.
2. **Sluk (som stock):** HOLD 1,5 s (stock: to sekund-skift = 1–2 s) → cmd8 tid/alarm-frame (alarm 0/0/0) → ≥ 80 ms → `aa 55 06 00 02 00 00 00 00 56 94` (ord 0 = 0, ord 1 = 0) → ACK → SLUKKET. Lyset slukkes 1,5 s efter trykket (duty 99 + `pwm2/enable 0`). Keep-awake står stille, mens der er slukket. Spærret under bryg/damp/kværn med samme prædikat som stock (`isExtractionOrSteamSwitchOn`) — stock stopper aldrig en aktivitet; ourui heller ikke. Uden forbindelse, med ord 0 = 0 eller i READ-ONLY sendes intet. Intet ACK → tilbage til TÆNDT med *SLUK: INTET SVAR · PRØV IGEN*.
3. **Tænd:** knappen i SLUKKET → lyset + TÆNDER…; MCU'en vågner af samme hold (reset). Når controlleren svarer med ord 0 = 1 → PowerOn `aa 55 06 00 01 01 00 c5 5f` + vågen `aa 55 06 01 01 00 00 c5 33` (som ved opstart) → HJEM. 10 s uden svar → SLUKKET igen. Svarer controlleren af sig selv med ord 0 = 1, tændes ourui også. Efter MCU-resettet lægger den eksisterende re-push (v6.6) config'en tilbage. Touch i SLUKKET viser siden i 8 s og sluges.
4. **Frame-politik:** `word_ok(0)` tillader nu 0 og 1, men ord 0 = 0 kræver den nye intent `I_POWEROFF` (ellers `WJ_TRIG`). `I_POWEROFF` tillader intet andet (start-ord, ord 26/27/30 og ord 1 = 1 afvises fortsat; `_Static_assert`). `rx_feed` tæller 0x88-kvitteringen for cmd8 (ingen anden ændring).
5. **Verifikation (lokal dev-iteration, §15):**

| Hvad | Resultat |
|---|---|
| Selftest | **441 / 0** EN og **441 / 0** DA. Nyt afsnit "v6.14 taend/sluk" **28/28**: cmd8 + PowerOff + PowerOn byte for byte; politik (I_POWEROFF); kort tryk = intet; hold → cmd8 ved 1,5 s, cmd6 ≥ 80 ms senere, ACK → SLUKKET; præcis to frames (ingen stop-frames); lys af efter 1,5 s; keep-awake 0 frames i 2 × 65 s (kontrol: 2 i TÆNDT); touch sluges + 8 s visning; tænd → PowerOn + vågen først ved ord 0 = 1; 10 s-timeout; auto-tænd; spærret under bryg/damp/kværn/bit 12 og et nyt hold bagefter; varmt vand ikke spærret og ikke stoppet; ingen forbindelse / ord 0 = 0 / READ-ONLY; intet ACK; åben detalje; layout; DIAGNOSE; sort side. Det gamle tjek "ord0=0 afvist" forventer nu `WJ_TRIG` i stedet for `WJ_RANGE` |
| Selftest som 32-bit ARM-binær | **440 / 0** EN + DA (ARMv7-container) |
| Mutations-prøver | **34/34**. 9 nye: `nokey`, `off1`, `nogap`, `nocmd8`, `noguard`, `kafight`, `noackwait`, `stopfirst`, `freeoff` |
| Builds | enhed, SIM-ARM og SIM-vært: **0 warnings**; `-Wall -Wextra` samme to kategorier som v6.6. Enheds-binæren er reproducerbar. Mod v6.13: 25 nye og 23 ændrede stykker, alle på listen over tilsigtede; **54** styrings-/protokol-stykker token-identiske. EVIOCGRAB-kald: 3 som før (kun touch), 0 på knap-noden; ingen ny `system()`/`popen()`, ingen `/sys/power`, `killall` eller `reboot` |
| Previews | 83 scenarier × 2 sprog; **circle 0 violations** (166 billeder). Mod v6.13: 156 sammenlignet, **134 byte-identiske**, resten kun i DIAGNOSE-listen og rulle-indikatoren. 5 nye scenarier (78–82: SLUKKER, SLUKKET, TÆNDER, spærret under bryg, DIAGNOSE) |
| Spec-afstemning | **ALT AFSTEMT** |
| `sim/selftest.py` | **150 / 0** |
| `run_e2e.py --all --quick --parallel 4` (EN) | v6.13-SIM-binæren (uændret): **5/5, 97/97** · ekstra mod `ourui_v6_14_sim`: **5/5, 97/97** |
| `verify_power_v614.py --selftest` | **5/5** (god log grøn; uden cmd8 / uden dvale / keep-awake i SLUKKET / uden re-push fanges) |
| On-device | **ikke kørt** (v6.13 blev installeret i slot B samtidig; ingen kontakt med enheden). `verify_power_v614.py` er klar (§11 T) |

6. **Filer og md5 (SEPARATE — `ourui_v6` / `ourui_v6_sim*` er v6.13 og er IKKE rørt):**

| Fil | md5 |
|---|---|
| `ourui_v6.c` (v6.14-kilden) | `94e7f577afb589d4e02c54b0942f78ab` |
| `ourui_v6_14.bin` (statisk ARMv7; reproducerbar) | `1dd63f42e4b0d649ef851660fc123a10` |
| `ourui_v6_14_sim_arm` | `d0559b45751a1fcddf75af57ac2df037` |
| `ourui_v6_14_sim` (vært; skifter ved hver linkning) | `deac1b7aff018619bb86afb582e9cd7d` |
| `ourui_v6_13.c` (v6.13-kilden, kopi før ændringerne) | `eec5085ddc1861c212d396c70771e9dc` |

   Build-scripts: `build_v614.sh` (bygger KUN de tre `_14`-filer; log `round_v6/sim_build_check_v614.txt`) og `round_v6/build_previews_v614.sh` (+ `round_v6/v613_compare.py`). `build_sim.sh` og `round_v6/build_previews.sh` er v6.13-udgaverne og er uændrede — de skriver `ourui_v6` og må ikke køres, før v6.14 er forfremmet.
7. **Forfremmelse (forælderen, når v6.13-installationen er færdig):** `cp ourui_v6 ourui_v6_13.bin; cp ourui_v6_sim ourui_v6_13_sim; cp ourui_v6_sim_arm ourui_v6_13_sim_arm` (bevar v6.13) → `cp ourui_v6_14.bin ourui_v6; cp ourui_v6_14_sim ourui_v6_sim; cp ourui_v6_14_sim_arm ourui_v6_sim_arm` → `md5 -q ourui_v6` = `1dd63f42…`. Derefter on-device: `python3 verify_power_v614.py` (run.sh-tilstand fra `/tmp`, ingen install nødvendig til beviset).
8. **REVERT:** v6.14 ligger kun i `ourui_v6.c` og de tre `_14`-filer. Tilbage til v6.13: `cp ourui_v6_13.c ourui_v6.c` (md5 `eec5085d…`); binærerne `ourui_v6` (`163692b5…`) og `ourui_v6_sim*` er uændrede v6.13. Er v6.14 allerede forfremmet/installeret: `cp ourui_v6_13.bin ourui_v6` (fra trin 7) og geninstallér med den normale boot-install-vej. UI_v6.md før v6.14 findes ikke som kopi — afsnittene §19, testplan T og dette afsnit er rene tilføjelser, og Appendiks A er genereret fra streng-tabellen. Ingen config-, flag- eller slot-ændringer at fortryde (ingen er lavet). På enheden efterlader en afbrudt `verify_power_v614.py` højst `/tmp/ourui_p/` (tmpfs) — run.sh SIGCONT'er stock, og ourui tænder lyset ved exit; slukket lys efter et nedbrud: `echo 1 > /sys/class/pwm/pwmchip0/pwm2/enable`.
9. **Ikke rørt:** `boot/`, `sim/mcu_sim.py`, `sim/scenarios/`, `sim/run_e2e.py`, `ourui_v6`, `ourui_v6_sim`, `ourui_v6_sim_arm`, `config.json`, flag og slots. Ingen kontakt med enheden. Previews i `round_v6/` er genereret fra v6.14-kilden.

## Appendiks A — streng-tabel (id / EN / DA)

<!-- I18N-APPENDIX:BEGIN (genereret af i18n_appendix.py — ret i STR_TABLE i ourui_v6.c, ikke her) -->

**403 streng-id'er × 2 sprog** (43 er ens i EN og DA: enheder, egennavne, tekniske ord og kommandoer). Klasse = det felt strengen vises i; selftesten måler hver klassificeret streng i begge sprog mod feltets pixel-budget (`-` = måles i sin sammenhæng af layout-testene). `%s`/`%d` er pladsholdere for tal og navne; tekst i `kode-skrift` inde i en streng vises med små bogstaver (stier, kommandoer). Kilde: `round_v6/strings.tsv` (`--dump-strings`).


**Enheder (identiske i begge sprog)**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_U_G` | - | `G` | `G` |
| `TX_U_S` | - | `S` | `S` |
| `TX_U_BAR` | - | `BAR` | `BAR` |
| `TX_U_C` | - | `°C` | `°C` |
| `TX_U_F` | - | `°F` | `°F` |

**Produkt, splash og sider**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_TAGLINE` | tagline | `ADVANCED BREWING INTERFACE` | `AVANCERET BRYGGE-INTERFACE` |
| `TX_SPL_CFG_OK` | - | `CONFIG LOADED` | `CONFIG INDLÆST` |
| `TX_SPL_CFG_MISSING` | - | `CONFIG MISSING` | `CONFIG MANGLER` |
| `TX_PG_BREW` | page-title | `BREW` | `BRYG` |
| `TX_PG_STEAM` | page-title | `STEAM · WATER` | `DAMP · VAND` |
| `TX_PG_SYSTEM` | page-title | `SYSTEM` | `SYSTEM` |
| `TX_PG_DIAG` | page-title | `DIAGNOSTICS` | `DIAGNOSE` |
| `TX_PS_BREW` | page-sub | `ESPRESSO · CORE PARAMETERS` | `ESPRESSO · KERNE-PARAMETRE` |
| `TX_PS_STEAM` | page-sub | `MILK · HOT WATER` | `MÆLK · VARMT VAND` |
| `TX_PS_SYSTEM` | page-sub | `DISPLAY · SOUND · LANGUAGE · TIME` | `SKÆRM · LYD · SPROG · TID` |
| `TX_PS_DIAG` | page-sub | `LINK · SENSORS · CONFIG · BOOT` | `LINK · SENSORER · CONFIG · BOOT` |

**Settings: række-labels (SL), noter (SN) og hints (SH)**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_SL_GRINDW` | - | `GRIND DOSE` | `KVÆRN-DOSIS` |
| `TX_SN_GRINDW` | note | `STOPS AUTOMATICALLY AT THIS WEIGHT` | `STOPPER AUTOMATISK VED DENNE VÆGT` |
| `TX_SL_GRINDMODE` | - | `GRIND MODE` | `KVÆRN-TILSTAND` |
| `TX_SN_GRINDMODE` | note | `MERAKI+ STARTS WEIGHT MODE ONLY` | `MERAKI+ STARTER KUN VÆGT-TILSTAND` |
| `TX_SL_BREWW` | - | `BREW WEIGHT` | `BRYG-VÆGT` |
| `TX_SN_BREWW` | note | `STOPS AUTOMATICALLY AT THIS WEIGHT` | `STOPPER AUTOMATISK VED DENNE VÆGT` |
| `TX_SL_BREWT` | - | `BREW TEMP` | `BRYG-TEMP` |
| `TX_SN_BREWT` | note | `TARGET FOR THE BREW BOILER` | `MÅL FOR BRYGKEDLEN` |
| `TX_SL_PISW` | - | `PRE-INFUSION` | `PRE-INFUSION` |
| `TX_SN_PISW` | note | `SOAKS THE PUCK BEFORE FULL PRESSURE` | `VÆDER KAFFEN FØR FULDT TRYK` |
| `TX_SL_PITIME` | - | `PRE-INF. TIME` | `PRE-INF. TID` |
| `TX_SN_PITIME` | note | `SOAK TIME BEFORE FULL PRESSURE` | `VÆDE-TID FØR FULDT TRYK` |
| `TX_SL_PIPRESS` | - | `PRE-INF. PRESSURE` | `PRE-INF. TRYK` |
| `TX_SN_PIPRESS` | note | `THE MACHINE ONLY KNOWS 3 AND 4` | `MASKINEN KENDER KUN 3 OG 4` |
| `TX_SL_BREAK` | - | `PAUSE TIME` | `PAUSE-TID` |
| `TX_SN_BREAK` | note | `NO EFFECT · PRE-INF. TIME IS USED` | `INGEN EFFEKT · PRE-INF. TID BRUGES` |
| `TX_SL_HEADT` | - | `GROUP HEAD` | `GRUPPEHOVED` |
| `TX_SN_HEADT` | note | `TARGET FOR THE GROUP HEAD` | `MÅL FOR GRUPPEHOVEDET` |
| `TX_SL_STEAMT` | - | `MILK TEMP` | `MÆLKETEMP` |
| `TX_SN_STEAMT` | note | `STEAM STOPS AT THIS MILK TEMP` | `DAMPEN STOPPER VED DENNE MÆLKETEMP` |
| `TX_SL_STEAMG` | - | `STEAM LEVEL` | `DAMP-NIVEAU` |
| `TX_SN_STEAMG` | note | `STEAM POWER 1-4` | `DAMP-STYRKE 1-4` |
| `TX_SL_HOTW` | - | `HOT WATER` | `VARMT VAND` |
| `TX_SN_HOTW` | note | `STOPS AUTOMATICALLY AT THIS WEIGHT` | `STOPPER AUTOMATISK VED DENNE VÆGT` |
| `TX_SL_HOTMODE` | - | `WATER MODE` | `VAND-TILSTAND` |
| `TX_SN_HOTMODE` | note | `CONFIG ONLY · MERAKI+ STARTS AUTO ONLY` | `KUN CONFIG · MERAKI+ STARTER KUN AUTO` |
| `TX_SL_HOTT` | - | `WATER SENSOR` | `VAND-SENSOR` |
| `TX_SN_HOTT` | note | `MEASURED ONLY · NO TARGET EXISTS` | `KUN MÅLT · INTET MÅL FINDES` |
| `TX_SL_UILANG` | - | `UI LANGUAGE` | `UI-SPROG` |
| `TX_SN_UILANG` | note | `LIVE PREVIEW · SAVE TO KEEP` | `LIVE PREVIEW · GEM FOR AT BEHOLDE` |
| `TX_SL_BL` | - | `BRIGHTNESS` | `LYSSTYRKE` |
| `TX_SN_BL` | note | `LIVE PREVIEW · 0 OMITTED (DARK SCREEN)` | `LIVE PREVIEW · 0 UDELADT (MØRK SKÆRM)` |
| `TX_SL_VOL` | - | `VOLUME` | `LYDSTYRKE` |
| `TX_SN_VOL` | note | `CONFIG ONLY · STOCK UI SOUND · 0-100 SLIDER` | `KUN CONFIG · STOCK-UI LYD · SLIDER 0-100` |
| `TX_SH_VOL` | - | `STEPS = ESTIMATE` | `TRIN = SKØN` |
| `TX_SL_TUNIT` | - | `TEMP UNIT` | `TEMP-ENHED` |
| `TX_SN_TUNIT` | note | `ALSO APPLIES IN MERAKI+` | `GÆLDER OGSÅ I MERAKI+` |
| `TX_SL_LANG` | - | `STOCK LANG.` | `STOCK-SPROG` |
| `TX_SN_LANG` | note | `STOCK UI ONLY · MERAKI+: SEE UI LANGUAGE` | `KUN STOCK-UI · MERAKI+: SE UI-SPROG` |
| `TX_SL_HOTSC` | - | `WATER SHORTCUT` | `VAND-GENVEJ` |
| `TX_SN_HOTSC` | note | `CONFIG ONLY · STOCK UI HOME SHORTCUT` | `KUN CONFIG · STOCK-UI HJEM-GENVEJ` |
| `TX_SL_DST` | - | `DAYLIGHT SAVING` | `SOMMERTID` |
| `TX_SN_CLOCK` | note | `CONFIG ONLY · STOCK UI CLOCK` | `KUN CONFIG · STOCK-UI UR` |
| `TX_SL_CLEARTIP` | - | `CLEANING REMINDER` | `RENSE-PÅMINDELSE` |
| `TX_SN_CLEARTIP` | note | `CONFIG ONLY · STOCK UI TIP` | `KUN CONFIG · STOCK-UI TIP` |
| `TX_SL_TIMEMODE` | - | `TIME FORMAT` | `TIDSFORMAT` |
| `TX_SL_AUTOTIME` | - | `AUTO TIME` | `AUTO-TID` |
| `TX_SN_AUTOTIME` | note | `CONFIG ONLY · NETWORK TIME` | `KUN CONFIG · NETVÆRKSTID` |
| `TX_SL_TZ` | - | `TIME ZONE` | `TIDSZONE` |
| `TX_SN_TZ` | note | `LOCKED · SET IT IN THE STOCK UI` | `LÅST · SÆTTES I STOCK-UI'ET` |
| `TX_SH_EST` | - | `MAX = ESTIMATE` | `MAX = SKØN` |

**Enum-navne**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_E_OFF` | - | `OFF` | `FRA` |
| `TX_E_ON` | - | `ON` | `TIL` |
| `TX_E_WEIGHT` | - | `WEIGHT` | `VÆGT` |
| `TX_E_TIME` | - | `TIME` | `TID` |
| `TX_E_AUTO` | - | `AUTO` | `AUTO` |
| `TX_E_MANUAL` | - | `MANUAL` | `MANUEL` |
| `TX_E_GENTLE` | - | `GENTLE` | `BLID` |
| `TX_E_MILD` | - | `MILD` | `MILD` |
| `TX_E_MEDIUM` | - | `MEDIUM` | `MEDIUM` |
| `TX_E_STRONG` | - | `STRONG` | `STÆRK` |
| `TX_E_24H` | - | `24-HOUR` | `24-TIMERS` |
| `TX_E_12H` | - | `12-HOUR` | `12-TIMERS` |
| `TX_LG_EN` | - | `ENGLISH` | `ENGELSK` |
| `TX_LG_DE` | - | `GERMAN` | `TYSK` |
| `TX_LG_FR` | - | `FRENCH` | `FRANSK` |
| `TX_LG_IT` | - | `ITALIAN` | `ITALIENSK` |
| `TX_LG_ES` | - | `SPANISH` | `SPANSK` |
| `TX_LG_PL` | - | `POLISH` | `POLSK` |
| `TX_LG_AR` | - | `ARABIC` | `ARABISK` |
| `TX_LG_ZHS` | - | `CHINESE SIMPL.` | `KINESISK FORENKL.` |
| `TX_LG_ZHT` | - | `CHINESE TRAD.` | `KINESISK TRAD.` |
| `TX_LG_KO` | - | `KOREAN` | `KOREANSK` |
| `TX_LG_JA` | - | `JAPANESE` | `JAPANSK` |
| `TX_UL_EN` | - | `ENGLISH` | `ENGLISH` |
| `TX_UL_DA` | - | `DANSK` | `DANSK` |

**Faser, fejlkoder og aktiviteter**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_PH_OFFLINE` | phase-chip | `OFFLINE` | `OFFLINE` |
| `TX_PH_SLEEP` | phase-chip | `ASLEEP` | `SOVER` |
| `TX_PH_ERROR` | phase-chip | `ERROR` | `FEJL` |
| `TX_PH_HEAT` | phase-chip | `HEATING UP` | `VARMER OP` |
| `TX_PH_READY` | phase-chip | `READY` | `KLAR` |
| `TX_PH_GRIND` | phase-chip | `GRINDING` | `KVÆRNER` |
| `TX_PH_PRE` | phase-chip | `PRE-INFUSION` | `PRE-INFUSION` |
| `TX_PH_EXT` | phase-chip | `EXTRACTION` | `EKSTRAKTION` |
| `TX_PH_WATER` | phase-chip | `HOT WATER` | `VARMT VAND` |
| `TX_PH_STEAM` | phase-chip | `STEAM` | `DAMP` |
| `TX_PH_DONE` | phase-chip | `DONE` | `FÆRDIG` |
| `TX_PH_STEAM_AUTO` | phase-chip | `STEAM·AUTO` | `DAMP·AUTO` |
| `TX_PH_STEAM_WAND` | phase-chip | `STEAM·WAND` | `DAMP·WAND` |
| `TX_ER_E3_BREW` | home-sub | `BREW BOILER TOO HOT` | `BRYGKEDLEN ER FOR VARM` |
| `TX_ER_E3_STEAM` | home-sub | `STEAM BOILER TOO HOT` | `DAMPKEDLEN ER FOR VARM` |
| `TX_ER_E1_GROUP` | home-sub | `GROUP HEAD TOO HOT` | `GRUPPEHOVEDET ER FOR VARMT` |
| `TX_ER_E1_MAINS` | home-sub | `PROBLEM WITH MAINS VOLTAGE` | `PROBLEM MED NETSPÆNDINGEN` |
| `TX_ER_E2_BREW` | home-sub | `BREW BOILER SENSOR FAULT` | `FEJL PÅ BRYGKEDEL-SENSOR` |
| `TX_ER_E2_GROUP` | home-sub | `GROUP HEAD SENSOR FAULT` | `FEJL PÅ GRUPPEHOVED-SENSOR` |
| `TX_ER_E2_STEAM` | home-sub | `STEAM BOILER SENSOR FAULT` | `FEJL PÅ DAMPKEDEL-SENSOR` |
| `TX_ER_E2_MILK` | home-sub | `MILK SENSOR FAULT` | `FEJL PÅ MÆLKESENSOR` |
| `TX_ER_UNKNOWN` | home-sub | `UNKNOWN FAULT` | `UKENDT FEJL` |
| `TX_A_GRIND` | - | `GRIND` | `KVÆRN` |
| `TX_A_BREW` | - | `BREW` | `BRYG` |
| `TX_A_WATER` | - | `WATER` | `VAND` |
| `TX_A_STEAM` | - | `STEAM` | `DAMP` |

**Vagter og toasts**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_G_RO` | toast | `READ-ONLY · NO WRITES` | `READ-ONLY · INGEN WRITES` |
| `TX_G_NOLINK` | toast | `NO CONNECTION` | `INGEN FORBINDELSE` |
| `TX_G_ERROR` | toast | `ERROR ACTIVE · START BLOCKED` | `FEJL AKTIV · START SPÆRRET` |
| `TX_G_SLEEP` | toast | `WAKING UP · ONE MOMENT` | `VÅGNER · ET ØJEBLIK` |
| `TX_G_BUSY` | toast | `ACTIVITY IN PROGRESS` | `AKTIVITET I GANG` |
| `TX_G_WAIT` | toast | `ONE MOMENT…` | `ET ØJEBLIK…` |
| `TX_G_NOCFG` | toast | `CONFIG MISSING · START BLOCKED` | `CONFIG MANGLER · START SPÆRRET` |
| `TX_G_OOR` | toast | `CONFIG OUT OF SPEC · FIX FIRST` | `CONFIG UDEN FOR SPEC · RET FØRST` |
| `TX_G_GRINDTIME` | toast | `GRIND = TIME · USE STOCK UI` | `KVÆRN = TID · BRUG STOCK-UI` |
| `TX_G_WATERMAN` | toast | `WATER = MANUAL · USE STOCK UI` | `VAND = MANUEL · BRUG STOCK-UI` |
| `TX_T_POLICY` | toast | `BLOCKED BY A SAFETY RULE` | `SPÆRRET AF EN SIKKERHEDSREGEL` |
| `TX_T_QFULL` | toast | `BUSY · TRY AGAIN` | `OPTAGET · PRØV IGEN` |
| `TX_T_RANGE` | toast | `OUT OF RANGE` | `UDEN FOR GRÆNSER` |
| `TX_T_CFG_UNAVAIL` | toast | `CONFIG UNAVAILABLE · NOT SAVED` | `CONFIG UTILGÆNGELIG · INTET GEMT` |
| `TX_T_NOLINK_SAVE` | toast | `NO CONNECTION · NOT SAVED` | `INGEN FORBINDELSE · INTET GEMT` |
| `TX_T_WAIT_ACT` | toast | `WAIT UNTIL THE ACTIVITY ENDS` | `VENT TIL AKTIVITET ER SLUT` |
| `TX_T_WRITE_REJ` | toast | `NOT ACCEPTED · NOT SAVED` | `IKKE ACCEPTERET · INTET GEMT` |
| `TX_T_NOACK_SAVE` | toast | `SAVE FAILED · NO REPLY` | `GEM FEJLET · INTET SVAR` |
| `TX_T_SAVED_MCU` | toast | `SAVED · CONTROLLER + CONFIG` | `GEMT · CONTROLLER + CONFIG` |
| `TX_T_SAVED_CFG` | toast | `SAVED · STOCK UI: AFTER RESTART` | `GEMT · STOCK-UI: EFTER GENSTART` |
| `TX_T_CFG_ERR` | toast | `CONFIG ERROR · NOT SAVED` | `CONFIG-FEJL · INTET GEMT` |
| `TX_T_LANG_SAVED` | toast | `LANGUAGE SAVED · ENGLISH` | `SPROG GEMT · DANSK` |
| `TX_T_LANG_NOFILE` | toast | `LANGUAGE ACTIVE · FILE NOT SAVED` | `SPROG AKTIVT · FIL IKKE GEMT` |
| `TX_T_DRYRUN` | toast | DRY RUN · SEE `cfgbak/boot_ui.log` | DRY-RUN · SE `cfgbak/boot_ui.log` |
| `TX_T_SW_FAIL_NOBIN` | toast | `SWITCH FAILED · NO STOCK BINARY` | `SKIFT FEJLEDE · INGEN STOCK-BINÆR` |
| `TX_T_SW_FAIL_FORK` | toast | `SWITCH FAILED · FORK ERROR` | `SKIFT FEJLEDE · FORK-FEJL` |
| `TX_T_SW_FAIL_DIED` | toast | `SWITCH FAILED · STOCK UI EXITED` | `SKIFT FEJLEDE · STOCK-UI STOPPEDE` |
| `TX_T_BOOT_SAVED_OURS` | toast | `SAVED · NEXT RESTART: MERAKI+` | `GEMT · NÆSTE GENSTART: MERAKI+` |
| `TX_T_BOOT_SAVED_ORIG` | toast | `SAVED · NEXT RESTART: ORIGINAL` | `GEMT · NÆSTE GENSTART: ORIGINAL` |
| `TX_T_BOOT_LOCKED` | toast | `LOCKED · FLAG NOT CHANGED` | `LÅST · FLAG IKKE ÆNDRET` |
| `TX_T_BOOT_ERR` | toast | `FLAG ERROR · NOTHING CHANGED` | `FLAG-FEJL · INTET ÆNDRET` |
| `TX_T_STOP_SENT` | toast | `STOPPING STEAM…` | `STOPPER DAMP…` |
| `TX_T_STOP_GRIND` | toast | `STOPPING GRIND…` | `STOPPER KVÆRN…` |
| `TX_T_STOP_BREW` | toast | `STOPPING BREW…` | `STOPPER BRYG…` |
| `TX_T_STOP_WATER` | toast | `STOPPING WATER…` | `STOPPER VAND…` |
| `TX_T_STOPPED_GRIND` | toast | `GRIND STOPPED` | `KVÆRN STOPPET` |
| `TX_T_STOPPED_BREW` | toast | `BREW STOPPED` | `BRYG STOPPET` |
| `TX_T_STOPPED_WATER` | toast | `WATER STOPPED` | `VAND STOPPET` |
| `TX_T_STOPPED_STEAM` | toast | `STEAM STOPPED` | `DAMP STOPPET` |
| `TX_T_STOP_NOACK` | toast | `STOP: NO REPLY · TRY AGAIN` | `STOP: INTET SVAR · PRØV IGEN` |
| `TX_T_STOP_UNCONF` | toast | `STOP NOT CONFIRMED · TRY AGAIN` | `STOP IKKE BEKRÆFTET · PRØV IGEN` |
| `TX_T_STOP_FAIL` | toast | `STOP NOT SENT · TRY AGAIN` | `STOP IKKE SENDT · PRØV IGEN` |
| `TX_T_PW_BUSY` | toast | `STOP THE ACTIVITY FIRST · THEN OFF` | `STOP AKTIVITETEN FØRST · SÅ SLUK` |
| `TX_T_PW_NOLINK` | toast | `NO CONNECTION · CANNOT TURN OFF` | `INGEN FORBINDELSE · SLUKKER IKKE` |
| `TX_T_PW_NOACK` | toast | `POWER OFF: NO REPLY · TRY AGAIN` | `SLUK: INTET SVAR · PRØV IGEN` |
| `TX_T_PW_ON` | toast | `MACHINE IS ON` | `MASKINEN ER TÆNDT` |
| `TX_T_HOLD_HINT` | toast | `HOLD THE BUTTON TO START` | `HOLD KNAPPEN NEDE FOR START` |
| `TX_T_PINFO` | - | `PRE-INF. %d BAR = RAW %d · LINEAR` | `PRÆ-INF. %d BAR = RÅ %d · LINEÆR` |
| `TX_T_NOACK` | toast | `THE MACHINE DID NOT ANSWER` | `MASKINEN SVAREDE IKKE` |
| `TX_T_NOSTART` | toast | `NO START · CHECK THE MACHINE` | `INGEN START · TJEK MASKINEN` |

**HJEM (labels, underlinjer, hold-hints, stats, chart, knapper)**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_H_DOSE_TGT` | home-label | `DOSE · TARGET %s G` | `DOSIS · MÅL %s G` |
| `TX_H_CUP_TGT` | home-label | `IN THE CUP · TARGET %s G` | `I KOPPEN · MÅL %s G` |
| `TX_H_MILK_TGT` | home-label | `MILK · TARGET %s°` | `MÆLK · MÅL %s°` |
| `TX_H_MILK_PROBE` | home-label | `MILK PROBE · NO TARGET` | `MÆLKEPROBE · INTET MÅL` |
| `TX_H_BOILER` | home-label | `BREW BOILER · NOW` | `BRYGKEDEL · NU` |
| `TX_H_CRC` | - | `CRC %u` | `CRC %u` |
| `TX_H_DONE_IN` | home-sub | `DONE IN %s` | `FÆRDIG PÅ %s` |
| `TX_H_AUTOSTOP_G` | home-sub | `AUTO-STOP AT %s G · OR STOP` | `AUTO-STOP VED %s G · ELLER STOP` |
| `TX_H_AUTOSTOP_T` | home-sub | `AUTO-STOP AT %s° · OR STOP` | `AUTO-STOP VED %s° · ELLER STOP` |
| `TX_H_WAND_NOSTOP` | home-sub | `NO AUTO-STOP · RELEASE WAND` | `INGEN AUTO-STOP · SLIP WAND` |
| `TX_H_DONE_RISE` | home-sub | `DONE IN %s · RISES ≈%s° MORE` | `FÆRDIG PÅ %s · STIGER ≈%s° TIL` |
| `TX_H_NORESP` | home-sub | `NO CONNECTION TO THE MACHINE` | `INGEN FORBINDELSE TIL MASKINEN` |
| `TX_H_WAKING` | home-sub | `WAKING UP…` | `VÅGNER…` |
| `TX_H_HEATING_TO` | home-sub | `HEATING TO %s°` | `PÅ VEJ MOD %s°` |
| `TX_H_SET_TO` | home-sub | `TARGET %s°` | `MÅL %s°` |
| `TX_H_STARTING` | home-sub | `STARTING %s…` | `STARTER %s…` |
| `TX_H_HOLD_GRIND` | home-sub | `HOLD · GRIND %s G` | `HOLD · KVÆRN %s G` |
| `TX_H_HOLD_BREW` | home-sub | `HOLD · BREW %s G` | `HOLD · BRYG %s G` |
| `TX_H_HOLD_WATER` | home-sub | `HOLD · HOT WATER %s G` | `HOLD · VARMT VAND %s G` |
| `TX_H_HOLD_STEAM` | home-sub | `HOLD · STEAM TO %s°` | `HOLD · DAMP TIL %s°` |
| `TX_ST_TIME` | - | `TIME` | `TID` |
| `TX_ST_BAR` | - | `≈BAR EST.` | `≈BAR EST.` |
| `TX_ST_PRAW` | - | `RAW` | `RÅ` |
| `TX_ST_PULSE` | - | `PULSE` | `PULS` |
| `TX_ST_FLOW` | - | `FLOW` | `FLOW` |
| `TX_ST_CURRENT` | - | `CURRENT` | `STRØM` |
| `TX_ST_GRINDSTEP` | - | `STEP` | `TRIN` |
| `TX_ST_BOILER` | - | `BOILER` | `KEDEL` |
| `TX_ST_STEAMBOILER` | - | `STEAM` | `DAMP` |
| `TX_ST_LEVEL` | - | `LEVEL` | `NIVEAU` |
| `TX_ST_GROUP` | - | `GROUP` | `GRUPPE` |
| `TX_ST_WEIGHT` | - | `WEIGHT` | `VÆGT` |
| `TX_ST_MILK` | - | `MILK` | `MÆLK` |
| `TX_C_HINT` | - | `HOLD AN ICON TO START` | `HOLD ET IKON FOR AT STARTE` |
| `TX_C_HINT2` | - | `SWIPE FOR SETTINGS` | `STRYG FOR INDSTILLINGER` |
| `TX_C_ERRCODE` | - | `CODE %s · SEE DIAGNOSTICS` | `KODE %s · SE DIAGNOSE` |
| `TX_C_DIAGREF` | - | `DETAILS IN DIAGNOSTICS` | `DETALJER UNDER DIAGNOSE` |
| `TX_C_PRESS` | - | `PRESSURE` | `TRYK` |
| `TX_C_WEIGHT` | - | `WEIGHT` | `VÆGT` |
| `TX_C_MILK` | - | `MILK TEMP` | `MÆLKETEMP` |
| `TX_C_TARGET` | - | `- - TARGET` | `- - MÅL` |
| `TX_C_RAW` | - | `RAW` | `RÅ` |
| `TX_B_STOP_STEAM` | - | `STOP STEAM` | `STOP DAMP` |
| `TX_B_STOP_GRIND` | - | `STOP GRIND` | `STOP KVÆRN` |
| `TX_B_STOP_BREW` | - | `STOP BREW` | `STOP BRYG` |
| `TX_B_STOP_WATER` | - | `STOP WATER` | `STOP VAND` |

**Knapper**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_B_BACK` | button | `BACK` | `TILBAGE` |
| `TX_B_CANCEL` | button | `CANCEL` | `ANNULLER` |
| `TX_B_SAVE` | button | `SAVE` | `GEM` |
| `TX_B_SAVING` | button | `SAVING` | `GEMMER` |
| `TX_B_SWITCH` | button | `SWITCH NOW` | `SKIFT NU` |
| `TX_B_EXIT` | button | `EXIT` | `AFSLUT` |

**SYSTEM: opstart / UI-valg (BOOT-UI, låse-årsager, hjælpetekster)**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_X_CAP` | full-line | `STARTUP · UI CHOICE` | `OPSTART · UI-VALG` |
| `TX_X_BOOTUI` | - | `BOOT UI` | `BOOT-UI` |
| `TX_X_SWITCH` | - | `SWITCH TO ORIGINAL NOW` | `SKIFT TIL ORIGINAL NU` |
| `TX_X_SWITCH_SUB` | row-sub | `IMMEDIATE · NO RESTART` | `STRAKS · UDEN GENSTART` |
| `TX_BN_ORIGINAL` | - | `ORIGINAL` | `ORIGINAL` |
| `TX_BN_OURS` | - | `MERAKI+` | `MERAKI+` |
| `TX_BN_SLOTB` | - | `SLOT B` | `SLOT B` |
| `TX_BL_NOMOUNT` | row-sub | `LOCKED · SLOT B NOT MOUNTED` | `LÅST · SLOT B IKKE MOUNTET` |
| `TX_BL_NOMOUNT_L` | note | `/new_customer` IS NOT MOUNTED | `/new_customer` ER IKKE MOUNTET |
| `TX_BL_NOBIN` | row-sub | LOCKED · SLOT B HAS NO `demo_v9` | LÅST · SLOT B UDEN `demo_v9` |
| `TX_BL_NOBIN_L` | note | `/new_customer/demo_v9` IS MISSING | `/new_customer/demo_v9` MANGLER |
| `TX_BL_VENDOR` | row-sub | `LOCKED · SLOT B IS NOT MERAKI+` | `LÅST · SLOT B ER IKKE MERAKI+` |
| `TX_BL_VENDOR_L` | note | `/new_customer/demo_v9` IS NOT MERAKI+ | `/new_customer/demo_v9` ER IKKE MERAKI+ |
| `TX_BS_OURS` | - | `NOW: MERAKI+ (B) AT BOOT` | `NU: MERAKI+ (B) VED BOOT` |
| `TX_BS_ORIG` | - | `NOW: ORIGINAL (A) AT BOOT` | `NU: ORIGINAL (A) VED BOOT` |
| `TX_BS_SLOTB` | - | `NOW: SLOT B (NOT MERAKI+) AT BOOT` | `NU: SLOT B (IKKE MERAKI+) VED BOOT` |
| `TX_BH1` | row-sub | `APPLIES AT NEXT RESTART` | `GÆLDER NÆSTE GENSTART` |
| `TX_BH2` | foot-1 | `OTA MAY OVERWRITE THIS` | `OTA KAN OVERSKRIVE DETTE` |
| `TX_BH3` | foot-2 | CHOICE — SEE BOOT`.md` §7 | VALG — SE BOOT`.md` §7 |
| `TX_SW_LOCK` | row-sub | `LOCKED · STOCK BINARY MISSING` | `LÅST · STOCK-BINÆR MANGLER` |
| `TX_SW_LOCK_L` | note | `/customer/demo_v9` IS MISSING | `/customer/demo_v9` MANGLER |

**Stepper-detalje**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_D_MEASURED` | - | `MEASURED VALUE · NOT A SETTING` | `MÅLT VÆRDI · INGEN INDSTILLING` |
| `TX_D_NOEDIT` | - | `CANNOT BE CHANGED HERE` | `KAN IKKE ÆNDRES HER` |
| `TX_D_NOPTS` | - | `%d OPTIONS` | `%d VALG` |
| `TX_D_LIVE` | foot-1 | `LIVE FROM CONTROLLER` | `LIVE FRA CONTROLLER` |
| `TX_D_SAMESENSOR` | foot-2 | `SAME SENSOR AS STOCK UI` | `SAMME SENSOR SOM STOCK-UI` |
| `TX_D_FROMCFG` | foot-1 | `SHOWN FROM CONFIG.JSON` | `VISES FRA CONFIG.JSON` |
| `TX_D_STOCKEDIT` | foot-2 | `EDITOR ONLY IN STOCK UI` | `EDITOR KUN I STOCK-UI` |
| `TX_D_MCU_CFG` | foot-1 | `CONTROLLER NOW + CONFIG` | `CONTROLLER STRAKS + CONFIG` |
| `TX_D_CFGONLY` | foot-1 | `CONFIG.JSON ONLY` | `KUN CONFIG.JSON` |
| `TX_D_STOCK_RESTART` | foot-2 | `STOCK UI: AFTER RESTART` | `STOCK-UI: EFTER GENSTART` |
| `TX_D_LANG_L1` | foot-1 | MERAKI+ ONLY · `ourui/lang` | KUN MERAKI+ · `ourui/lang` |
| `TX_D_LANG_L2` | foot-2 | `STOCK UI LANGUAGE UNCHANGED` | `STOCK-UI-SPROG UÆNDRET` |
| `TX_D_SAVING` | status | `SAVING…` | `GEMMER…` |
| `TX_D_OOR` | status | `OUT OF SPEC · PICK A NEW VALUE` | `UDEN FOR SPEC · VÆLG NY VÆRDI` |
| `TX_D_CHANGED` | - | `CHANGED · WAS %s %s` | `ÆNDRET · FØR %s %s` |
| `TX_D_SENSOR_LIVE` | status | `SENSOR · LIVE` | `SENSOR · LIVE` |
| `TX_D_LOCKED` | status | `LOCKED` | `LÅST` |
| `TX_D_SAVEDVAL` | status | `SAVED VALUE` | `GEMT VÆRDI` |

**System-handlinger: skift-bekræftelse, exit-påmindelse, farvel-skærm**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_Y_CHANGED` | - | `CHANGED · WAS %s` | `ÆNDRET · FØR %s` |
| `TX_Y_SAVEDCHOICE` | status | `SAVED CHOICE` | `GEMT VALG` |
| `TX_Y_FLAG_YES` | foot-1 | FLAG `/customer/new` EXISTS | FLAG `/customer/new` FINDES |
| `TX_Y_FLAG_NO` | foot-1 | NO FLAG `/customer/new` | INTET FLAG `/customer/new` |
| `TX_Y_FLAG_HANDSOFF` | foot-2 | `FLAG IS NOT TOUCHED HERE` | `FLAGET RØRES IKKE HERFRA` |
| `TX_Y_NEXT` | - | `NEXT RESTART: %s` | `NÆSTE GENSTART: %s` |
| `TX_Y_NOSTOCK` | foot-1 | `NO STOCK UI TO START` | `INTET STOCK-UI AT STARTE` |
| `TX_Y_STAYS` | foot-2 | `MERAKI+ KEEPS RUNNING` | `MERAKI+ BLIVER KØRENDE` |
| `TX_Y_CLOSE_CONT` | status | `MERAKI+ CLOSES · STOCK UI RESUMES` | `MERAKI+ LUKKER · STOCK-UI FORTSÆTTER` |
| `TX_Y_CLOSE_START` | status | `MERAKI+ CLOSES · STOCK UI STARTS` | `MERAKI+ LUKKER · STOCK-UI STARTER` |
| `TX_Y_REMINDER_FOLLOWS` | note | `CONFIG CHANGED · REMINDER FOLLOWS` | `CONFIG ÆNDRET · PÅMINDELSE FØLGER` |
| `TX_Y_PAUSED_PID` | note | `STOCK UI IS PAUSED · PID %d` | `STOCK-UI ER PAUSET · PID %d` |
| `TX_Y_RUNSH` | foot-1 | `RUN.SH MODE · CLEAN EXIT` | `RUN.SH-TILSTAND · REN EXIT` |
| `TX_Y_BACK_RUNSH` | foot-2 | `BACK HERE: RUN.SH` | `TILBAGE HERTIL: RUN.SH` |
| `TX_Y_FRESH` | note | `FRESH START · READS CURRENT CONFIG` | `FRISK START · LÆSER AKTUEL CONFIG` |
| `TX_Y_BACK_GESTURE1` | foot-1 | `BACK HERE: 3 LONG PRESSES` | `TILBAGE HERTIL: 3 LANGE TRYK` |
| `TX_Y_BACK_GESTURE2` | foot-2 | `TOP LEFT CORNER (1.5 S)` | `ØVERST TIL VENSTRE (1.5 S)` |
| `TX_Y_NOSWITCHER` | foot-1 | `SWITCHER DAEMON NOT RUNNING` | `SWITCHER-DAEMON KØRER IKKE` |
| `TX_Y_BACK_RESTART` | foot-2 | `BACK HERE: RESTART` | `TILBAGE HERTIL: GENSTART` |
| `TX_Y_BACK_TELNET` | foot-2 | `BACK HERE: TELNET ONLY` | `TILBAGE HERTIL: KUN TELNET` |
| `TX_Y_EXIT_TITLE` | - | `CONFIG HAS CHANGED` | `CONFIG ER ÆNDRET` |
| `TX_Y_EXIT_SUB` | - | `STOCK UI HAS OLD VALUES` | `STOCK-UI HAR GAMLE VÆRDIER` |
| `TX_Y_EXIT_NOTE` | note | `RUN VIA TELNET · WATCHDOG RESTARTS IT` | `KØR VIA TELNET · WATCHDOG STARTER DEN IGEN` |
| `TX_Y_EXIT_F1` | foot-1 | `OTHERWISE STOCK UI SENDS` | `ELLERS SENDER STOCK-UI` |
| `TX_Y_EXIT_F2` | foot-2 | `OLD VALUES WHEN BREWING` | `GAMLE VÆRDIER VED BRYG` |
| `TX_Y_EXIT_L1` | - | `STOCK UI NEEDS A RESTART OF` | `STOCK-UI KRÆVER GENSTART AF` |
| `TX_Y_EXIT_L2` | - | `demo_v9` FOR NEW VALUES | `demo_v9` FOR NYE VÆRDIER |
| `TX_Y_EXIT_CMD` | - | `kill $(pidof demo_v9)` | `kill $(pidof demo_v9)` |
| `TX_Y_BYE_T` | - | `SWITCHING TO ORIGINAL` | `SKIFTER TIL ORIGINAL` |
| `TX_Y_BYE_CONT` | - | `STOCK UI RESUMES` | `STOCK-UI FORTSÆTTER` |
| `TX_Y_BYE_START` | - | `STOCK UI STARTING · ONE MOMENT` | `STOCK-UI STARTER · ET ØJEBLIK` |
| `TX_Y_BYE_CLOSED` | - | `MERAKI+ HAS CLOSED` | `MERAKI+ ER AFSLUTTET` |

**Øvrige**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_PW_OFFING` | - | `TURNING OFF…` | `SLUKKER…` |
| `TX_PW_OFF` | - | `OFF` | `SLUKKET` |
| `TX_PW_WAKING` | - | `TURNING ON…` | `TÆNDER…` |
| `TX_PW_SUB_OFFING` | - | `THE MACHINE GOES TO SLEEP` | `MASKINEN GÅR I DVALE` |
| `TX_PW_SUB_OFF` | - | `HOLD THE POWER BUTTON TO TURN ON` | `HOLD KNAPPEN INDE FOR AT TÆNDE` |
| `TX_PW_SUB_WAKING` | - | `KEEP HOLDING THE BUTTON A MOMENT` | `HOLD KNAPPEN INDE ET ØJEBLIK` |
| `TX_PW_NOTE_OFF` | - | `THE SCREEN TURNS OFF · TOUCH TO SHOW` | `SKÆRMEN SLUKKER · RØR FOR AT VISE` |

**DIAGNOSE (labels, værdier, fuldbredde-linjer, OM-blok)**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_DL_LINK` | - | `LINK` | `LINK` |
| `TX_DL_PHASE` | - | `PHASE` | `FASE` |
| `TX_DL_RXOK` | - | `RX OK` | `RX OK` |
| `TX_DL_RXCRC` | - | `RX CRC / RESYNC` | `RX CRC / RESYNC` |
| `TX_DL_WORKSTA` | - | `WORK_STA` | `WORK_STA` |
| `TX_DL_E123` | - | `E1 / E2 / E3` | `E1 / E2 / E3` |
| `TX_DL_CMD6` | - | `CMD6 OK·RTX·ERR·BLK` | `CMD6 OK·RTX·FEJL·BLOK` |
| `TX_DL_AWAKE` | - | `AWAKE (WORD 1)` | `VÅGEN (ORD 1)` |
| `TX_DL_MAINS` | - | `MAINS` | `NET` |
| `TX_DL_CURRENT` | - | `CURRENT 1 / 2` | `STRØM 1 / 2` |
| `TX_DL_FRAMEMS` | - | `FRAME MS NOW/MAX` | `FRAME MS NU/MAX` |
| `TX_DL_MIRROR` | - | `MIRROR 112-114` | `SPEJL 112-114` |
| `TX_DL_CFGMD5` | - | `CONFIG MD5` | `CONFIG MD5` |
| `TX_DL_VERSION` | - | `VERSION` | `VERSION` |
| `TX_DL_BP_A` | - | `BOOT PUSH 1-30` | `BOOT-PUSH 1-30` |
| `TX_DL_BP_B` | - | `BOOT PUSH 112-114` | `BOOT-PUSH 112-114` |
| `TX_DL_BP_A_RE` | - | `RE-PUSH %d · 1-30` | `RE-PUSH %d · 1-30` |
| `TX_DL_BP_B_RE` | - | `112-114` | `112-114` |
| `TX_DL_FLAG` | diag-label | FLAG `/customer/new` | FLAG `/customer/new` |
| `TX_DL_NEXTBOOT` | diag-label | `NEXT BOOT` | `NÆSTE BOOT` |
| `TX_DL_STARTED` | diag-label | `STARTED FROM` | `STARTET FRA` |
| `TX_DL_SLOTB` | diag-label | `SLOT B` | `SLOT B` |
| `TX_DL_SWITCHER` | diag-label | `SWITCHER DAEMON` | `SWITCHER-DAEMON` |
| `TX_DL_STOCK` | diag-label | `STOCK PROCESS` | `STOCK-PROCES` |
| `TX_DV_OK` | diag-value | `OK` | `OK` |
| `TX_DV_DOWN` | diag-value | `DOWN` | `NEDE` |
| `TX_DV_YES` | diag-value | `YES` | `JA` |
| `TX_DV_NO` | diag-value | `NO` | `NEJ` |
| `TX_DV_MAINS` | - | `%u V · %u HZ` | `%u V · %u HZ` |
| `TX_DV_VERSION` | - | `MERAKI+ V6.14` | `MERAKI+ V6.14` |
| `TX_DV_VERSION_RO` | - | `MERAKI+ V6.14 RO` | `MERAKI+ V6.14 RO` |
| `TX_DV_MISSING` | diag-value | `MISSING` | `MANGLER` |
| `TX_DV_FLAG_YES` | diag-value | `EXISTS` | `FINDES` |
| `TX_DV_FLAG_NO` | diag-value | `NOT PRESENT` | `FINDES IKKE` |
| `TX_DV_SLOTA` | diag-value | `SLOT A` | `SLOT A` |
| `TX_DV_SLOTB` | diag-value | `SLOT B` | `SLOT B` |
| `TX_DV_RUNSH` | diag-value | `RUN.SH` | `RUN.SH` |
| `TX_DV_UNKNOWN` | diag-value | `UNKNOWN` | `UKENDT` |
| `TX_DV_B_OK` | diag-value | `MERAKI+ FOUND` | `MERAKI+ FUNDET` |
| `TX_DV_B_NOMOUNT` | diag-value | `NOT MOUNTED` | `IKKE MOUNTET` |
| `TX_DV_B_NOBIN` | diag-value | `NO DEMO_V9` | `UDEN DEMO_V9` |
| `TX_DV_B_VENDOR` | diag-value | `NOT MERAKI+` | `IKKE MERAKI+` |
| `TX_DV_RUNNING` | diag-value | `RUNNING` | `KØRER` |
| `TX_DV_NOTRUNNING` | diag-value | `NOT RUNNING` | `KØRER IKKE` |
| `TX_DV_NONE` | diag-value | `NONE` | `INGEN` |
| `TX_DV_ALSO` | diag-value | `ALSO RUNNING!` | `KØRER SAMTIDIG!` |
| `TX_DV_PAUSED` | diag-value | `PAUSED` | `PAUSET` |
| `TX_DV_BP_OFF` | diag-value | `OFF` | `FRA` |
| `TX_DV_BP_WAIT` | diag-value | `WAITING` | `VENTER` |
| `TX_DV_BP_SENT` | diag-value | `SENDING` | `SENDER` |
| `TX_DV_BP_NOACK` | diag-value | `NO ACK` | `INGEN ACK` |
| `TX_DV_BP_BLOCKED` | diag-value | `REJECTED` | `AFVIST` |
| `TX_DV_BP_NOCFG` | diag-value | `NO CONFIG` | `INGEN CONFIG` |
| `TX_DV_BP_SKIP` | diag-value | `SKIPPED` | `SPRUNGET OVER` |
| `TX_DG_CAP` | full-line | `BOOT · SLOT · SWITCHER` | `BOOT · SLOT · SWITCHER` |
| `TX_DG_L_PID` | full-line | `PID · MERAKI+ / STOCK UI / SWITCHER` | `PID · MERAKI+ / STOCK-UI / SWITCHER` |
| `TX_DG_L_EXE` | full-line | RUNNING FROM · `/proc/self/exe` | KØRER FRA · `/proc/self/exe` |
| `TX_DG_L_EVT` | full-line | LAST SWITCH · `/tmp/switcher.log` | SENESTE SKIFT · `/tmp/switcher.log` |
| `TX_DG_L_NET` | full-line | `NETWORK · WPA PID / IP / STATE` | `NETVÆRK · WPA-PID / IP / TILSTAND` |
| `TX_NS_OFF` | - | `OFF` | `FRA` |
| `TX_NS_DOWN` | - | `DOWN` | `NEDE` |
| `TX_NS_STARTING` | - | `STARTING` | `STARTER` |
| `TX_NS_CONNECTING` | - | `CONNECTING` | `FORBINDER` |
| `TX_NS_UP` | - | `UP` | `OPPE` |
| `TX_DG_NOEXE` | full-line | `NOT READABLE` | `IKKE LÆSBAR` |
| `TX_DG_NOEVT` | full-line | `NO SWITCH EVENT IN THE LOG` | `INGEN SKIFT-HÆNDELSE I LOGGEN` |
| `TX_DG_NOLOG` | full-line | `LOG NOT READABLE` | `LOG IKKE LÆSBAR` |
| `TX_DG_L_GRAPH` | full-line | `GRAPH · TAP TO TOGGLE` | `GRAF · TRYK FOR AT SKIFTE` |
| `TX_DV_G_SMOOTH` | full-line | `SMOOTH · MEDIAN 3 + EMA` | `GLAT · MEDIAN 3 + EMA` |
| `TX_DV_G_RAW` | full-line | `RAW · UNFILTERED` | `RÅ · UFILTRERET` |
| `TX_DG_PCAP` | full-line | `PRESSURE · ESTIMATE` | `TRYK · ESTIMAT` |
| `TX_DG_L_PANCH` | full-line | `ANCHOR · PRE-INF. IN RECORDING` | `ANKER · PRÆ-INF. I OPTAGELSEN` |
| `TX_DV_PANCH` | - | %d BAR = RAW %d · `k` %.5f | %d BAR = RÅ %d · `k` %.5f |
| `TX_DG_L_PNOW` | full-line | `NOW · WORD 51 · PUMP SIDE, LINEAR` | `NU · ORD 51 · PUMPE-SIDE, LINEÆR` |
| `TX_DV_PNOW` | - | `RAW %d = ≈%.1f BAR` | `RÅ %d = ≈%.1f BAR` |
| `TX_DV_PPULSE` | - | `RAW %d · PULSE, NO PRESSURE` | `RÅ %d · PULS, IKKE TRYK` |
| `TX_DV_PMAINT` | - | `RAW %d · MAINTENANCE PULSE` | `RÅ %d · VEDLIGEHOLD-PULS` |
| `TX_DG_PNOTE1` | full-line | `ESTIMATE · PUMP SIDE · NOT AT THE PUCK` | `ESTIMAT · PUMPE-SIDE · IKKE VED PUCKEN` |
| `TX_DG_PNOTE2` | - | `> %d BAR = PULSE · RAW: GRAPH PILL` | `> %d BAR = PULS · RÅ: GRAF-PILLEN` |
| `TX_DG_ABOUT` | full-line | `ABOUT` | `OM` |
| `TX_DG_RAWCAP` | full-line | `RAW COUNTERS · SERVICE` | `RÅ TÆLLERE · SERVICE` |
| `TX_DG_L_LANG` | full-line | UI LANGUAGE · `ourui/lang` | UI-SPROG · `ourui/lang` |
| `TX_DG_LANG_DEFAULT` | - | `%s · DEFAULT (NO FILE)` | `%s · STANDARD (INGEN FIL)` |
| `TX_DG_LANG_FILE` | - | `%s · FROM FILE` | `%s · FRA FIL` |
| `TX_DG_LANG_UNKNOWN` | - | `%s · UNKNOWN FILE VALUE` | `%s · UKENDT VÆRDI I FIL` |
| `TX_DG_LANG_SESSION` | - | `%s · NOT SAVED` | `%s · IKKE GEMT` |
| `TX_DG_PWCAP` | full-line | `POWER BUTTON` | `TÆND/SLUK-KNAP` |
| `TX_DG_L_PWNODE` | full-line | `BUTTON NODE · EVDEV · NO GRAB` | `KNAP-NODE · EVDEV · INTET GRAB` |
| `TX_DL_PW_STATE` | diag-label | `STATE` | `TILSTAND` |
| `TX_DL_PW_W0` | diag-label | `POWER · WORD 0` | `STRØM · ORD 0` |
| `TX_DL_PW_COUNT` | diag-label | `PRESS · OFF · ON` | `TRYK · SLUK · TÆND` |
| `TX_DL_PW_LAST` | diag-label | `LAST KEY` | `SIDSTE KNAP` |
| `TX_DV_PW_ON` | diag-value | `ON` | `TÆNDT` |
| `TX_DV_PW_OFFING` | diag-value | `TURNING OFF` | `SLUKKER` |
| `TX_DV_PW_OFF` | diag-value | `OFF` | `SLUKKET` |
| `TX_DV_PW_WAKING` | diag-value | `TURNING ON` | `TÆNDER` |
| `TX_DV_PW_NOREPLY` | diag-value | `NO REPLY` | `INTET SVAR` |
| `TX_DV_PW_KEY` | - | `%d = %d · %ld S` | `%d = %d · %ld S` |
| `TX_DV_PW_NOKEY` | diag-value | `NO PRESS YET` | `INGEN TRYK` |
| `TX_DV_PW_NONODE` | full-line | `NO BUTTON NODE FOUND` | `INGEN KNAP-NODE FUNDET` |

**Konsol (stdout ved exit; ASCII)**

| id | klasse | EN | DA |
|---|---|---|---|
| `TX_O_SWITCHED` | console | `== ourui: runtime switch B->A (user button): stock UI pid %d started from /customer/demo_v9; ourui exits\n` | `== ourui: runtime-skift B->A (bruger-knap): stock-UI pid %d startet fra /customer/demo_v9; ourui afslutter\n` |
| `TX_O_RESTART` | console | `== ourui: STOCK UI NEEDS A RESTART OF demo_v9 FOR NEW VALUES:   kill $(pidof demo_v9)\n==        (config.json changed by ourui; the watchdog restarts it, the boot push syncs the controller)\n` | `== ourui: STOCK-UI KRAEVER GENSTART AF demo_v9 FOR NYE VAERDIER:   kill $(pidof demo_v9)\n==        (config.json aendret af ourui; watchdog genstarter, boot-push synker controlleren)\n` |
| `TX_O_CFGCHANGED` | console | `== ourui: config.json changed %d time(s) - no paused stock UI is waiting; the next stock start reads it itself\n` | `== ourui: config.json aendret %d gang(e) - intet pauset stock-UI venter; naeste stock-start laeser den selv\n` |
| `TX_O_BLOCKED` | console | `== ourui: %ld cmd6 frame(s) rejected by the frame policy (see UI_v6.md)\n` | `== ourui: %ld cmd6-frame(s) afvist af frame-politikken (se UI_v6.md)\n` |

<!-- I18N-APPENDIX:END -->
