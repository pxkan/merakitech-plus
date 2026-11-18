#!/usr/bin/env python3
"""measure_stock_home.py — measure the stock home buttons' geometry from an fb capture (no PIL).

Finds the bright ring pixels (the Grinder/Hot Water/Brew/Steam circles) and computes
center+radius per cluster plus the distance from the screen center (240,240) to the nearest
ring edge. Used to choose a safe gesture radius (GESTUS-FIX.md).
"""
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w, h, bitdepth, ctype = 8, b"", 0, 0, 0, 0
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bitdepth, ctype = struct.unpack(">IIBB", data[:10])
        elif typ == b"IDAT":
            idat += data
        pos += 12 + ln
    assert bitdepth == 8 and ctype in (2, 6), (bitdepth, ctype)
    ch = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * ch
    px = bytearray(w * h * ch)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        px[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, ch, px


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "test-logs/final_A_stock_v611.png"
    w, h, ch, px = read_png(path)
    # ring pixels: light gray (all channels high and equal) — the background is brown
    pts = []
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * ch
            r, g, b = px[i], px[i + 1], px[i + 2]
            if r > 150 and g > 150 and b > 150 and abs(r - g) < 24 and abs(g - b) < 24:
                pts.append((x, y))
    # cluster per quadrant (the four circles) — icons/labels are also bright but
    # lie INSIDE the circle; the ring dominates the extreme points.
    quads = {(0, 0): [], (1, 0): [], (0, 1): [], (1, 1): []}
    for x, y in pts:
        if y < 40 or y > 396:    # chevron (top) + clock (bottom) are not rings
            continue
        quads[(x >= w // 2, y >= h // 2)].append((x, y))
    print(f"capture {w}x{h}, bright pixels: {len(pts)}")
    import math
    centers = []
    for q, qp in sorted(quads.items()):
        xs = [p[0] for p in qp]; ys = [p[1] for p in qp]
        # the ring's extreme points give center+radius (the label sits above the ring:
        # use the x extremes and the bottom point; top is estimated symmetrically)
        x0, x1 = min(xs), max(xs)
        y1 = max(ys)
        cx = (x0 + x1) / 2
        r = (x1 - x0) / 2
        cy = y1 - r
        centers.append((cx, cy, r))
        print(f"  quadrant {q}: ring x {x0}..{x1} y..{y1} → center ({cx:.0f},{cy:.0f}) r={r:.0f}")
    cx0, cy0 = w / 2, h / 2
    for cx, cy, r in centers:
        d = math.hypot(cx - cx0, cy - cy0)
        print(f"  center ({cx:.0f},{cy:.0f}) r={r:.0f}: distance from ({cx0:.0f},{cy0:.0f}) = {d:.1f} → ring edge {d - r:.1f} px")
    # largest safe radius: min distance to the ring edge (the touch target can be slightly
    # larger than the drawn ring; subtract an 8 px margin)
    gap = min(math.hypot(cx - cx0, cy - cy0) - r for cx, cy, r in centers)
    print(f"smallest gap center→ring edge: {gap:.1f} px; recommended safe radius ≤ {gap - 8:.0f} px")


if __name__ == "__main__":
    main()
