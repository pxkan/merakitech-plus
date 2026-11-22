#!/usr/bin/env python3
"""gen_font.py — bakes the Montserrat outlines Meraki+ draws its text with into C source. Python stdlib only.

    python3 tools/gen_font.py            write src/mp/font_data.c from third_party/montserrat/*.ttf
    python3 tools/gen_font.py --check    regenerate in memory and compare with the committed file (CI / build.sh)

Why outlines and not bitmaps: the display needs text at many sizes, tabular digits, and labels set along the arc of a
ring (rotated glyphs). So the build keeps each glyph's TrueType outline (quadratic Béziers, font units) plus advance
widths and pair kerning (GPOS 'kern', PairPos formats 1 + 2), and the UI rasterizes a glyph the first time it needs it at
a size (exact-area anti-aliasing, src/mp/gfx.c) and caches the 8-bit coverage. The output is deterministic: integers
only, fixed order, no timestamps.

Only the characters the UI uses are kept (CHARS below; ~120 per face) in four weights: Light (large numbers),
Medium (body text), SemiBold (labels, titles), Bold (the clock, emphasis). Composite glyphs (Å, Æ …) are flattened.
The fonts are by the Montserrat Project Authors under the SIL Open Font License 1.1 (third_party/montserrat/OFL.txt);
the generated tables are a converted form of the fonts and carry the same license (see the header of the output).
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "third_party" / "montserrat"
OUT = ROOT / "src" / "mp" / "font_data.c"
FACES = ("Light", "Medium", "SemiBold", "Bold")
EXTRA = "ÆØÅæøåÉéÜüÖöÄä°·…—–→←−≈§±×µ"
CHARS = sorted(set(chr(c) for c in range(32, 127)) | set(EXTRA))


class TTF:
    def __init__(self, data):
        self.d = data
        n = struct.unpack_from(">H", data, 4)[0]
        self.t = {}
        for i in range(n):
            tag, _, off, ln = struct.unpack_from(">4sIII", data, 12 + 16 * i)
            self.t[tag.decode("latin-1")] = (off, ln)
        d = data
        h = self.t["head"][0]
        self.upm = struct.unpack_from(">H", d, h + 18)[0]
        self.loc_long = struct.unpack_from(">h", d, h + 50)[0]
        hh = self.t["hhea"][0]
        self.asc, self.desc = struct.unpack_from(">hh", d, hh + 4)
        self.nhm = struct.unpack_from(">H", d, hh + 34)[0]
        self.ng = struct.unpack_from(">H", d, self.t["maxp"][0] + 4)[0]
        o2 = self.t["OS/2"][0]
        ver = struct.unpack_from(">H", d, o2)[0]
        self.xh, self.cap = struct.unpack_from(">hh", d, o2 + 86) if ver >= 2 else (0, 0)
        self.cmap = self._cmap()
        lo = self.t["loca"][0]
        if self.loc_long:
            self.loca = [struct.unpack_from(">I", d, lo + 4 * i)[0] for i in range(self.ng + 1)]
        else:
            self.loca = [2 * struct.unpack_from(">H", d, lo + 2 * i)[0] for i in range(self.ng + 1)]

    def _cmap(self):
        d = self.d
        base = self.t["cmap"][0]
        n = struct.unpack_from(">H", d, base + 2)[0]
        best = None
        for i in range(n):
            pid, eid, off = struct.unpack_from(">HHI", d, base + 4 + 8 * i)
            fmt = struct.unpack_from(">H", d, base + off)[0]
            if fmt == 12 and (best is None or best[0] != 12):
                best = (12, base + off)
            elif fmt == 4 and best is None and pid in (0, 3):
                best = (4, base + off)
        m = {}
        fmt, o = best
        if fmt == 4:
            segx2 = struct.unpack_from(">H", d, o + 6)[0]
            seg = segx2 // 2
            ends = struct.unpack_from(">%dH" % seg, d, o + 14)
            starts = struct.unpack_from(">%dH" % seg, d, o + 16 + segx2)
            deltas = struct.unpack_from(">%dh" % seg, d, o + 16 + 2 * segx2)
            ro_off = o + 16 + 3 * segx2
            ros = struct.unpack_from(">%dH" % seg, d, ro_off)
            for s in range(seg):
                for c in range(starts[s], ends[s] + 1):
                    if c == 0xFFFF:
                        continue
                    if ros[s] == 0:
                        g = (c + deltas[s]) & 0xFFFF
                    else:
                        a = ro_off + 2 * s + ros[s] + 2 * (c - starts[s])
                        g = struct.unpack_from(">H", d, a)[0]
                        if g:
                            g = (g + deltas[s]) & 0xFFFF
                    m[c] = g
        else:
            ng = struct.unpack_from(">I", d, o + 12)[0]
            for i in range(ng):
                a, b, g = struct.unpack_from(">III", d, o + 16 + 12 * i)
                for c in range(a, b + 1):
                    m[c] = g + (c - a)
        return m

    def adv(self, g):
        hm = self.t["hmtx"][0]
        i = min(g, self.nhm - 1)
        return struct.unpack_from(">H", self.d, hm + 4 * i)[0]

    def contours(self, g, depth=0):
        """list of contours, each a list of (x, y, on_curve) in font units"""
        d = self.d
        a, b = self.loca[g], self.loca[g + 1]
        if a == b:
            return []
        o = self.t["glyf"][0] + a
        nc = struct.unpack_from(">h", d, o)[0]
        if nc >= 0:
            ends = struct.unpack_from(">%dH" % nc, d, o + 10)
            npt = ends[-1] + 1 if nc else 0
            il = struct.unpack_from(">H", d, o + 10 + 2 * nc)[0]
            p = o + 12 + 2 * nc + il
            flags = []
            while len(flags) < npt:
                f = d[p]; p += 1
                flags.append(f)
                if f & 8:
                    r = d[p]; p += 1
                    flags.extend([f] * r)
            flags = flags[:npt]
            xs, ys = [], []
            for arr, short, same in ((xs, 2, 16), (ys, 4, 32)):
                v = 0
                for f in flags:
                    if f & short:
                        dv = d[p]; p += 1
                        v += dv if f & same else -dv
                    elif not f & same:
                        v += struct.unpack_from(">h", d, p)[0]; p += 2
                    arr.append(v)
            out, s = [], 0
            for e in ends:
                out.append([(xs[i], ys[i], bool(flags[i] & 1)) for i in range(s, e + 1)])
                s = e + 1
            return out
        # composite
        p = o + 10
        out = []
        while True:
            fl, gi = struct.unpack_from(">HH", d, p); p += 4
            if fl & 1:
                dx, dy = struct.unpack_from(">hh", d, p); p += 4
            else:
                dx, dy = struct.unpack_from(">bb", d, p); p += 2
            if not fl & 2:
                raise ValueError("point-matched composite not supported")
            xx, xy, yx, yy = 1.0, 0.0, 0.0, 1.0
            if fl & 8:
                xx = yy = struct.unpack_from(">h", d, p)[0] / 16384.0; p += 2
            elif fl & 0x40:
                xx, yy = (v / 16384.0 for v in struct.unpack_from(">hh", d, p)); p += 4
            elif fl & 0x80:
                xx, xy, yx, yy = (v / 16384.0 for v in struct.unpack_from(">hhhh", d, p)); p += 8
            for c in self.contours(gi, depth + 1):
                out.append([(round(x * xx + y * yx + dx), round(x * xy + y * yy + dy), on) for x, y, on in c])
            if not fl & 0x20:
                break
        return out

    # ---- GPOS pair kerning ----
    def kerning(self, gids):
        if "GPOS" not in self.t:
            return {}
        d = self.d
        g0 = self.t["GPOS"][0]
        fl_off, ll_off = struct.unpack_from(">HH", d, g0 + 6)
        fl = g0 + fl_off
        lookups = set()
        for i in range(struct.unpack_from(">H", d, fl)[0]):
            tag, off = struct.unpack_from(">4sH", d, fl + 2 + 6 * i)
            if tag == b"kern":
                ft = fl + off
                n = struct.unpack_from(">H", d, ft + 2)[0]
                lookups.update(struct.unpack_from(">%dH" % n, d, ft + 4))
        ll = g0 + ll_off
        want = set(gids)
        res = {}
        for li in sorted(lookups):
            lo = ll + struct.unpack_from(">H", d, ll + 2 + 2 * li)[0]
            lt, _, ns = struct.unpack_from(">HHH", d, lo)
            done = set()                      # within one lookup the first subtable that covers a pair wins
            for si in range(ns):
                st = lo + struct.unpack_from(">H", d, lo + 6 + 2 * si)[0]
                t = lt
                if t == 9:
                    t = struct.unpack_from(">H", d, st + 2)[0]
                    st = st + struct.unpack_from(">I", d, st + 4)[0]
                if t != 2:
                    continue
                for (a, b), v in self._pairpos(st, want).items():
                    if (a, b) in done:
                        continue
                    done.add((a, b))
                    if v:
                        res[(a, b)] = res.get((a, b), 0) + v
        return res

    def _coverage(self, o):
        d = self.d
        fmt, n = struct.unpack_from(">HH", d, o)
        cov = {}
        if fmt == 1:
            for i, g in enumerate(struct.unpack_from(">%dH" % n, d, o + 4)):
                cov[g] = i
        else:
            for i in range(n):
                s, e, si = struct.unpack_from(">HHH", d, o + 4 + 6 * i)
                for g in range(s, e + 1):
                    cov[g] = si + g - s
        return cov

    def _classdef(self, o):
        d = self.d
        fmt = struct.unpack_from(">H", d, o)[0]
        cd = {}
        if fmt == 1:
            s, n = struct.unpack_from(">HH", d, o + 2)
            for i, c in enumerate(struct.unpack_from(">%dH" % n, d, o + 6)):
                cd[s + i] = c
        else:
            n = struct.unpack_from(">H", d, o + 2)[0]
            for i in range(n):
                s, e, c = struct.unpack_from(">HHH", d, o + 4 + 6 * i)
                for g in range(s, e + 1):
                    cd[g] = c
        return cd

    @staticmethod
    def _vsize(vf):
        return 2 * bin(vf & 0xFF).count("1")

    def _xadv(self, p, vf):
        """XAdvance (bit 0x0004) of a value record at p"""
        if not vf & 4:
            return 0
        skip = 2 * bin(vf & 3).count("1")
        return struct.unpack_from(">h", self.d, p + skip)[0]

    def _pairpos(self, st, want):
        d = self.d
        fmt, cov_off, vf1, vf2 = struct.unpack_from(">HHHH", d, st)
        cov = self._coverage(st + cov_off)
        s1, s2 = self._vsize(vf1), self._vsize(vf2)
        out = {}
        if fmt == 1:
            n = struct.unpack_from(">H", d, st + 8)[0]
            offs = struct.unpack_from(">%dH" % n, d, st + 10)
            for g, ci in cov.items():
                if g not in want or ci >= n:
                    continue
                ps = st + offs[ci]
                cnt = struct.unpack_from(">H", d, ps)[0]
                rec = 2 + s1 + s2
                for k in range(cnt):
                    q = ps + 2 + rec * k
                    g2 = struct.unpack_from(">H", d, q)[0]
                    if g2 in want:
                        out[(g, g2)] = self._xadv(q + 2, vf1)
        elif fmt == 2:
            cd1o, cd2o, c1n, c2n = struct.unpack_from(">HHHH", d, st + 8)
            cd1, cd2 = self._classdef(st + cd1o), self._classdef(st + cd2o)
            rec = s1 + s2
            base = st + 16
            for g in want:
                if g not in cov:
                    continue
                c1 = cd1.get(g, 0)
                for g2 in want:
                    c2 = cd2.get(g2, 0)
                    out[(g, g2)] = self._xadv(base + (c1 * c2n + c2) * rec, vf1)
        return out


def path_ops(contours):
    """TrueType contours -> ops (M/L/Q/Z) with integer coordinates (y up, font units)."""
    ops = []
    for c in contours:
        if not c:
            continue
        n = len(c)
        # start at an on-curve point (or the midpoint of two off points)
        k = next((i for i, p in enumerate(c) if p[2]), None)
        if k is None:
            a, b = c[0], c[1 % n]
            start = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
            seq = c[1:] + c[:1]
        else:
            start = c[k][:2]
            seq = c[k + 1:] + c[:k]
        ops.append(("M", start))
        prev_off = None
        for x, y, on in seq + [(start[0], start[1], True)]:
            if on:
                if prev_off is None:
                    ops.append(("L", (x, y)))
                else:
                    ops.append(("Q", prev_off, (x, y)))
                    prev_off = None
            else:
                if prev_off is not None:
                    mid = ((prev_off[0] + x) / 2, (prev_off[1] + y) / 2)
                    ops.append(("Q", prev_off, mid))
                prev_off = (x, y)
        ops.append(("Z",))
    return ops


def generate():
    fonts = [TTF((SRC / ("Montserrat-%s.ttf" % f)).read_bytes()) for f in FACES]
    lines = []
    w = lines.append
    w("/* src/mp/font_data.c — GENERATED by tools/gen_font.py from third_party/montserrat/*.ttf. Do not edit.")
    w(" * Part of the merakiplus.c unity build (#included before gfx.c).")
    w(" *")
    w(" * Montserrat: Copyright 2024 The Montserrat.Git Project Authors (https://github.com/JulietaUla/Montserrat.git).")
    w(" * These tables are a converted subset of the Montserrat fonts (outlines, advances, kerning) and are licensed under")
    w(" * the SIL Open Font License, Version 1.1 — see third_party/montserrat/OFL.txt. They are not covered by the MIT license")
    w(" * of the rest of this repository.")
    w(" *")
    w(" * Layout: FG_CP[] = the code points (sorted); per face f and glyph i: FG_ADV[f][i] (font units), the outline ops")
    w(" * FG_OP[FG_OPS[f][i] .. FG_OPS[f][i+1]) (0 = move, 1 = line, 2 = quad, 3 = close) with their points in FG_PT")
    w(" * (x, y pairs, y up, font units; a quad takes two points), and FK_* = kerning pairs (left << 8 | right) -> units. */")
    cps = [ord(c) for c in CHARS]
    present = [cp for cp in cps if all(cp in f.cmap for f in fonts)]
    missing = [chr(cp) for cp in cps if cp not in present]
    w("#define FG_N %d" % len(present))
    w("#define FG_NFACE %d" % len(fonts))
    f0 = fonts[0]
    w("#define FG_UPM %d" % f0.upm)
    w("static const short FG_METRIC[FG_NFACE][4]={ /* ascender, descender, cap height, x height */")
    for f in fonts:
        w(" {%d,%d,%d,%d}," % (f.asc, f.desc, f.cap, f.xh))
    w("};")
    w("static const unsigned short FG_CP[FG_N]={")
    for i in range(0, len(present), 16):
        w(" " + ",".join(str(c) for c in present[i:i + 16]) + ",")
    w("};")
    all_ops, all_pts, ops_idx, advs, kerns = [], [], [], [], []
    for f in fonts:
        gids = [f.cmap[cp] for cp in present]
        idx = []
        for g in gids:
            idx.append((len(all_ops), len(all_pts)))
            for op in path_ops(f.contours(g)):
                code = "MLQZ".index(op[0])
                all_ops.append(code)
                for pt in op[1:]:
                    all_pts.append((int(round(pt[0])), int(round(pt[1]))))
        idx.append((len(all_ops), len(all_pts)))
        ops_idx.append(idx)
        advs.append([f.adv(g) for g in gids])
        k = f.kerning(gids)
        gi = {g: i for i, g in enumerate(gids)}
        pairs = sorted(((gi[a] << 8) | gi[b], v) for (a, b), v in k.items() if v and a in gi and b in gi)
        kerns.append(pairs)
    assert len(present) < 256
    w("static const short FG_ADV[FG_NFACE][FG_N]={")
    for a in advs:
        w(" {" + ",".join(map(str, a)) + "},")
    w("};")
    w("static const unsigned FG_OPS[FG_NFACE][FG_N+1][2]={ /* first op, first point */")
    for idx in ops_idx:
        w(" {" + ",".join("{%d,%d}" % p for p in idx) + "},")
    w("};")
    w("static const unsigned char FG_OP[%d]={" % len(all_ops))
    for i in range(0, len(all_ops), 64):
        w(" " + ",".join(map(str, all_ops[i:i + 64])) + ",")
    w("};")
    w("static const short FG_PT[%d]={" % (2 * len(all_pts)))
    for i in range(0, len(all_pts), 16):
        w(" " + ",".join("%d,%d" % p for p in all_pts[i:i + 16]) + ",")
    w("};")
    for fi, pairs in enumerate(kerns):
        w("static const unsigned short FK_K%d[%d]={" % (fi, max(1, len(pairs))))
        for i in range(0, len(pairs), 20):
            w(" " + ",".join(str(p[0]) for p in pairs[i:i + 20]) + ",")
        if not pairs:
            w(" 0")
        w("};")
        w("static const signed char FK_V%d[%d]={" % (fi, max(1, len(pairs))))
        for i in range(0, len(pairs), 32):
            w(" " + ",".join(str(max(-128, min(127, p[1]))) for p in pairs[i:i + 32]) + ",")
        if not pairs:
            w(" 0")
        w("};")
    w("static const unsigned short*const FK_K[FG_NFACE]={" + ",".join("FK_K%d" % i for i in range(len(fonts))) + "};")
    w("static const signed char*const FK_V[FG_NFACE]={" + ",".join("FK_V%d" % i for i in range(len(fonts))) + "};")
    w("static const int FK_N[FG_NFACE]={" + ",".join(str(len(p)) for p in kerns) + "};")
    text = "\n".join(lines) + "\n"
    stats = "glyphs %d/face x %d faces, %d ops, %d points, kerning pairs %s%s" % (
        len(present), len(fonts), len(all_ops), len(all_pts), [len(p) for p in kerns],
        (", missing: " + "".join(missing)) if missing else "")
    return text, stats


def main():
    text, stats = generate()
    if "--check" in sys.argv:
        ok = OUT.is_file() and OUT.read_text() == text
        print("font_data.c: " + ("OK (" + stats + ")" if ok else "DIFFERS from tools/gen_font.py output — run python3 tools/gen_font.py"))
        return 0 if ok else 1
    OUT.write_text(text)
    print("wrote %s: %s, %d bytes" % (OUT.relative_to(ROOT), stats, len(text)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
