# Bidrag til Meraki+ / Contributing

Tak fordi du vil hjælpe! Issues og pull requests er velkomne på dansk eller engelsk.
*Contributions are welcome in Danish or English.*

## Sikkerhedsregler (ufravigelige)

1. **Ingen skrivning til en maskine uden backup og revert.** Enhver ny device-handling skal tage
   md5-verificeret backup først, være atomisk (tmp-fil på samme filsystem → md5 → `mv`), være
   idempotent og kunne fortrydes af `./revert.sh`. Slot A (`/customer/demo_v9`, libs, `test.sh`)
   må aldrig skrives. Ingen `reboot` fra værktøjer.
2. **Ingen producent-filer i repoet** — ingen `demo_v9`, MCU-firmware (`MEM03_*.bin`), libs,
   `config.bin`, OTA-pakker eller vendor-scripts (`demo.sh`, `test.sh`, `upgrade.sh`). Heller ikke
   disassembly-dumps. Korte adresser/uddrag i dokumentation er ok (interoperabilitet).
3. **Ingen credentials eller persondata** — ingen `config.json` fra en rigtig maskine (den indeholder
   Wi-Fi-adgangskoden), ingen Wi-Fi-navne/-koder, IP-adresser, MAC-adresser, serienumre,
   personlige stier (`/Users/<navn>/…`) eller md5 af filer, der indeholder hemmeligheder. Brug
   pladsholdere (`<display-ip>`). CI og reviewere tjekker det.
4. **UI'et må kun sende frames, som stock-UI'et også sender**, og kun gennem frame-politikken
   (skrive-bånd, interval-tjek, CRC, ACK + én retransmit). Kalibrerings-, reboot-, drain- og
   back-flush-ord sendes aldrig. Nye kommandoer kræver dokumentation i `docs/protocol.md` med bevis.

## Udviklingsmiljø

| Værktøj | Hvorfor |
|---|---|
| [zig](https://ziglang.org/download/) **0.17.1** | eneste C-toolchain: cross-compile til `arm-linux-musleabihf` (Cortex-A7, statisk) + host-builds |
| `python3` ≥ 3.8 | simulator, E2E-runner, installer og tests (kun standardbiblioteket) |
| `bash`, `git` | scripts |
| (valgfrit) Docker + `arm32v7/debian:bullseye-slim` | kør ARM-builds (32-bit `long`!) lokalt via qemu |

```sh
./build.sh           # alt: enhed (ARM) + værktøjer + host-SIM + self-tests  -> build/
./build.sh host      # kun det CI bruger: build/ourui_sim, switcher_host + self-tests
```

## Test

Hurtig løkke (kør før hver PR — ca. 1 minut):

```sh
./build.sh host
python3 sim/selftest.py                                   # simulatorens egen selvtest
python3 sim/run_e2e.py --all --quick --parallel 4         # 5 kerne-scenarier mod det RIGTIGE UI (SIM-build)
python3 tests/test_installer.py                           # install/revert/idempotens mod en mock-skærm
python3 boot/test_switcher.py                             # gestus-skifteren end-to-end (fil-mode)
```

Den **fulde** E2E-suite (alle scenarier, begge sprog, replay af optagelser; > 30 min) køres ikke pr.
iteration, men som CI-gate (`workflow_dispatch` / ugentligt) og før et release:

```sh
python3 sim/run_e2e.py --all --parallel 4
python3 sim/run_e2e.py --all --parallel 4 --ui-lang da
```

UI'ets indbyggede selftest (layout, strenge, frame-politik, mutations-prøver) — kræver preview-build:

```sh
zig cc -DOURUI_PREVIEW -O2 -o build/ourui_preview src/ourui_v6.c -lm && ./build/ourui_preview --selftest sim/fixtures/config.json
```

Simulatoren kan også spilles interaktivt i browseren: `python3 sim/play.py` (se `sim/PLAY.md`).

## Kodestil

- **C (`src/`, `boot/`):** C11, ingen eksterne afhængigheder, statisk musl. Nye filer skal bygge
  med `-Wall -Wextra -Werror` (UI-filen har et kendt, uændret sæt advarsler — tilføj ingen nye).
  Hold ændringer små og lokale; sikkerheds-kritiske funktioner (write-kø, frame-politik, start-
  /stop-recepter, boot-push) ændres kun med test, der beviser adfærden.
- **Python:** standardbibliotek, ingen afhængigheder, Python 3.8-kompatibel.
- **Strenge:** al synlig tekst findes på engelsk **og** dansk (streng-tabellen i `ourui_v6.c`).
- Kommentarer på dansk eller engelsk; brugerrettede CLI-beskeder på engelsk.

## Pull requests

1. Fork → branch (`feat/…`, `fix/…`) → små, fokuserede commits.
2. Kør den hurtige test-løkke ovenfor; beskriv i PR'en hvad du har kørt (og om det er testet på en
   rigtig maskine — hvilken model/firmware).
3. Ændrer du binærerne i `bin/`, så beskriv hvordan de er bygget og opdatér `bin/MD5SUMS` **og**
   `BIN_MD5` i `tools/merakiplus.py` (CI tjekker, at de stemmer).
4. Opdatér `CHANGELOG.md` og relevant dokumentation.

## Fejlrapporter

Brug issue-skabelonen "Bug report". Medtag: maskine (Gen 1/Gen 2), Meraki+-version (DIAGNOSE →
VERSION), hvad du gjorde, hvad der skete, og gerne output fra
`python3 tools/merakiplus.py status --ip ADDR` og loggen fra `logs/` — **fjern IP-adresser og andre
personlige data først**.

## Sikkerhedsfejl

Ikke som offentlig issue — se [SECURITY.md](SECURITY.md).
