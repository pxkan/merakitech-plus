#!/usr/bin/env python3
"""png.py — PNG writing + contact sheet WITHOUT dependencies (stdlib zlib only).

Pillow is not in .venv (policy: no new deps). Same method as
artifacts/write_test/fb2png.py: raw BGRA → RGB scanlines w/ filter byte 0 →
zlib-compressed IDAT. Contact sheet: thumbnails in a grid w/ 5×7 bitmap-font
labels (scenario names + index).
"""
import struct
import zlib

W = H = 480


def _chunk(typ, data):
    c = struct.pack(">I", len(data)) + typ + data
    return c + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)


def rgb_to_png_bytes(w, h, rgb: bytes) -> bytes:
    """rgb = w*h*3 bytes (RGB, row-major)."""
    lines = bytearray()
    stride = w * 3
    for y in range(h):
        lines.append(0)
        lines += rgb[y * stride:(y + 1) * stride]
    return (b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(bytes(lines), 6))
            + _chunk(b"IEND", b""))


def fb2png(raw_bgra: bytes, path, w=W, h=H):
    """480×480 BGRA (fb format) → PNG. No dependencies."""
    need = w * h * 4
    if len(raw_bgra) < need:
        raise ValueError(f"fb buffer too short: {len(raw_bgra)} < {need}")
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        b, g, r = raw_bgra[i * 4], raw_bgra[i * 4 + 1], raw_bgra[i * 4 + 2]
        rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2] = r, g, b
    with open(path, "wb") as f:
        f.write(rgb_to_png_bytes(w, h, bytes(rgb)))


def bgra_to_rgb(raw_bgra: bytes, w=W, h=H) -> bytes:
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        b, g, r = raw_bgra[i * 4], raw_bgra[i * 4 + 1], raw_bgra[i * 4 + 2]
        rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2] = r, g, b
    return bytes(rgb)


def scale_rgb(rgb: bytes, w, h, nw, nh) -> bytes:
    """Nearest-neighbor scaling (thumbnails)."""
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        sy = y * h // nh
        for x in range(nw):
            sx = x * w // nw
            so = (sy * w + sx) * 3
            do = (y * nw + x) * 3
            out[do:do + 3] = rgb[so:so + 3]
    return bytes(out)


# ---------------------------------------------------------------- 5×7 font
_FONT = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["01110", "10000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00001", "01110"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    "_": ["00000", "00000", "00000", "00000", "00000", "00000", "11111"],
    ".": ["00000", "00000", "00000", "00000", "00000", "00000", "00100"],
    ":": ["00000", "00100", "00000", "00000", "00000", "00100", "00000"],
    " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    "(": ["00010", "00100", "01000", "01000", "01000", "00100", "00010"],
    ")": ["01000", "00100", "00010", "00010", "00010", "00100", "01000"],
    "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    "=": ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
}


def draw_text(rgb: bytearray, w, h, x, y, text, color, scale=1):
    """Draw text (uppercase ASCII) into an rgb buffer. Glyphs are 5×7 px."""
    cr, cg, cb = color
    cx = x
    for ch in text.upper():
        glyph = _FONT.get(ch, _FONT[" "])
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit == "1":
                    for sy in range(scale):
                        for sx in range(scale):
                            px, py = cx + gx * scale + sx, y + gy * scale + sy
                            if 0 <= px < w and 0 <= py < h:
                                o = (py * w + px) * 3
                                rgb[o], rgb[o + 1], rgb[o + 2] = cr, cg, cb
        cx += 6 * scale


def contact_sheet(items, path, cols=4, thumb=160, label_h=22, pad=6,
                  title=None, bg=(24, 26, 30), fg=(220, 220, 225)):
    """items = [(label, raw_bgra_480x480_or_None, ok_bool)] → PNG grid.

    ok_bool sets the label color (green/red). Missing fb = dark placeholder.
    """
    n = len(items)
    rows = (n + cols - 1) // cols
    cell_w, cell_h = thumb + pad, thumb + label_h + pad
    head = 26 if title else 0
    w = cols * cell_w + pad
    h = rows * cell_h + pad + head
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2] = bg
    if title:
        draw_text(rgb, w, h, pad, 8, title[: (w - 2 * pad) // 6], fg, 1)
    for i, (label, raw, ok) in enumerate(items):
        r, c = divmod(i, cols)
        x0 = pad + c * cell_w
        y0 = head + pad + r * cell_h
        if raw is not None and len(raw) >= W * H * 4:
            small = scale_rgb(bgra_to_rgb(raw), W, H, thumb, thumb)
            for y in range(thumb):
                so, do = y * thumb * 3, ((y0 + y) * w + x0) * 3
                rgb[do:do + thumb * 3] = small[so:so + thumb * 3]
        else:
            for y in range(thumb):
                for x in range(thumb):
                    o = ((y0 + y) * w + x0 + x) * 3
                    rgb[o:o + 3] = bytes((40, 40, 46))
        col = (120, 230, 150) if ok else (255, 110, 110)
        draw_text(rgb, w, h, x0, y0 + thumb + 4, f"{i:02d} {label}"[: (thumb // 6)], col, 1)
    with open(path, "wb") as f:
        f.write(rgb_to_png_bytes(w, h, bytes(rgb)))
    return path