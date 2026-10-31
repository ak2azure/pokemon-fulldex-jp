"""Makes frontend/jpfont.h, the menus' Japanese characters (frontend/i18n.h), from a 12-dot
BDF font: the characters frontend/i18n_ja.h and the settings use, and all the kana.

    python3 tools/i18n/make_font.py <b12.bdf>

The font: efont-unicode-bdf's b12.bdf (/efont/, http://openlab.ring.gr.jp/efont/; also in
u8g2's tools/font/bdf), whose Japanese characters are Shinonome 12 and K12 (public domain).
Also checks that each English string of i18n_ja.h is still in the program's sources (one that
is not is never drawn: the program's wording changed).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FRONTEND = os.path.join(HERE, "..", "..", "frontend")


def read_bdf(path):
    glyphs, cur, bits = {}, None, None
    for line in open(path, encoding="latin-1"):
        p = line.split()
        if not p:
            continue
        if p[0] == "ENCODING":
            cur = {"cp": int(p[1])}
        elif p[0] == "DWIDTH" and cur is not None:
            cur["w"] = int(p[1])
        elif p[0] == "BBX" and cur is not None:
            cur["bbx"] = tuple(int(x) for x in p[1:5])
        elif p[0] == "BITMAP" and cur is not None:
            bits = []
        elif p[0] == "ENDCHAR" and cur is not None:
            cur["rows"] = bits or []
            glyphs[cur["cp"]] = cur
            cur, bits = None, None
        elif bits is not None:
            bits.append(int(p[0], 16))
    return glyphs


def rows12(g):
    """The glyph as 12 rows of 16 bits (leftmost pixel in bit 15), placed by its box in the
    font's 12-dot cell (ascent 10, descent 2)."""
    bw, bh, bx, by = g["bbx"]
    out = [0] * 12
    nbytes = (bw + 7) // 8
    top = 10 - (bh + by)  # (the cell's row of the box's top)
    for i, v in enumerate(g["rows"]):
        r = top + i
        if 0 <= r < 12:
            out[r] = ((v << (8 * (2 - nbytes))) >> bx) & 0xFFFF
    return out


def main():
    bdf = read_bdf(sys.argv[1])
    src = open(os.path.join(FRONTEND, "i18n_ja.h"), encoding="utf-8").read()
    text = src + "".join(open(os.path.join(FRONTEND, f), encoding="utf-8").read() for f in ("i18n.h", "launcher.h"))
    chars = {ord(c) for c in text if ord(c) >= 0x80}
    chars |= set(range(0x3041, 0x3095)) | set(range(0x30A1, 0x30FB)) | {0x30FC, 0x3001, 0x3002, 0x300C, 0x300D, 0x30FB}
    missing = sorted(c for c in chars if c not in bdf)
    if missing:
        sys.exit("not in the font: " + "".join(chr(c) for c in missing))
    cps = sorted(chars)
    with open(os.path.join(FRONTEND, "jpfont.h"), "w", encoding="utf-8") as f:
        f.write("// The menus' Japanese characters (frontend/i18n.h), 12 dots: made by tools/i18n/make_font.py from\n"
                "// efont-unicode-bdf's b12.bdf (/efont/ The Electronic Font Open Laboratory,\n"
                "// http://openlab.ring.gr.jp/efont/), whose Japanese characters come from Shinonome 12 and\n"
                "// K12 (public domain). Each: its code point, its width in dots, 12 rows (leftmost dot: bit 15).\n"
                "#pragma once\n#include <cstdint>\n\nnamespace JpFont\n{\nstruct Glyph { uint32_t cp; uint8_t w; uint16_t rows[12]; };\n"
                "static const Glyph kGlyphs[] = {\n")
        for cp in cps:
            g = bdf[cp]
            f.write("    { 0x%04X, %d, { %s } }, // %s\n" % (cp, g["w"], ", ".join("0x%04X" % r for r in rows12(g)), chr(cp)))
        f.write("};\nstatic const int kCount = %d;\n\n" % len(cps))
        f.write("// The glyph for code point cp, or nullptr.\ninline const Glyph* Find(uint32_t cp)\n{\n"
                "    int lo = 0, hi = kCount - 1;\n    while (lo <= hi)\n    {\n        const int mid = (lo + hi) / 2;\n"
                "        if (kGlyphs[mid].cp == cp) return &kGlyphs[mid];\n        if (kGlyphs[mid].cp < cp) lo = mid + 1;\n        else hi = mid - 1;\n"
                "    }\n    return nullptr;\n}\n} // namespace JpFont\n")
    print("%d characters" % len(cps))
    # The English strings: still in the program?
    sources = ""
    for fn in os.listdir(FRONTEND):
        if fn.endswith((".h", ".cpp")) and not fn.startswith(("i18n", "jpfont")):
            sources += open(os.path.join(FRONTEND, fn), encoding="utf-8", errors="replace").read()
    flat = re.sub(r'"\s*\n\s*"', "", sources)  # (strings continued on the next line)
    words = src.split("kPhrases")[0]
    for en in re.findall(r'\{ "((?:[^"\\]|\\.)*)"(?:\s*"((?:[^"\\]|\\.)*)")?,', words):
        s = en[0] + en[1]
        if '"' + s + '"' not in flat and s not in flat:
            print("not in the program:", s)


if __name__ == "__main__":
    main()
