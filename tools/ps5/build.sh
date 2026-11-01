#!/usr/bin/env bash
# Builds the PS5 download: the all-games program (the game list, then the game: Pokemon Black,
# White, Black 2, White 2, SoulSilver, HeartGold and Platinum, the Japanese ones too) as a payload
# for a jailbroken PS5's ELF loader (elfldr, port 9021), with its function tables beside it.
#
#   tools/ps5/build.sh
#
# Needs Linux (Ubuntu 24.04) with git, CMake, Ninja, Python 3, clang-18 and lld-18, and
# ps5-payload-sdk with its libraries (SDL2 among them): ps5-payload-dev.tar.gz from
# https://github.com/ps5-payload-dev/pacbrew-repo/releases, unpacked at / (it holds
# opt/ps5-payload-sdk), or PS5_PAYLOAD_SDK where it is. No ROM: SoulSilver and HeartGold's tables
# come from tools/soulsilver/tables and the Black games' from the decompilations' symbol files,
# as for the Switch's all-games download; the download holds nothing of the games.
#
# The result, build-ps5-dist/pokemon-fulldex-ps5.zip:
#   homebrew/PokemonFulldex/     copied to /data/homebrew/PokemonFulldex on the PS5 and started
#                                from websrv's homebrew list (ps5-payload-websrv): eboot.elf, the
#                                payload, with its icon and homebrew.js. It runs as an application
#                                there, which a payload needs to show its picture: sent straight
#                                to elfldr, SDL could not open the video output.
#   pokemon-ds-wide/             copied to /data/pokemon-ds-wide on the PS5 (FTP, ftpsrv): its
#                                data/funcs holds the function tables, the ROMs go in its roms
#
# The PS5's picture is drawn by its processor (SDL's PS5 video driver), and the games run on
# melonDS's interpreter (no JIT): see frontend/ssr_os.h, SSR_PS5.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
BW2_DECOMP=https://github.com/fuddlesworth/pokebw2.git
BW2_COMMIT=753b3fd97683fc619bb98b52e7276afac3d5e32f
W2_COMMIT=be2c125a700c23702ba1c5a4ef63e0e970bcc87a
BW1_DECOMP=https://github.com/Project-Holon/aletheia.git
BW1_COMMIT=0d05b6ccc61fcea090d8318a00af9f1634810e18

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

[ -f "$PS5_PAYLOAD_SDK/toolchain/prospero.sh" ] ||
    fail "no ps5-payload-sdk in $PS5_PAYLOAD_SDK (PS5_PAYLOAD_SDK: where it is)"
[ -f "$PS5_PAYLOAD_SDK/target/user/homebrew/lib/libSDL2.a" ] ||
    fail "ps5-payload-sdk has no SDL2: unpack pacbrew-repo's ps5-payload-dev.tar.gz at /"
for tool in git ninja python3 clang-18 ld.lld-18; do
    command -v "$tool" >/dev/null || fail "$tool is not installed (Ubuntu: sudo apt install git ninja-build python3 clang-18 lld-18)"
done

fetch_symbols() { # dir url commit paths...
    local dir="$1" url="$2" commit="$3"
    shift 3
    if [ ! -d "$dir/.git" ]; then
        git init -q "$dir"
        git -C "$dir" remote add origin "$url"
    fi
    git -C "$dir" sparse-checkout set --no-cone "$@"
    git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit"
    git -C "$dir" checkout -q --force FETCH_HEAD
}

say "SoulSilver's tables from tools/soulsilver/tables"
python3 tools/soulsilver/tables_only.py tools/soulsilver/tables build-gen-tables

OUT=build-ps5-dist/pokemon-ds-wide
rm -rf build-ps5-dist
mkdir -p "$OUT/data/funcs" "$OUT/roms" "$OUT/saves"
FUNCS="$OUT/data/funcs"
say "The function tables, from the decompilations' symbol files"
fetch_symbols build-decomp-pokebw2 "$BW2_DECOMP" "$BW2_COMMIT" \
    '/config/b2_us/arm9/symbols.txt' '/config/b2_us/arm9/*/symbols.txt' '/config/b2_us/arm9/overlays/*/symbols.txt'
