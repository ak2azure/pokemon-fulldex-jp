#!/usr/bin/env bash
# Builds the PS5 download as a title of its own: eboot.bin is the all-games program (the game
# list, then the game), started from its Home screen tile like any game and closed with the PS
# button's menu, as Porpoise (Dolphin for the PS5) is. No payload, ELF loader or websrv on the
# way. tools/ps5/build.sh builds the payload version, which stays until this one is checked.
#
#   tools/ps5/build_native.sh
#
# Needs what tools/ps5/build.sh needs (Linux, ps5-payload-sdk with pacbrew's libraries at
# /opt/ps5-payload-sdk or PS5_PAYLOAD_SDK, git, CMake, Ninja, Python 3, clang-18, lld-18) and
# zlib's headers (zlib1g-dev), for the boilerplate's converter. No ROM.
#
# What a title is made with, and from where (all GPL-3.0-or-later, fetched at the commits
# below; their notices go in the download's licenses folder):
#   ps5-native-app-boilerplate (BlackBearReloaded): the start-up code, the linker script, the
#     converter that makes the console's imports and signs eboot.bin, and sce_module/libc.prx,
#     the runtime module a title's folder must have (generated from its sources).
#   PS5_PayloadSDK (Mihawk), its platform/ layer: the heap in direct memory, memory the JIT can
#     run, longer thread stacks, and the libc functions a title is refused (directories among
#     them). tools/ps5/native/wraps.c links the program to it.
#   Porpoise (elripalda): how a title asks etaHEN to free it from the sandbox, for /data
#     (frontend/ps5_native.h).
#
# The picture is SDL's software renderer on the video output, 1920x1080, drawn by the processor
# (a title cannot load Mesa's libraries, which the payload's graphics chip path used).
#
# The result, build-ps5-native-dist/ (a folder, not a zip):
#   PKFD00002/        copied to /data/homebrew/PKFD00002: the title (Pokemon Fulldex Native)
#   pokemon-ds-wide/  copied to /data/pokemon-ds-wide: the function tables; the ROMs go in roms
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
BOILERPLATE_REPO=https://github.com/leolobato/ps5-native-app-boilerplate.git
BOILERPLATE_COMMIT=38c68b4645ab6a1470c7598df1a3fc10d3e0cae0
PLATFORM_REPO=https://github.com/mihawk-99/PS5_PayloadSDK.git
PLATFORM_COMMIT=e105cf6f25801d9e9981812a89a3b74021d2d2a2
TITLE_ID=PKFD00002
PROBE_ID=PKFD00099
DIST=build-ps5-native-dist
WORK=build-ps5-native-work

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

[ -f "$PS5_PAYLOAD_SDK/toolchain/prospero.sh" ] ||
    fail "no ps5-payload-sdk in $PS5_PAYLOAD_SDK (PS5_PAYLOAD_SDK: where it is)"
[ -f "$PS5_PAYLOAD_SDK/target/user/homebrew/lib/libSDL2.a" ] ||
    fail "ps5-payload-sdk has no SDL2: unpack pacbrew-repo's ps5-payload-dev.tar.gz at /"
for tool in git ninja python3 clang-18 clang++-18 ld.lld-18 llvm-ar-18 make wget unzip; do
    command -v "$tool" >/dev/null || fail "$tool is not installed (Ubuntu: sudo apt install git ninja-build python3 clang-18 lld-18 llvm-18 make wget unzip zlib1g-dev)"
done

