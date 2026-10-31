"""Makes the function table for Pokemon Black 2 / White 2 from the pokebw2 decompilation's
symbol files (ds-decomp configs) and your own ROM.

    python3 tools/bw2/funcmap.py <pokebw2>/config/b2_us/arm9 <rom.nds> out/b2_funcs.txt
    python3 tools/bw2/funcmap.py <pokebw2>/config/w2_us/arm9 <rom.nds> out/w2_funcs.txt
    python3 tools/bw2/funcmap.py <pokebw2>/config/b2_us/arm9 - out/IREO.txt

With - for the ROM, the overlay functions' first bytes are left as ?: the program reads them
from the player's ROM when it starts (frontend/funcwatch.h), so the table can go in a program
that is shared (the Switch's, tools/switch/build_game.sh bw).

Each line: <address> <a|t> <module> <first bytes|-> <name>
  address  where the function starts in the DS's memory (hex)
  a|t      ARM or Thumb code
  module   main, itcm, dtcm, autoload_N or ovNNN (an overlay)
  bytes    for overlay functions, the first 4 bytes of the function in the ROM: many overlays
           are loaded at the same addresses, so the frontend compares these with memory to
           tell which overlay's function is running
The table holds addresses, names and a few bytes per function: not the game's code.
"""
import os
import re
import struct
import sys

LINE = re.compile(r"^(\S+) kind:function\((arm|thumb)[^)]*\) addr:0x([0-9a-fA-F]+)")


def blz_decompress(pak):
    """Nintendo's backward LZ, used for the ARM9 binary and overlays."""
    inc_len = struct.unpack_from("<I", pak, len(pak) - 4)[0]
    if not inc_len:
        return bytes(pak)
    hdr_len = pak[-5]
    enc_len = struct.unpack_from("<I", pak, len(pak) - 8)[0] & 0xFFFFFF
    dec_len = len(pak) - enc_len
    pak_len = enc_len - hdr_len
    raw_len = dec_len + enc_len + inc_len
    src = pak[dec_len:dec_len + pak_len][::-1]
    out = bytearray()
    want = raw_len - dec_len
    i = mask = flags = 0
    while len(out) < want:
        mask >>= 1
        if not mask:
            if i >= len(src):
                break
            flags = src[i]
            i += 1
            mask = 0x80
        if not flags & mask:
            if i >= len(src):
                break
            out.append(src[i])
            i += 1
        else:
            if i + 1 >= len(src):
                break
            pos = (src[i] << 8) | src[i + 1]
            i += 2
            n = min((pos >> 12) + 3, want - len(out))
            pos = (pos & 0xFFF) + 3
            if pos > len(out):
                # (A reference before the start: not a valid overlay. The Japanese Black's
                # overlay 12 is one, never loaded; as frontend/ndsoverlays.h, it is left empty.)
                return b""
            for _ in range(n):
                out.append(out[-pos])
    return bytes(pak[:dec_len]) + bytes(out[::-1])


def overlays(rom):
    """Overlay id -> (load address, decompressed contents)."""
    fat_off, fat_size, ovt_off, ovt_size = struct.unpack_from("<IIII", rom, 0x48)
    result = {}
    for e in range(ovt_size // 32):
        oid, ram, ram_size, bss, s0, s1, file_id, flags = struct.unpack_from("<8I", rom, ovt_off + e * 32)
        start, end = struct.unpack_from("<II", rom, fat_off + file_id * 8)
        data = rom[start:end]
        if flags & (1 << 24):
            data = blz_decompress(data)
        result[oid] = (ram, data)
    return result


def main():
    cfg, rom_path, out_path = sys.argv[1:4]
    if rom_path == "-":
        rom, ovs = None, {}
    else:
        rom = open(rom_path, "rb").read()
        # (Each version's functions are at its own addresses: the other version's table would
        # point into the middle of functions.)
        want = {"b2_us": b"IREO", "w2_us": b"IRDO"}
        version = os.path.basename(os.path.dirname(os.path.normpath(cfg)))
        if version in want and rom[0x0C:0x10] != want[version]:
            sys.exit(f"this ROM is {rom[0x0C:0x10].decode('ascii', 'replace')}, but {version} is for "
                     f"{want[version].decode()} ({'Black 2' if version == 'b2_us' else 'White 2'}, US)")
        ovs = overlays(rom)
    rows = []

    def read(path, module, check):
        if not os.path.exists(path):
            return
        for line in open(path):
            m = LINE.match(line)
            if not m:
                continue
            name, kind, addr = m.group(1), m.group(2), int(m.group(3), 16)
            first = "?" if rom is None and module.startswith("ov") else "-"
            if check:
                base, data = check
                off = addr - base
                if 0 <= off and off + 4 <= len(data):
                    first = data[off:off + 4].hex()
            rows.append((addr, "t" if kind == "thumb" else "a", module, first, name))

    read(os.path.join(cfg, "symbols.txt"), "main", None)
    for sub in sorted(os.listdir(cfg)):
        if os.path.isdir(os.path.join(cfg, sub)) and sub != "overlays" and not sub.startswith("ltd_"):
            read(os.path.join(cfg, sub, "symbols.txt"), sub, None)
    ovdir = os.path.join(cfg, "overlays")
    for sub in sorted(os.listdir(ovdir)):
        oid = int(sub[2:])
        read(os.path.join(ovdir, sub, "symbols.txt"), sub, ovs.get(oid))

    with open(out_path, "w") as f:
        for r in sorted(rows):
            f.write("%08x %s %s %s %s\n" % r)
    named = sum(1 for r in rows if not r[4].startswith("func_"))
    print(f"{len(rows)} functions ({named} named) from {len(ovs)} overlays and the main program")


if __name__ == "__main__":
    main()
