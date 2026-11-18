# Tryk-estimat, graf- og vægt-konditionering (kalibrerings-note)

**Kort:** Meraki+ viser trykket som et **lineært estimat** (≈bar · EST.), forankret i ét punkt:
pre-infusion-plateauet i en UART-optagelse (rå 358) sættes lig med config-værdien
`preInfusionPressure` (standard 4 bar). Det er **ikke** en kalibrering — kun en fysisk måling
(manometer på en blind-kurv) kan give det. Rå-værdien er altid tilgængelig under
*DIAGNOSE → GRAF = RÅ*. Graf og vægt filtreres kun i *visningen*; al styring, auto-stop og alle
sikkerhedsvagter bruger de rå værdier.

Uddraget af [dev/UI_v6.md](dev/UI_v6.md) §14 og §16. Hjælp gerne med en ægte kalibrering: mål
trykket med et manometer og åbn en issue med rå-værdierne fra DIAGNOSE.

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