fetch() { # dir url commit paths...
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

rm -rf "$DIST"
mkdir -p "$DIST" "$WORK"
say "The function tables (as tools/ps5/build.sh makes them)"
PS5_TABLES_ONLY=1 bash tools/ps5/build.sh
cp -r build-ps5-dist/pokemon-ds-wide "$DIST/pokemon-ds-wide"

say "ps5-native-app-boilerplate: its converter and the runtime module"
BP=build-ps5-boilerplate
fetch "$BP" "$BOILERPLATE_REPO" "$BOILERPLATE_COMMIT" '/*'
# (Its own pinned SDK and zlib are fetched into its .deps by this; libc.prx is generated from
# its sources and checked against the digest it records.)
make -C "$BP" libc USE_CCACHE=0 HOST_CC=clang-18 HOST_CXX=clang++-18 >/dev/null
[ -f "$BP/runtime/libc.prx" ] || fail "the boilerplate made no runtime/libc.prx"
ZLIB_A="$(find "$BP/.deps/native/zlib/root" -name libz.a | head -1)"
TOOL="$HERE/$WORK/ps5-native-tool"
clang++-18 -std=c++20 -O2 -I "$BP/.deps/native/zlib/root/usr/include" \
    "$BP/tooling/native/native_app_builder.cpp" "$BP/tooling/native/self_container.cpp" \
    "$BP/tooling/native/elf_object.cpp" "$BP/tooling/native/sce_module_writer.cpp" "$ZLIB_A" -o "$TOOL"

say "The platform layer (PS5_PayloadSDK's platform/)"
PF=build-ps5-platform
fetch "$PF" "$PLATFORM_REPO" "$PLATFORM_COMMIT" '/platform/src/' '/platform/include/' '/include/freebsd/' '/LICENSE'
rm -rf "$WORK/platform"
mkdir -p "$WORK/platform"
# (Built as its own Makefile builds it: the flags prospero-clang gives, its tree's headers.)
PCF=(-target x86_64-sie-ps5 -fPIC -fno-plt -fno-stack-protector -femulated-tls
     -fvisibility-nodllstorageclass=default -isystem "$PF/include/freebsd" -I"$PF/platform/include" -std=c11 -O2)
for f in "$PF"/platform/src/*.c; do clang-18 "${PCF[@]}" -c "$f" -o "$WORK/platform/$(basename "$f" .c).o"; done
for f in "$PF"/platform/src/regex/*.c; do clang-18 "${PCF[@]}" -w -c "$f" -o "$WORK/platform/regex_$(basename "$f" .c).o"; done
clang-18 "${PCF[@]}" -w -c "$PF/platform/src/dlmalloc/mspace.c" -o "$WORK/platform/mspace.o"
rm -f "$WORK/libps5platform.a"
llvm-ar-18 rcs "$WORK/libps5platform.a" "$WORK"/platform/*.o

say "RADV (tools/ps5/build_radv.sh)"
bash tools/ps5/build_radv.sh
RADV_A=build-ps5-radv/radv/lib/libvulkan_radeon.ps5.a
RADV_INC=build-ps5-radv/radv/include

say "The title's start-up code and its links to the platform layer"
"$PS5_PAYLOAD_SDK/bin/prospero-clang++" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -c "$BP/tooling/native/app_crt.cpp" -o "$WORK/app_crt.o"
"$PS5_PAYLOAD_SDK/bin/prospero-clang" -std=gnu11 -O2 -Wall -I"$PF/platform/include" \
    -c tools/ps5/native/wraps.c -o "$WORK/wraps.o"
# (Trace points in RADV's start and any exit or abort, for the program's link: radv_trace.c.)
"$PS5_PAYLOAD_SDK/bin/prospero-clang" -std=gnu11 -O2 -Wall \
    -c tools/ps5/native/radv_trace.c -o "$WORK/radv_trace.o"
TRACES="$(grep -o '^SSR_TRACE([a-z_0-9]*)' tools/ps5/native/radv_trace.c | sed 's/SSR_TRACE(//;s/)//' | tr '\n' ' ')"
TRACES+="abort exit _Exit"
# The names linked with --wrap: wraps.c's, and the platform layer's own (the heap, and threads
# with the main thread's 2 MiB stack instead of a title's 64 KiB).
WRAPS="$(grep -o '__wrap_[A-Za-z_0-9]*' tools/ps5/native/wraps.c | sed 's/__wrap_//' | sort -u | tr '\n' ' ')"
WRAPS+="malloc calloc realloc free posix_memalign aligned_alloc memalign malloc_usable_size reallocf "
WRAPS+="reallocarray getline getdelim pthread_create pthread_join pthread_detach"
# The program's link has RADV in it too (link.json; the test title's, link-plain.json, has not):
# its archive whole, Clang's builtins (__emutls_get_address and the like for its C++), libc's
# FILE published through fclose and fflush (open_memstream, which Mesa uses), the libc names
# RADV uses that no module a title loads gives, bound to the platform layer's ps5_ functions
# (PS5_Vulkan's tools/radv-link.sh lists them), and SDL's renderer functions, which go to
# frontend/vkrender.cpp.
BUILTINS="$(clang-18 --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
DEFSYMS="qsort_r mkstemps openlog popen pclose open_memstream __xuname __assert __memset_chk regcomp regexec regfree"
DEFSYMS+=" regerror newlocale freelocale strtod_l strtof_l dladdr utimensat localeconv_l strtoll_l strtoull_l"
DEFSYMS+=" strtold_l snprintf_l sscanf_l asprintf_l strcoll_l strxfrm_l strftime_l wcscoll_l wcsxfrm_l btowc_l"
DEFSYMS+=" wctob_l iswctype_l mbrlen_l mbrtowc_l mbsrtowcs_l mbsnrtowcs_l wcrtomb_l wcsnrtombs_l mbtowc_l"
DEFSYMS+=" ___mb_cur_max_l ___runetype_l ___tolower_l ___toupper_l __runes_for_locale catopen catgets catclose"
DEFSYMS+=" backtrace backtrace_symbols_fd localeconv arc4random_uniform fstatvfs futimens if_nameindex"
DEFSYMS+=" if_freenameindex nl_langinfo nl_langinfo_l getpwuid_r posix_fallocate access memfd_create readlink link symlink"
SDLWRAPS="SDL_CreateRenderer SDL_DestroyRenderer SDL_GetRendererInfo SDL_GetRendererOutputSize SDL_CreateTexture"
SDLWRAPS+=" SDL_DestroyTexture SDL_UpdateTexture SDL_SetTextureBlendMode SDL_SetTextureScaleMode SDL_SetTextureColorMod"
SDLWRAPS+=" SDL_SetTextureAlphaMod SDL_SetRenderDrawColor SDL_SetRenderDrawBlendMode SDL_RenderClear SDL_RenderFillRect"
SDLWRAPS+=" SDL_RenderFillRects SDL_RenderCopy SDL_RenderCopyF SDL_RenderCopyEx SDL_RenderGeometry SDL_RenderReadPixels"
SDLWRAPS+=" SDL_RenderPresent"
python3 - "$WORK" "$WRAPS" "$DEFSYMS" "$SDLWRAPS" "$HERE/$RADV_A" "$BUILTINS" "$TRACES" <<EOF
import json, sys
work, wraps, defsyms, sdl, radv, builtins, traces = sys.argv[1:8]
plain = {"sdk": "$PS5_PAYLOAD_SDK", "boilerplate": "$HERE/$BP", "tool": "$TOOL",
         "platform": "$HERE/$WORK/libps5platform.a", "crt": "$HERE/$WORK/app_crt.o",
         "wraps": "$HERE/$WORK/wraps.o", "wraps_names": wraps.split()}
json.dump(plain, open(work + "/link-plain.json", "w"), indent=1)
full = dict(plain, whole=[radv], builtins=builtins, defsyms=defsyms.split(),
            objects=["$HERE/$WORK/radv_trace.o"],
            wraps_names=wraps.split() + ["fclose", "fflush"] + sdl.split() + traces.split())
json.dump(full, open(work + "/link.json", "w"), indent=1)
EOF

title() { # dir title-id name eboot.elf
    local dir="$1" id="$2" name="$3" elf="$4"
    mkdir -p "$dir/sce_sys" "$dir/sce_module"
    "$TOOL" self --sign --in "$elf" --out "$dir/eboot.bin" --magic 0x1D3D154F >/dev/null
    cp "$BP/runtime/libc.prx" "$dir/sce_module/libc.prx"
    python3 - tools/ps5/launcher/param.json "$dir/sce_sys/param.json" "$id" "$name" <<'EOF'
import json, sys
src, out, tid, name = sys.argv[1:5]
p = json.load(open(src, encoding="utf-8"))
p["titleId"] = tid
p["contentId"] = "IV9999-%s_00-%s" % (tid, ("FULLDEX" + tid).ljust(16, "0")[:16])
p["conceptId"] = tid[-5:]
for lang in p["localizedParameters"].values():
    if isinstance(lang, dict):
        lang["titleName"] = name
json.dump(p, open(out, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
EOF
    python3 tools/ps5/make_icon.py "$dir/sce_sys/icon0.png"
}

# PS5_PROBE=1: also the test title, PKFD00099 "Fulldex Test" (tools/ps5/native/probe.cpp: /data,
# the ROM folder, the heap, the JIT, SDL's parts, the picture, sound and the pad, each tried on
# its own), in $WORK/PKFD00099. Not part of the download.
if [ -n "${PS5_PROBE:-}" ]; then
    say "The test title (tools/ps5/native/probe.cpp)"
    "$PS5_PAYLOAD_SDK/bin/prospero-clang++" -std=c++17 -O2 -DSSR_PS5_NATIVE \
        -I"$PS5_PAYLOAD_SDK/target/user/homebrew/include/SDL2" -c tools/ps5/native/probe.cpp -o "$WORK/probe.o"
    python3 tools/ps5/native_link.py --config "$WORK/link-plain.json" --out "$WORK/probe.elf" "$WORK/probe.o" \
        "$PS5_PAYLOAD_SDK/target/user/homebrew/lib/libSDL2.a" "$PS5_PAYLOAD_SDK/target/user/homebrew/lib/libiconv.a"
    rm -rf "$WORK/$PROBE_ID"
    title "$WORK/$PROBE_ID" "$PROBE_ID" "Fulldex Test" "$WORK/probe.elf"
fi

say "Compiling the program for the PS5, as a title"
(
    source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
    "$CMAKE" -S . -B build-ps5-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DSSR_JIT=ON -DSSR_PC_ALL_GAMES=ON \
        -DSSR_PNG=OFF -DSSR_GPU3D=OFF -DSSR_PORTABLE_X86=ON -DSSR_GENERATED_DIR="$HERE/build-gen-tables" \
        -DSSR_PS5_NATIVE=ON -DSSR_PS5_NATIVE_LINK="$HERE/$WORK/link.json" -DSSR_WIDE_VK=ON -DSSR_VK_RENDER=ON \
        -DSSR_VULKAN_INCLUDE="$HERE/$RADV_INC" -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
    ninja -C build-ps5-native ssrecomp
)
title "$DIST/$TITLE_ID" "$TITLE_ID" "Pokémon Fulldex Native" build-ps5-native/pokemon-fulldex.elf
cp build-ps5-native/pokemon-fulldex.elf.imports "$WORK/imports.txt"
cp build-ps5-native/pokemon-fulldex.elf.map "$WORK/pokemon-fulldex.map"

say "Notices"
mkdir -p "$DIST/licenses"
cp LICENSE "$DIST/licenses/pokemon-fulldex-LICENSE.txt"
cp "$BP/LICENSE" "$DIST/licenses/ps5-native-app-boilerplate-LICENSE.txt"
cp "$PF/LICENSE" "$DIST/licenses/PS5_PayloadSDK-LICENSE.txt"
cat > "$DIST/licenses/README.txt" <<EOF
Pokemon Fulldex for the PS5 (the title, $TITLE_ID) is GPL-3.0-or-later. Its source is at
https://github.com/ak2azure/pokemon-fulldex-jp (tools/ps5/build_native.sh builds it). It contains:

- melonDS (GPL-3.0-or-later) with this project's changes, and SDL 2 (zlib licence) as
  ps5-payload-dev builds it (https://github.com/ps5-payload-dev/pacbrew-repo).
- ps5-native-app-boilerplate (GPL-3.0-or-later, BlackBearReloaded), commit $BOILERPLATE_COMMIT:
  https://github.com/leolobato/ps5-native-app-boilerplate - the start-up code, the converter
  that made eboot.bin, and sce_module/libc.prx.
- The platform layer of PS5_PayloadSDK (GPL-3.0-or-later, Mihawk), commit $PLATFORM_COMMIT:
  https://github.com/mihawk-99/PS5_PayloadSDK (platform/), with musl's regex engine and
  dlmalloc (MIT) in it.
- How the title asks the jailbreak to free it, from Porpoise (GPL-3.0-or-later, elripalda):
  https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5 (src/porpoise_jailbreak.cpp).
- RADV, Mesa's Vulkan driver (MIT; Mesa's licence in Mesa-license.rst), from Mihawk's Mesa fork
  with its PS5 winsys, https://github.com/mihawk-99/PS5_Mesa, built as PS5_Vulkan
  (GPL-3.0-or-later, https://github.com/mihawk-99/PS5_Vulkan) builds it (tools/ps5/build_radv.sh).
EOF
[ -f build-ps5-radv/radv/MESA-LICENSE.rst ] && cp build-ps5-radv/radv/MESA-LICENSE.rst "$DIST/licenses/Mesa-license.txt"
cp tools/ps5/README-native.txt "$DIST/README.txt"

# Only the titles, the tables, the notes and the empty folders: never a ROM, save or game code.
(cd "$DIST" && find . -mindepth 1 | sed 's|^\./||' | python3 -c "
import re, sys
title = r'(eboot\.bin|sce_module|sce_module/libc\.prx|sce_sys|sce_sys/icon0\.png|sce_sys/param\.json)'
ok = re.compile(r'^(README\.txt|licenses(/[A-Za-z0-9_.-]+\.txt)?|PKFD00002(/' + title + r')?|pokemon-ds-wide(/(LICENSE\.txt|roms|saves|data|data/funcs|data/funcs/(IREO|IRDO|IRBO|IRAO|IRBJ|IRAJ|IREJ|CPUE|CPUJ)\.txt))?)$')
bad = [l for l in sys.stdin.read().split() if not ok.match(l)]
if bad: sys.exit('unexpected files in the download: %s' % bad)
")
say "Done: $HERE/$DIST"
