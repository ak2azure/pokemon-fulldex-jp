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
#   homebrew/PKFD00001/          copied to /data/homebrew/PKFD00001 on the PS5: eboot.elf, the
#                                program (a payload), with Mesa's libraries; eboot.bin, sce_module
#                                and sce_sys, the Home screen app that ShadowMountPlus registers
#                                (tools/ps5/launcher); starter.elf, which that app sends to the ELF
#                                loader to start eboot.elf as an application (tools/ps5/starter),
#                                which a payload needs to show its picture: sent straight to
#                                elfldr, SDL could not open the video output. homebrew.js puts it
#                                in websrv's homebrew list too.
#   pokemon-ds-wide/             copied to /data/pokemon-ds-wide on the PS5 (FTP, ftpsrv): its
#                                data/funcs holds the function tables, the ROMs go in its roms
#
# The game's picture is drawn by the PS5's graphics chip at 3840x2160 (OpenGL ES through Mesa:
# zink on RADV), with the processor's picture (SDL's software renderer, 1920x1080) when that
# cannot start; the game list keeps to the processor. The widescreen world is drawn by the
# graphics chip too (WORLD ON GPU: desktop OpenGL 4.3 on zink, frontend/glcontext.cpp).
# Mesa's libraries go beside eboot.elf, and SDL is built here (ps5-payload-dev's SDL with pacbrew's Mesa patch) with its OpenGL ES 2
# renderer, which pacbrew's SDL leaves out. The games run on melonDS's JIT without fast memory,
# or its interpreter if the kernel does not let the payload run code it makes (frontend/ps5.h).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
BW2_DECOMP=https://github.com/fuddlesworth/pokebw2.git
BW2_COMMIT=753b3fd97683fc619bb98b52e7276afac3d5e32f
W2_COMMIT=be2c125a700c23702ba1c5a4ef63e0e970bcc87a
BW1_DECOMP=https://github.com/Project-Holon/aletheia.git
BW1_COMMIT=0d05b6ccc61fcea090d8318a00af9f1634810e18
SDL_REPO=https://github.com/ps5-payload-dev/SDL.git
SDL_COMMIT=ee4c47dc0d617b3bc8f35108f9956baf228a1322
PACBREW_REPO=https://github.com/ps5-payload-dev/pacbrew-repo.git
PACBREW_COMMIT=722187f0ee246c0d7d57401c8a97de1c7f4e2ff0
# The starter's way of starting the program (hbldr.c and what it uses) and the Home screen app's
# build (eboot.bin from an ordinary ELF, the runtime module, the signature), both GPL-3.0.
WEBSRV_REPO=https://github.com/ps5-payload-dev/websrv.git
WEBSRV_COMMIT=7748df9d2a43822a5ebfda149f43d4f2696d640b
BOILERPLATE_REPO=https://github.com/leolobato/ps5-native-app-boilerplate.git
BOILERPLATE_COMMIT=38c68b4645ab6a1470c7598df1a3fc10d3e0cae0
TITLE_ID=PKFD00001

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

[ -f "$PS5_PAYLOAD_SDK/toolchain/prospero.sh" ] ||
    fail "no ps5-payload-sdk in $PS5_PAYLOAD_SDK (PS5_PAYLOAD_SDK: where it is)"
[ -f "$PS5_PAYLOAD_SDK/target/user/homebrew/lib/libSDL2.a" ] ||
    fail "ps5-payload-sdk has no SDL2: unpack pacbrew-repo's ps5-payload-dev.tar.gz at /"
for tool in git ninja python3 clang-18 ld.lld-18 make wget unzip; do
    command -v "$tool" >/dev/null || fail "$tool is not installed (Ubuntu: sudo apt install git ninja-build python3 clang-18 lld-18 make wget unzip)"
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
# (PS5_TABLES_ONLY: only the tables, for the title's build, tools/ps5/build_native.sh.)
if [ -n "${PS5_TABLES_ONLY:-}" ]; then
    cp LICENSE "$OUT/LICENSE.txt"
    exit 0
