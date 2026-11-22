#!/usr/bin/env python3
"""previews.py — turn the preview build's raw frames into PNGs and a contact sheet. Python stdlib only.

    zig cc -DMERAKIPLUS_PREVIEW -O2 -o build/merakiplus_preview src/merakiplus.c -lm
    build/merakiplus_preview [--lang da] build/pv            # writes build/pv/*.bin (480x480 BGRA)
    python3 tools/previews.py build/pv [--sheet out.png] [--cols 4] [--only 01_,02_,...] [--thumb 240]
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "sim"))
import png  # noqa: E402


def main(a):
    d = pathlib.Path(a[0])
    sheet = a[a.index("--sheet") + 1] if "--sheet" in a else None
    cols = int(a[a.index("--cols") + 1]) if "--cols" in a else 4
    thumb = int(a[a.index("--thumb") + 1]) if "--thumb" in a else 240
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    items = []
    for b in sorted(d.glob("*.bin")):
        if only and not any(b.stem.startswith(o) for o in only):
            continue
        raw = b.read_bytes()
        if len(raw) != 480 * 480 * 4:
            continue
        png.fb2png(raw, str(b.with_suffix(".png")))
        items.append((b.stem, raw, True))
    if sheet:
        png.contact_sheet(items, sheet, cols=cols, thumb=thumb)
    print("%d PNGs%s" % (len(items), (" + " + sheet) if sheet else ""))


if __name__ == "__main__":
    main(sys.argv[1:])
