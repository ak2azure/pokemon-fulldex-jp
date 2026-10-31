"""Finds where the Japanese Pokemon Black (IRBJ) or White (IRAJ) has each of the English Black's
(aletheia) functions, and writes the shifts file tools/bw1/funcmap.py --jp-black / --jp-white
reads (tools/bw1/jp_black_shifts.txt, jp_white_shifts.txt). A developer tool: the shifts are in
the repository, so the Japanese tables are made without it (and without any ROM).

    python3 tools/bw1/match_jp.py <aletheia>/layout/function_addrs.tsv <black-en.nds> <black-jp.nds>
    python3 tools/bw1/match_jp.py <aletheia>/layout/function_addrs.tsv <black-en.nds> <white-jp.nds>

The Japanese games are the same program as the English ones, built earlier and with their own
text: most functions are where the English ones are plus a shift that changes now and then
(more code or data before them). As tools/bw1/match_white.py does for White, each function is
looked for in the same module at the offset the function before it had, then further either
side (a few windows, wider each time): found when its instructions are the same once
addresses, branch targets and literal-pool offsets are masked (tools/bw1/match_bw2.py's
tokens). A function found nowhere keeps the shift before it, and is listed (the frontend's
watched functions first, so that a missing one is seen).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_bw2 import Rom, tokens  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FRONTEND = os.path.join(HERE, "..", "..", "frontend")
WINDOWS = (0x200, 0x2000, 0x10000)


def watched_names():
    """The function names the frontend looks up (Find("...") and the like), and the aliases'."""
    names = set()
    for fn in (os.listdir(FRONTEND) if os.path.isdir(FRONTEND) else ()):
        if fn.endswith((".h", ".cpp")):
            names.update(re.findall(r'"([A-Za-z_][A-Za-z0-9_]{5,})"', open(os.path.join(FRONTEND, fn), errors="replace").read()))
    aliases = {}
    for line in open(os.path.join(HERE, "aliases.txt")):
        p = line.split("#", 1)[0].split()
        if len(p) >= 4:
            aliases[(p[2], int(p[3], 16))] = p[0]
    return names, aliases


def find_bw2map(en, jp):
    """Where the Japanese version has what frontend/bw2map.h changes for the wider map: the field
    overlay's MAP_CONFIGS (English 0x021d3700 in overlay 21: 21 entries of 72 bytes, the first
    starting 0x200000, 0x400000; the Japanese entries' heap sizes differ a little) and
    HEAP_SETUP_NTR's user heap (English 0x0209d8b0: 0x6000, 0, 0x199500 in a list that ends
    0xe000f, 0xc000d, 0xa000b; the Japanese heap is smaller), found by their contents."""
    import struct
    out = {}
    base, data = jp.ovs.get(21, (0, b""))
    first = struct.pack("<II", 0x200000, 0x400000)
    hits = []
    for o in range(0, len(data) - 21 * 72 + 1, 4):
        if data[o:o + 8] != first:
            continue
        # (The table's 21 entries: each with a map window size where the English has one.)
        if all(data[o + i * 72 + 16:o + i * 72 + 20] == en.read("ov21", 0x021d3700 + i * 72 + 16, 4) for i in range(21)):
            hits.append(base + o)
    out["table"] = hits[:1]
    tail = struct.pack("<4I", 0xE000F, 0xC000D, 0xA000B, 0)
    hits = []
    o = jp.main.find(tail)
    while o >= 0:
        # (The user heap's entry: 0x6000, 0, its size, 0, just before the list's end.)
        a = jp.ram + o - 0x10
        size = struct.unpack_from("<I", jp.main, o - 8)[0]
        hits.append("0x%08x (user heap 0x%x)" % (a, size))
        o = jp.main.find(tail, o + 4)
    out["heapSetup"] = hits
    return out


def similar(en, jp, module, addr, size, isa, tok, delta, reach=0x400, least=0.8):
    import difflib
    step = 4 if isa == "arm" else 2
    best, at = 0.0, None
    for d in range(delta - reach, delta + reach + 1, step):
        w = jp.read(module, addr + d, size)
        if w is None:
            continue
        r = difflib.SequenceMatcher(None, tok, tokens(w, isa), autojunk=False).ratio()
        # (Ties: the one nearer the shift.)
        if r > best or (r == best and at is not None and abs(d - delta) < abs(at - delta)):
            best, at = r, d
    return (at, best) if best >= least else (None, best)