fi

say "SDL with its OpenGL ES 2 renderer"
# (pacbrew's SDL2 package, ps5-payload-dev's SDL with its Mesa patch, and one line more: the
# OpenGL ES 2 renderer, which draws the game with the graphics chip. It replaces the SDK's
# libSDL2.a; the headers stay.)
fetch_symbols build-ps5-pacbrew "$PACBREW_REPO" "$PACBREW_COMMIT" '/SDL2/*.patch'
if [ ! -d build-ps5-sdl/.git ]; then
    git clone -q "$SDL_REPO" build-ps5-sdl
fi
git -C build-ps5-sdl checkout -q --force "$SDL_COMMIT"
git -C build-ps5-sdl clean -qfdx
patch -d build-ps5-sdl -Np1 -s -i "$HERE/build-ps5-pacbrew/SDL2/0001-ps5-opengl-opengles-and-vulkan-through-mesa.patch"
python3 - build-ps5-sdl/CMakeLists.txt <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
a = "      set(SDL_VIDEO_OPENGL_ES2 1)\n      set(HAVE_OPENGLES TRUE)\n    endif()\n    if(SDL_VULKAN)"
assert s.count(a) == 1, "the PS5's OpenGL ES lines in SDL's CMakeLists.txt have changed"
s = s.replace(a, a.replace("set(SDL_VIDEO_OPENGL_ES2 1)", "set(SDL_VIDEO_OPENGL_ES2 1)\n      set(SDL_VIDEO_RENDER_OGL_ES2 1)"))
open(p, "w").write(s)
EOF
(
    source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
    "$CMAKE" -S build-ps5-sdl -B build-ps5-sdl/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSDL_OPENGL=YES -DSDL_OPENGLES=YES -DSDL_VULKAN=YES -DSDL_LOADSO=YES >/dev/null
    ninja -C build-ps5-sdl/build SDL2-static >/dev/null
)
SDL_LIB="$PS5_PAYLOAD_SDK/target/user/homebrew/lib/libSDL2.a"
if [ -w "$SDL_LIB" ]; then cp build-ps5-sdl/build/libSDL2.a "$SDL_LIB"; else sudo cp build-ps5-sdl/build/libSDL2.a "$SDL_LIB"; fi

say "The Home screen app (ps5-native-app-boilerplate)"
# Built in the boilerplate's own checkout, with its own pinned SDK, before this one's toolchain
# is in the environment. Its checks allow only PPSA titles (a game's); ShadowMountPlus takes
# homebrew titles too (four letters and five digits), as Playden's FAKE77031.
fetch_symbols build-ps5-boilerplate "$BOILERPLATE_REPO" "$BOILERPLATE_COMMIT" '/*'
python3 - build-ps5-boilerplate/tools/build.sh <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
n = s.count(r"PPSA\d{5}")
assert n == 2, "the boilerplate's title checks have changed"
open(p, "w").write(s.replace(r"PPSA\d{5}", r"[A-Z]{4}\d{5}"))
EOF
rm -rf build-ps5-boilerplate/fulldex
mkdir -p build-ps5-boilerplate/fulldex/src build-ps5-boilerplate/fulldex/sce_sys
cp tools/ps5/launcher/main.cpp build-ps5-boilerplate/src/demo_renderer.cpp build-ps5-boilerplate/src/demo_renderer.hpp \
    build-ps5-boilerplate/fulldex/src/
cp tools/ps5/launcher/param.json build-ps5-boilerplate/fulldex/param.json
python3 tools/ps5/make_icon.py build-ps5-boilerplate/fulldex/sce_sys/icon0.png
make -C build-ps5-boilerplate app USE_CCACHE=0 HOST_CC=clang-18 HOST_CXX=clang++-18 APP_SOURCE_DIR=fulldex/src \
    APP_PARAM=fulldex/param.json APP_SCE_SYS=fulldex/sce_sys APP_ASSETS= >/dev/null
