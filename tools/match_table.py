"""Makes a Japanese version's function table from an English one's (tools/platinum/CPUE.txt, a
Black 2 table made by tools/bw2/funcmap.py), by finding each function in the Japanese ROM:

    python3 tools/match_table.py <english table> <english.nds> <japanese.nds> <japanese table>

The Japanese games are the same programs as the English ones, built earlier and with their own
text, so as tools/bw1/match_jp.py does for Black, each function is looked for in the same module
at the offset the function before it had, then further either side (wider windows): found when
its instructions are the same once addresses, branch targets and literal-pool offsets are
masked (tools/bw1/match_bw2.py's tokens). Its size: up to the next function in the table. The
functions the frontend watches (its "..." names) that are not found so are looked for by
likeness, as match_jp.py does. A function found nowhere keeps the shift before it.

The table written has the same lines at the Japanese addresses (the first bytes as the English
table has them: - or ?, read from the player's ROM). A developer tool: the tables are made once
and kept in the repository; they hold addresses and names, not the games' code.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "bw1"))
from match_bw2 import Rom, tokens  # noqa: E402
from match_jp import WINDOWS, similar  # noqa: E402

FRONTEND = os.path.join(HERE, "..", "frontend")


def watched_names():
    names = set()
    for fn in os.listdir(FRONTEND):
        if fn.endswith((".h", ".cpp")):
            names.update(re.findall(r'"([A-Za-z_][A-Za-z0-9_]{5,})"', open(os.path.join(FRONTEND, fn), errors="replace").read()))
    return names


def main():
    table, en_path, jp_path, out_path = sys.argv[1:5]
    en, jp = Rom(en_path), Rom(jp_path)
    names = watched_names()
    rows = []
    for line in open(table):
        p = line.split()
        if len(p) >= 5:
            rows.append((int(p[0], 16), p[1], p[2], p[3], " ".join(p[4:])))
    # Each module's functions in order, with their sizes (to the next one, at most 4 KB).
    by_module = {}
    for r in rows:
        by_module.setdefault(r[2], []).append(r)
    out, found, total, missed = {}, 0, 0, []
    for module, funcs in by_module.items():
        funcs.sort()
        m = re.match(r"ov0*(\d+)$", module)
        mod = "main" if module == "main" else ("ov%d" % int(m.group(1)) if m else None)
        if mod is None or (m and (int(m.group(1)) not in en.ovs or int(m.group(1)) not in jp.ovs)):
            for r in funcs:
                out[(r[2], r[0])] = r[0]  # (the ITCM and the like: as they are)
            continue
        delta = 0 if mod == "main" else jp.ovs[int(m.group(1))][0] - en.ovs[int(m.group(1))][0]
        print("%s: %d functions" % (module, len(funcs)), file=sys.stderr, flush=True)
        for i, (addr, kind, _mod, _first, name) in enumerate(funcs):
            total += 1
            a = addr & ~1
            isa = "thumb" if kind == "t" else "arm"
            nxt = funcs[i + 1][0] & ~1 if i + 1 < len(funcs) else a + 0x1000
            size = max(0, min(nxt - a, 0x1000))
            code = en.read(mod, a, size) if size >= 4 else None
            hit = None
            if code:
                tok = tokens(code, isa)
                step = 4 if isa == "arm" else 2
                lo = 0
                for window in WINDOWS:
                    if window > WINDOWS[0] and size < 24:
                        break
                    for k in range(lo, window // step + 1):
                        for d in ((delta,) if k == 0 else (delta + k * step, delta - k * step)):
                            head = jp.read(mod, a + d, 4)
                            if head is None or tokens(head, isa)[:1] != tok[:1]:
                                continue
                            w = jp.read(mod, a + d, size)
                            if w is not None and tokens(w, isa) == tok:
                                hit = d
                                break
                        if hit is not None:
                            break
                    if hit is not None:
                        break
                    lo = window // step + 1
                if hit is None and name in names and size >= 8:
                    hit, ratio = similar(en, jp, mod, a, size, isa, tok, delta)
                    if hit is not None:
                        print("watched %s %s %08x: found by likeness %.2f at %+#x" % (module, name, addr, ratio, hit), file=sys.stderr)
            if hit is None:
                missed.append((name in names, module, addr, name))
                out[(module, addr)] = addr + delta
                continue
            found += 1
            out[(module, addr)] = addr + hit
            # (A small function's shift is not trusted for the ones after it.)
            if size >= 24:
                delta = hit
    with open(out_path, "w") as f:
        for addr, kind, module, first, name in sorted(rows, key=lambda r: (out[(r[2], r[0])], r[2])):
            f.write("%08x %s %s %s %s\n" % (out[(module, addr)], kind, module, first, name))
    print("%d of %d functions found; not found: %d" % (found, total, len(missed)))
    missed.sort(key=lambda m: not m[0])
    for m in missed[:40]:
        print("  %s%s %08x %s" % ("WATCHED " if m[0] else "", m[1], m[2], m[3]))


if __name__ == "__main__":
    main()
