# Changelog

Alle væsentlige ændringer i Meraki+. Detaljer (med beviser, frames og test-tal) står i
[docs/dev/UI_v6.md](docs/dev/UI_v6.md) under *CHANGES-v6.x*. Datoer er 2026.

## [6.14] — 2026-11-18 — første offentlige udgave
### Tilføjet
- **Den fysiske tænd/sluk-knap**: knap-noden (k62091, `event1`) læses read-only; hold 1,5 s →
  stock's sluk-sekvens (cmd8 tid/alarm + ord 0 = 0) byte for byte; SLUKKER… / SLUKKET / TÆNDER…-visning;
  tænd sender PowerOn + vågen som ved opstart. DIAGNOSE-blok *TÆND/SLUK-KNAP*.
- **Offentlig pakke**: `install.sh` / `revert.sh` (tools/merakiplus.py) med fingeraftryk, md5-gates,
  engangs-backups, atomisk install, aktivering uden genstart, dry-run og idempotens; mock-skærm og
  installer-tests; CI; dokumentation og ansvarsfraskrivelse.
### Kendt begrænsning
- På den testede hardware skærer kernel/PMIC skærmens strøm efter ~0,8 s knap-hold, og controllerens
  dvale slukker skærmen ~8 ms efter sluk-framen. Sluk = strømsvigt for skærmen, tænd = kold opstart
  (se [docs/power.md](docs/power.md)).

## [6.13] — 2026-11-18
- **Manuel STOP for VAND, BRYG og KVÆRN** (før kun damp). Stop-frames reverset fra MCU-firmwaren og
  krydstjekket mod stock; feedback-toasts (stopper… / stoppet / intet svar / ikke bekræftet).

## [6.12] — 2026-11-18
- **Gestus-skifter**: én center-hold ≥ 5 s (radius 40 px) i stock-UI'et skifter til Meraki+ uden
  genstart (før: 3 × hjørne-hold). Grab-bypass (`grab_shim.so`, kun i stock-processen), automatisk
  touch-område, fallback-gestus `SWITCHER_GESTURE=corner`.

## [6.11] — 2026-11-17
- **UX-review**: klart sprog (ingen ord-numre i brugertekster), bedre kontrast, start-hint, tandhjul
  til indstillinger, luk-chevron og stryg-ned for at fortryde, swipe-vagt, DIAGNOSE i grupper.

## [6.10] — 2026-11-17
- **Tryk som ≈bar-estimat** i stedet for "ukalibreret": lineært anker (pre-infusion 358 rå =
  config `preInfusionPressure`); pulser > 20 bar vises som *PULS*. Se [docs/calibration.md](docs/calibration.md).

## [6.9] — 2026-11-17
- **Rolig graf og stabil vægt**: median-3 + EMA, puls-gate, p90-skala, stabil vægt-kilde (latch),
  vægt-dødbånd ±0,25 g. Kun visning — styring bruger rå værdier.

## [6.8] — 2026-11-17
- REVERT-knappen på HJEM fjernet (udvikler-sikring); vejen tilbage er *SYSTEM → SKIFT TIL ORIGINAL NU*.

## [6.7] — 2026-11-17
- **Net-vagt**: Wi-Fi bringes op ved opstart fra slot B (stock-UI'et gjorde det selv) og helbredes
  automatisk; adgangskoden logges aldrig. Ord 113 med i boot-pushen.

## [6.6] — 2026-11-17
- Fase-logik efter live-optagelse (to damp-veje, samtidige faser); **64-bit ur** (32-bit-uret løb
  rundt efter 24,86 døgn); re-push af config når controlleren genstarter.

## [6.5] — 2026-11-17
- Meraki+ boot-pusher selv config til controlleren (også på enheden); RX-parseren kan ikke længere gå
  i stå (resync-watchdog); nye DIAGNOSE-felter.

## [6.4] — 2026-11-17
- **SIM-build og E2E**: UI'et kører uden hardware mod en simuleret controller; E2E-suiten grøn på
  engelsk og dansk.

## [6.3] — 2026-11-17
- Produktnavnet **Meraki+**, splash og ordmærke; al tekst på **engelsk og dansk** (*UI-SPROG*).

## [6.2] — 2026-11-17
- **Boot-slot-integration**: *BOOT-UI* (vælg hvad der starter) og *SKIFT TIL ORIGINAL NU* (runtime-skift
  uden genstart); DIAGNOSE-boot-blok.

## [6.1] — 2026-11-17
- **Indstillings-paritet**: grænser, trin og labels afstemt med stock-UI'et (adresse-bevist), tiers,
  låste/estimerede felter markeret.

## [6.0] — 2026-11-16
- Første v6: design til den runde skærm (egen anti-aliaset font, gauge, toasts, splash), 24
  indstillinger med atomisk `config.json`-gem + backup, KVÆRN/BRYG/VAND/DAMP med hold-for-start,
  write-kø med CRC + ACK + retransmit og frame-politik, keep-awake-rettelse (ord 1 = 0), faser inkl.
  damp/sover/fejl/offline.