fetch_symbols build-decomp-pokew2 "$BW2_DECOMP" "$W2_COMMIT" \
    '/config/w2_us/arm9/symbols.txt' '/config/w2_us/arm9/*/symbols.txt' '/config/w2_us/arm9/overlays/*/symbols.txt'
fetch_symbols build-decomp-aletheia "$BW1_DECOMP" "$BW1_COMMIT" '/layout/function_addrs.tsv'
python3 tools/bw2/funcmap.py build-decomp-pokebw2/config/b2_us/arm9 - "$FUNCS/IREO.txt"
python3 tools/bw2/funcmap.py build-decomp-pokew2/config/w2_us/arm9 - "$FUNCS/IRDO.txt"
python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv - "$FUNCS/IRBO.txt"
python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv - "$FUNCS/IRAO.txt" --white
python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv - "$FUNCS/IRBJ.txt" --jp-black
python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv - "$FUNCS/IRAJ.txt" --jp-white
python3 tools/bw2/funcmap.py build-decomp-pokebw2/config/b2_us/arm9 - "$FUNCS/IREJ.txt" --jp-black2
cp tools/platinum/CPUE.txt tools/platinum/CPUJ.txt "$FUNCS/"

say "Compiling for the PS5"
# (The toolchain file sets PS5, which makes CMakeLists.txt build the payload: SSR_PS5.)
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
"$CMAKE" -S . -B build-ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSSR_JIT=OFF -DSSR_PC_ALL_GAMES=ON \
    -DSSR_PNG=OFF -DSSR_GPU3D=OFF -DSSR_PORTABLE_X86=ON -DSSR_GENERATED_DIR="$HERE/build-gen-tables" \
    -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
ninja -C build-ps5 ssrecomp
HB=build-ps5-dist/homebrew/PokemonFulldex
mkdir -p "$HB/sce_sys"
"$STRIP" -o "$HB/eboot.elf" build-ps5/pokemon-fulldex.elf
cp tools/ps5/homebrew.js "$HB/homebrew.js"
python3 tools/ps5/make_icon.py "$HB/sce_sys/icon0.png"
cp LICENSE "$OUT/LICENSE.txt"
cp tools/ps5/README.txt build-ps5-dist/README.txt

# Only the payload, the tables, the notes and the empty folders: never a ROM, save or game code.
(cd build-ps5-dist && find . -mindepth 1 | sed 's|^\./||' | python3 -c "
import re, sys
ok = re.compile(r'^(README\.txt|homebrew(/PokemonFulldex(/(eboot\.elf|homebrew\.js|sce_sys|sce_sys/icon0\.png))?)?|pokemon-ds-wide(/(LICENSE\.txt|roms|saves|data|data/funcs|data/funcs/(IREO|IRDO|IRBO|IRAO|IRBJ|IRAJ|IREJ|CPUE|CPUJ)\.txt))?)$')
bad = [l for l in sys.stdin.read().split() if not ok.match(l)]
if bad: sys.exit('unexpected files in the download: %s' % bad)
")
(cd build-ps5-dist && rm -f pokemon-fulldex-ps5.zip && python3 -c "
import os, zipfile
with zipfile.ZipFile('pokemon-fulldex-ps5.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    z.write('README.txt')
    for top in ('homebrew', 'pokemon-ds-wide'):
        for root, dirs, files in os.walk(top):
            dirs.sort()
            z.writestr(zipfile.ZipInfo(root + '/'), '')
            for f in sorted(files): z.write(os.path.join(root, f))
")
say "Done: $HERE/build-ps5-dist/pokemon-fulldex-ps5.zip"
