#!/usr/bin/env python3
"""Where SoulSilver's (US) functions are in the Japanese SoulSilver or HeartGold: a developer
tool, run once; its output (tools/soulsilver/soulsilver-jp.json, heartgold-jp.json) holds
addresses only, as tools/soulsilver/heartgold-us.json does, and the tables-only build
(tools/soulsilver/tables_only.py) plays the game with it.

    python3 tools/soulsilver/match_japanese.py <soulsilver-us.nds> <japanese.nds> <gen dir> OUT.json

The Japanese games are the same program as the US SoulSilver, built earlier with their own
text: the main program and most overlays are the US ones with stretches moved by a few bytes.
As tools/soulsilver/match_heartgold.py does for HeartGold, each of SoulSilver's chunks
(<gen dir>/rc_tables.cpp) is looked for in the Japanese ROM by its first instructions with
branch targets and literal-pool offsets masked: first where the chunk before it moved to, then
further either side (the main program and the ITCM too, which HeartGold has as they are). The
result: per section, the stretches where the Japanese address is SoulSilver's plus a shift.
A chunk not found keeps the shift of the one before it.
"""
import bisect
import difflib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
from ndsrom import read_rom  # noqa: E402
from match_heartgold import LAYOUT, chunks_of, mask_arm, mask_thumb  # noqa: E402

NAMES = {b'IPGJ': 'Pokemon SoulSilver (Japan)', b'IPKJ': 'Pokemon HeartGold (Japan)'}


def main():
    us_rom, jp_rom, gen, out = sys.argv[1:5]
    code = open(jp_rom, 'rb').read(0x10)[0x0C:0x10]
    if code not in NAMES:
        sys.exit('%s is not the Japanese SoulSilver or HeartGold: %r' % (jp_rom, code))
    layout = json.load(open(LAYOUT))
    us_main, us_ovl = read_rom(us_rom)
    jp_main, jp_ovl = read_rom(jp_rom)
    chunks, names = chunks_of(gen)
    result, missed_list, liked = {}, [], []
    total_found = total_missed = 0
    for e in layout['sections']:
        src = e['source']
        # (moved: where the Japanese overlay is loaded less where SoulSilver's is. The shifts are
        # of offsets in the overlay; the runtime adds the overlay's move to them.)
        moved = 0
        if src == 'main':
            a, b = us_main, jp_main
        elif src in ('itcm',) or not str(src).isdigit():
            continue
        else:
            if int(src) not in us_ovl or int(src) not in jp_ovl:
                continue
            a, b = us_ovl[int(src)][1], jp_ovl[int(src)][1]
            moved = jp_ovl[int(src)][0] - us_ovl[int(src)][0]
        if e['name'] not in names:
            continue
        sec = names.index(e['name'])
        base = e['base'] if src != 'main' else 0x02000000
        # The units looked for: the chunks, split at the functions the layout names (a chunk
        # can hold several small functions, and the frontend watches some of those).
        units = []
        named = sorted(set(x for x in e.get('named', []) if isinstance(x, int)))
        for start, end, thumb, s in chunks:
            if s != sec:
                continue
            lo = bisect.bisect_right(named, start)
            hi = bisect.bisect_left(named, end)
            cuts = [start] + named[lo:hi] + [end]
            for u0, u1 in zip(cuts, cuts[1:]):
                if u1 > u0:
                    units.append((u0, u1, thumb))
        segs = []
        shift, found, missed = 0, 0, 0
        for start, end, thumb in units:
            off = start - base
            n = min(end - start, 32)
            mask = mask_thumb if thumb else mask_arm
            step = 2 if thumb else 4
            want = mask(a[off:off + n])
            best = None
            if want:
                reach = (0x400, 0x4000, 0x40000) if n >= 16 else (0x400,)
                tried = 0
                for r in reach:
                    for k in range(tried // step, r // step + 1):
                        for d in ((shift,) if k == 0 else (shift + k * step, shift - k * step)):
                            o = off + d
                            if o < 0 or o + n > len(b):
                                continue
                            # (The first instruction first: most places differ there.)
                            if mask(b[o:o + step])[:1] != want[:1]:
                                continue
                            if mask(b[o:o + n]) == want:
                                best = d
                                break
                        if best is not None:
                            break
                    if best is not None:
                        break
                    tried = r + step
            if best is None and end - start >= 32:
                # Not as it is: the Japanese one may use other offsets or text. The most alike
                # place near where the one before it moved, if alike enough (0.8: the Japanese
                # Black's watched functions that differ were 0.85 and more).
                m = min(end - start, 256)
                sm = difflib.SequenceMatcher(None, autojunk=False)
                sm.set_seq2(mask(a[off:off + m]))
                ratio, at = 0.0, None
                for k in range(0, 0x400 // step + 1):
                    for d in ((shift,) if k == 0 else (shift + k * step, shift - k * step)):
                        o = off + d
                        if o < 0 or o + m > len(b):
                            continue
                        sm.set_seq1(mask(b[o:o + m]))
                        if sm.real_quick_ratio() <= ratio or sm.quick_ratio() <= ratio:
                            continue
                        rr = sm.ratio()
                        if rr > ratio:
                            ratio, at = rr, d
                if ratio >= 0.8:
                    best = at
                    liked.append((e['name'], start, ratio, at))
            if best is None:
                missed += 1
                missed_list.append((e['name'], start))
            else:
                found += 1
                # (A short one's place is not trusted for the ones after it.)
                if n >= 16 or best == shift:
                    shift = best
            d = best if best is not None else shift
            # (Stretches with the same shift are one, the gaps between functions too.)
            if segs and segs[-1][2] == d:
                segs[-1][1] = end
            else:
                segs.append([start, end, d])
        segs = [s for s in segs if s[2]]
        if moved:
            # (The overlay's own move, a stretch from 0 to 0: the runtime moves the section
            # and every address in it by it, rc_portable_load and Remap in runtime/recomp_rt.cpp.)
            segs.insert(0, [0, 0, moved])
        if segs:
            result[e['name']] = segs
        total_found += found
        total_missed += missed
        print(f"{e['name']}: {found} functions found, {missed} not, {len(segs)} shifted stretches", file=sys.stderr, flush=True)
    json.dump({'game': NAMES[code], 'code': code.decode(), 'like': 'IPGE', 'shifts': result}, open(out, 'w'), indent=1)
    print(f'{total_found} functions found ({len(liked)} by likeness), {total_missed} not')
    if os.environ.get('MISSED'):
        json.dump(missed_list, open(os.environ['MISSED'], 'w'))


if __name__ == '__main__':
    main()
