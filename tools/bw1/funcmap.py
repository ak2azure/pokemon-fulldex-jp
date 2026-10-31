"""Makes the function table for Pokemon Black (US/Europe, IRBO) from the aletheia decompilation's
function list (layout/function_addrs.tsv) and your own ROM, in the same form as Black 2's
(tools/bw2/funcmap.py), so the same frontend and layouts can use it.

    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv <rom.nds> out/funcs.txt
    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv - out/IRBO.txt
    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv <white.nds> out/funcs.txt --white
    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv - out/IRAO.txt --white

--white makes Pokemon White's (IRAO) table: the same functions as Black's, at White's addresses
(tools/bw1/white_shifts.txt, made by tools/bw1/match_white.py). --jp-black and --jp-white make
the Japanese Black's (IRBJ) and White's (IRAJ) likewise, from tools/bw1/jp_black_shifts.txt and
jp_white_shifts.txt (made by tools/bw1/match_jp.py):

    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv - out/IRBJ.txt --jp-black
    python3 tools/bw1/funcmap.py <aletheia>/layout/function_addrs.tsv - out/IRAJ.txt --jp-white

With - for the ROM the overlay functions' first bytes are left as ?, for the program to read
from the player's ROM (see tools/bw2/funcmap.py).

Each line: <address> <a|t> <module> <first bytes|-> <name>, as for Black 2. Names are
aletheia's, except for the functions listed in tools/bw1/aliases.txt: those get the Black 2
(pokebw2) name the shared Black 2 layouts look for (FieldmapProc_Update, BtlMain_ProcMain, ...).
The table holds addresses, names and a few bytes per function: not the game's code.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bw2"))
from funcmap import overlays  # noqa: E402  (Black 2's: the same ROM format)

HERE = os.path.dirname(os.path.abspath(__file__))


def load_aliases(path):
    """(module, address) -> Black 2 name."""
    out = {}
    for line in open(path):
        line = line.split("#", 1)[0].split()
        if len(line) < 4:
            continue
        b2name, _b1name, module, addr = line[:4]
        out[(module, int(addr, 16))] = b2name
    return out


def load_shifts(path):
    """module -> sorted [(Black address from which, White - Black)]."""
    out = {}
    for line in open(path):
        line = line.split("#", 1)[0].split()
        if len(line) >= 3:
            out.setdefault(line[0], []).append((int(line[1], 16), int(line[2], 16)))
    for v in out.values():
        v.sort()
    return out


def shifted(shifts, module, addr):
    """Where White (or a Japanese version) has the function Black has at addr (unchanged where no shift is known)."""
    d = 0
    for start, delta in shifts.get(module, ()):
        if start > addr:
            break
        d = delta
    return addr + d


def main():
    versions = {
        # option: (shifts file, game code, name)
        "--white": ("white_shifts.txt", b"IRAO", "White (US/Europe)"),
        "--jp-black": ("jp_black_shifts.txt", b"IRBJ", "Black (Japan)"),
        "--jp-white": ("jp_white_shifts.txt", b"IRAJ", "White (Japan)"),
    }
    args = [a for a in sys.argv[1:] if a not in versions]
    chosen = [a for a in sys.argv[1:] if a in versions]
    tsv, rom_path, out_path = args[:3]
    shifts_file, want, title = versions[chosen[0]] if chosen else (None, b"IRBO", "Black (US/Europe)")
    white = shifts_file is not None
    shifts = load_shifts(os.path.join(HERE, shifts_file)) if white else {}
    if rom_path == "-":
        rom, ovs = None, {}
    else:
        rom = open(rom_path, "rb").read()
        if rom[0x0C:0x10] != want:
            print("warning: %s is not Pokemon %s: game code %r" % (rom_path, title, rom[0x0C:0x10]))
        ovs = overlays(rom)
    aliases = load_aliases(os.path.join(HERE, "aliases.txt"))
    found = set()
    rows = []
    for i, line in enumerate(open(tsv)):
        if i == 0:
            continue
        region, addr, name, isa, size, _path, _static = line.rstrip("\n").split("\t")
        addr = int(addr, 16)
        if region == "arm9":
            module, check = "main", None
        elif region == "arm9_autoload0_ITCM":
            module, check = "itcm", None
        else:
            m = re.match(r"ov(\d+)$", region)
            if not m:
                continue  # the DSi-only code (twl) and overlay 230's second half (ov230d)
            module, check = region, ovs.get(int(m.group(1)))
        alias = aliases.get((module, addr))
        if alias:
            found.add((module, addr))
            name = alias
        if white:
            addr = shifted(shifts, module, addr)
        first = "?" if rom is None and module.startswith("ov") else "-"
        if check:
            base, data = check
            off = addr - base
            if 0 <= off and off + 4 <= len(data):
                first = data[off:off + 4].hex()
        rows.append((addr, "t" if isa == "thumb" else "a", module, first, name))
    missing = [aliases[k] for k in aliases if k not in found]
    if missing:
        print("warning: not in the function list: " + ", ".join(missing))
    with open(out_path, "w") as f:
        for r in sorted(rows):
            f.write("%08x %s %s %s %s\n" % r)
    print(f"{len(rows)} functions ({len(found)} with Black 2 names) from {len(ovs)} overlays and the main program")


if __name__ == "__main__":
    main()