APP_BUILT="build-ps5-boilerplate/dist/$TITLE_ID"
[ -f "$APP_BUILT/eboot.bin" ] || fail "the Home screen app was not built"

say "Compiling for the PS5"
# (The toolchain file sets PS5, which makes CMakeLists.txt build the payload: SSR_PS5.)
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
"$CMAKE" -S . -B build-ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSSR_JIT=ON -DSSR_PC_ALL_GAMES=ON \
    -DSSR_PNG=OFF -DSSR_GPU3D=ON -DSSR_PORTABLE_X86=ON -DSSR_GENERATED_DIR="$HERE/build-gen-tables" \
    -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
ninja -C build-ps5 ssrecomp
HB="build-ps5-dist/homebrew/$TITLE_ID"
mkdir -p "$HB/sce_sys"
"$STRIP" -o "$HB/eboot.elf" build-ps5/pokemon-fulldex.elf
cp tools/ps5/homebrew.js "$HB/homebrew.js"
# The Home screen app's files beside the program: eboot.bin, its runtime module and sce_sys.
cp "$APP_BUILT/eboot.bin" "$HB/eboot.bin"
cp -r "$APP_BUILT/sce_module" "$HB/sce_module"
cp "$APP_BUILT/sce_sys/param.json" "$HB/sce_sys/param.json"

say "The starter (websrv's way of starting homebrew)"
fetch_symbols build-ps5-websrv "$WEBSRV_REPO" "$WEBSRV_COMMIT" '/src/ps5/*' '/LICENSE'
# (hbldr.c's "fs.h", "sys.h" and "websrv.h" are tools/ps5/starter's: not in its own folder.)
"$CC" -O2 -Wall -Itools/ps5/starter -Ibuild-ps5-websrv/src/ps5 -o build-ps5/starter.elf \
    tools/ps5/starter/main.c tools/ps5/starter/fs.c build-ps5-websrv/src/ps5/hbldr.c \
    build-ps5-websrv/src/ps5/elfldr.c build-ps5-websrv/src/ps5/pt.c \
    -lkernel_sys -lSceSystemService -lSceUserService
"$STRIP" -o "$HB/starter.elf" build-ps5/starter.elf
# Mesa (MIT licence): EGL and OpenGL ES, gallium's zink and RADV, found beside eboot.elf
# (LD_LIBRARY_PATH, frontend/ps5.h); everything else they use is the console's own.
MESA="$PS5_PAYLOAD_SDK/target/user/homebrew/lib"
for lib in libEGL.so.1 libGLESv2.so.2 libGL.so.1 libvulkan.so.1; do cp -L "$MESA/$lib" "$HB/$lib"; done
cp -L "$MESA"/libgallium-*.so "$HB/"
python3 tools/ps5/make_icon.py "$HB/sce_sys/icon0.png"
cp LICENSE "$OUT/LICENSE.txt"
cp tools/ps5/README.txt build-ps5-dist/README.txt

# Only the payload, the tables, the notes and the empty folders: never a ROM, save or game code.
(cd build-ps5-dist && find . -mindepth 1 | sed 's|^\./||' | python3 -c "
import re, sys
ok = re.compile(r'^(README\.txt|homebrew(/$TITLE_ID(/(eboot\.elf|eboot\.bin|starter\.elf|sce_module|sce_module/libc\.prx|homebrew\.js|sce_sys|sce_sys/icon0\.png|sce_sys/param\.json|libEGL\.so\.1|libGLESv2\.so\.2|libGL\.so\.1|libvulkan\.so\.1|libgallium-[0-9.]+\.so))?)?|pokemon-ds-wide(/(LICENSE\.txt|roms|saves|data|data/funcs|data/funcs/(IREO|IRDO|IRBO|IRAO|IRBJ|IRAJ|IREJ|CPUE|CPUJ)\.txt))?)$')
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
