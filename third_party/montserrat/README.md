# Montserrat (third-party font)

Meraki+ v7.0 draws all of its text in **Montserrat**, by the Montserrat Project Authors. The files in this
directory are the upstream fonts, unmodified, and their license. They are not covered by the MIT license of
the rest of the repository.

| File | What it is | Used for |
|---|---|---|
| `Montserrat-Light.ttf` | Montserrat Light (TrueType, version 9.000) | large numbers: the live view's number, the roller value, the stats |
| `Montserrat-Medium.ttf` | Montserrat Medium | body text, captions, toasts, the roller's neighbour values |
| `Montserrat-SemiBold.ttf` | Montserrat SemiBold | titles, row labels, buttons, the ring labels on HOME |
| `Montserrat-Bold.ttf` | Montserrat Bold | row values, the clock on HOME |
| `OFL.txt` | the SIL Open Font License, Version 1.1, with the copyright line | the license of the fonts and of the converted tables |

## Source

<https://github.com/JulietaUla/Montserrat>, directory `fonts/ttf`. The four files were copied from there
without changes (no renaming of the fonts, no subsetting of the TTFs themselves).

SHA-256 of the committed files:

```
853de0dd715d6681f7038cf6cd53c1695b614657cbc57c8f542fe08d5b84e006  Montserrat-Light.ttf
dae47428bb041f9716604e0e07b5b0c8585b3bdd8183362f75c69fe7bb3cfaf4  Montserrat-Medium.ttf
b4e1563393d73fdff491a869441245aef31add2ec03d9c97a6dae4de07c52fd0  Montserrat-SemiBold.ttf
bc6e854971cea46b463be6f9eef4d9cd52f51cfc1fc0dd90c9d3e6483dc0ec61  Montserrat-Bold.ttf
8b7141c03fa4f8d44e6345d5d4931709290f0f67875e452e95ac1fd3a027802e  OFL.txt
```

## License

SIL Open Font License, Version 1.1 — see [OFL.txt](OFL.txt). Copyright 2024 The Montserrat.Git Project
Authors. The OFL allows the fonts to be used, embedded and redistributed (also in converted form) as long as
the copyright notice and the license travel with them and the fonts are not sold on their own.

## How the fonts get into the binary: `src/mp/font_data.c`

The display binary does not load TTF files. [`tools/gen_font.py`](../../tools/gen_font.py) (Python standard
library only) reads the four TTFs and writes `src/mp/font_data.c`, a converted subset:

* only the characters the UI uses (printable ASCII plus `ÆØÅæøåÉéÜüÖöÄä°·…—–→←−≈§±×µ`; 122 glyphs per weight),
* per glyph: the TrueType outline (quadratic Béziers in font units) and the advance width; composite glyphs (Å, Æ, …) are flattened,
* the pair kerning from the GPOS `kern` feature (PairPos formats 1 and 2),
* per weight: ascender, descender, cap height and x-height.

The output is deterministic (integers only, fixed order, no timestamps). Its header carries the copyright
line and states that the tables are licensed under the OFL, not under MIT. At run time `src/mp/gfx.c`
rasterizes each glyph the first time a text style needs it (exact-area anti-aliasing, four sub-pixel phases)
and caches the coverage.

```sh
python3 tools/gen_font.py            # regenerate src/mp/font_data.c from the TTFs here
python3 tools/gen_font.py --check    # regenerate in memory and compare with the committed file
```

`--check` exits non-zero when the committed `font_data.c` differs from what the TTFs and the generator
produce; `build.sh` runs it (from v7.0), so a stale file fails the build. Do not edit `font_data.c` by hand: change the character set (`CHARS`/`EXTRA` in
`gen_font.py`) or the fonts, then regenerate and commit both.