def main():
    tsv, en_path, jp_path = sys.argv[1:4]
    en, jp = Rom(en_path), Rom(jp_path)
    game = open(jp_path, "rb").read(0x10)[0x0C:0x10]
    if game not in (b"IRBJ", b"IRAJ"):
        print("warning: %s is not the Japanese Black or White: game code %r" % (jp_path, game))
    white = game == b"IRAJ"
    out_path = sys.argv[4] if len(sys.argv) > 4 else os.path.join(HERE, "jp_white_shifts.txt" if white else "jp_black_shifts.txt")
    names, aliases = watched_names()
    # (The overlays: the same numbers in both, checked here.)
    only_en = sorted(set(en.ovs) - set(jp.ovs))
    if only_en:
        print("warning: overlays only in the English ROM: %s" % only_en)
    funcs = {}
    for i, line in enumerate(open(tsv)):
        if i == 0:
            continue
        region, addr, name, isa, size, _path, _static = line.rstrip("\n").split("\t")
        module = "main" if region == "arm9" else region
        funcs.setdefault(module, []).append((int(addr, 16), name, isa, int(size)))
    lines, found, missed, total = [], 0, [], 0
    for module in sorted(funcs, key=lambda m: (m != "main", int(m[2:]) if m[2:].isdigit() else -1, m)):
        if module == "main":
            delta = 0
        elif module.startswith("ov") and module[2:].isdigit() and int(module[2:]) in en.ovs and int(module[2:]) in jp.ovs:
            oid = int(module[2:])
            delta = jp.ovs[oid][0] - en.ovs[oid][0]
        else:
            continue  # the ITCM's code and the DSi-only parts: not watched
        # (The module's own shift from its first function on, so that functions before the first
        # one found are where the overlay's move puts them.)
        first = min(funcs[module])
        lines.append("%s %08x %+#x %s" % (module, first[0], delta, first[1]))
        last = delta
        print("%s: %d functions" % (module, len(funcs[module])), file=sys.stderr, flush=True)
        for addr, name, isa, size in sorted(funcs[module]):
            total += 1
            shown = aliases.get((module, addr), name)
            code = en.read(module, addr, size)
            if not code or size < 4:
                missed.append((shown in names, module, addr, shown, "no code"))
                continue
            tok = tokens(code, isa)
            step = 4 if isa == "arm" else 2
            hit = None
            lo = 0
            for window in WINDOWS:
                # (Small functions only near where they should be: their few masked instructions
                # are found in many places.)
                if window > WINDOWS[0] and size < 24:
                    break
                for k in range(lo, window // step + 1):
                    for d in ((delta,) if k == 0 else (delta + k * step, delta - k * step)):
                        # (The first instruction first: most places differ there, and masking a
                        # whole function at every place of a wide window is slow.)
                        head = jp.read(module, addr + d, 4)
                        if head is None or tokens(head, isa)[:1] != tok[:1]:
                            continue
                        w = jp.read(module, addr + d, size)
                        if w is not None and tokens(w, isa) == tok:
                            hit = d
                            break
                    if hit is not None:
                        break
                if hit is not None:
                    break
                lo = window // step + 1
            if hit is None and shown in names and size >= 8:
                # A watched function not found as it is: the Japanese one may use other offsets
                # into its structures (the same instructions, other immediates). The most alike
                # place near the shift, if alike enough, by hand-checked examples (the name entry
                # and options screens' main functions: 0.85 and more).
                hit, ratio = similar(en, jp, module, addr, size, isa, tok, delta)
                if hit is not None:
                    print("watched %s %s %08x: found by likeness %.2f at %+#x" % (module, shown, addr, ratio, hit), file=sys.stderr)
            if hit is None:
                missed.append((shown in names, module, addr, shown, "kept %+#x" % delta))
                continue
            found += 1
            if size < 24 and hit != delta:
                if shown in names:
                    # (A watched one still gets its place: a line for it alone, then the shift
                    # before it again.)
                    lines.append("%s %08x %+#x %s" % (module, addr, hit, name))
                    lines.append("%s %08x %+#x %s" % (module, addr + 1, delta, name + "+1"))
                    last = delta
                continue
            if hit != last:
                lines.append("%s %08x %+#x %s" % (module, addr, hit, name))
                last = hit
            delta = hit
    with open(out_path, "w") as f:
        f.write("# Pokemon %s (Japan, %s): where the English Black's (aletheia) functions are, as shifts\n"
                "# from those addresses: <module> <English address from which> <Japanese - English>\n"
                "# <the first function so moved>. Made by tools/bw1/match_jp.py; read by tools/bw1/funcmap.py %s.\n"
                % ("White" if white else "Black", game.decode("ascii", "replace"), "--jp-white" if white else "--jp-black"))
        for l in lines:
            f.write(l + "\n")
    print(f"{found} of {total} functions found, {len(lines)} shift lines; not found: {len(missed)}")
    for k, v in find_bw2map(en, jp).items():
        print("bw2map %s: %s%s" % (k, " ".join(a if isinstance(a, str) else "0x%08x" % a for a in v[:10]) or "not found",
                                   " (%d in all)" % len(v) if len(v) > 10 else ""))
    missed.sort(key=lambda m: not m[0])
    for m in missed[:60]:
        print("  %s%s %08x %s (%s)" % ((("WATCHED " if m[0] else ""),) + tuple(m[1:])))


if __name__ == "__main__":
    main()
