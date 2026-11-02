#!/usr/bin/env bash
# Builds RADV, Mesa's Vulkan driver, for the PS5 title (tools/ps5/build_native.sh links it in):
# Mihawk's Mesa fork with its PS5 winsys (https://github.com/mihawk-99/PS5_Mesa, Mesa 26.2.0,
# -Dradv-winsys=ps5), built the way PS5_Vulkan's tools/build-radv.sh builds its release archive
# (https://github.com/mihawk-99/PS5_Vulkan, GPL-3.0-or-later), against the payload SDK v0.42 with
# Mihawk's fork of it installed over it, as that project does. A title cannot load libraries,
# so the driver is one archive linked into eboot.bin.
#
#   tools/ps5/build_radv.sh
#
# Needs (Ubuntu 24.04): git, curl, unzip, rsync, python3 with mako, pyyaml and packaging, meson
# 1.4 or newer, ninja, clang-18, lld-18, llvm-18-dev, libclang-18-dev, libclc-18-dev,
# llvm-spirv-18, libllvmspirvlib-18-dev, bison, flex, glslang-tools. Mesa's own OpenCL
# kernels (geometry shaders run as compute) are compiled on the build machine by mesa_clc,
# built first from the same tree.
#
# The result: build-ps5-radv/radv/lib/libvulkan_radeon.ps5.a and build-ps5-radv/radv/include
# (Vulkan's headers), and build-ps5-radv/sdk (the SDK it was built with, whose platform layer
# the title links too). Built again only when a pin below changes.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
SDK_URL=https://github.com/ps5-payload-dev/sdk/releases/download/v0.42/ps5-payload-sdk.zip
SDK_SHA256=8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da
FORK_REPO=https://github.com/mihawk-99/PS5_PayloadSDK.git
FORK_COMMIT=e105cf6f25801d9e9981812a89a3b74021d2d2a2
MESA_REPO=https://github.com/mihawk-99/PS5_Mesa.git
MESA_COMMIT=1ba9874f478657ccf6da72ce55f0260164cc0bd0
W=build-ps5-radv
STAMP="sdk $SDK_SHA256 fork $FORK_COMMIT mesa $MESA_COMMIT"

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

if [ -f "$W/radv/STAMP" ] && [ "$(cat "$W/radv/STAMP")" = "$STAMP" ]; then
    say "RADV is built already ($W/radv)"
    exit 0
fi
for tool in git curl unzip rsync meson ninja clang-18 python3; do
    command -v "$tool" >/dev/null || fail "$tool is not installed"
done
mkdir -p "$W"

fetch() { # dir url commit [paths...]
    local dir="$1" url="$2" commit="$3"
    shift 3
    if [ ! -d "$dir/.git" ]; then
        git init -q "$dir"
        git -C "$dir" remote add origin "$url"
    fi
    if [ $# -gt 0 ]; then git -C "$dir" sparse-checkout set --no-cone "$@"; fi
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" checkout -q --force FETCH_HEAD
}

say "The payload SDK v0.42 with the PS5 fork over it"
SDK="$HERE/$W/sdk"
if [ ! -f "$SDK/.fork" ] || [ "$(cat "$SDK/.fork")" != "$FORK_COMMIT" ]; then
    [ -f "$W/ps5-payload-sdk.zip" ] || curl -fsSL -o "$W/ps5-payload-sdk.zip" "$SDK_URL"
    echo "$SDK_SHA256  $W/ps5-payload-sdk.zip" | sha256sum --check --strict >/dev/null
    rm -rf "$W/sdk-unzip" "$SDK"
    unzip -q "$W/ps5-payload-sdk.zip" -d "$W/sdk-unzip"
    mv "$W/sdk-unzip/ps5-payload-sdk" "$SDK"
    rm -rf "$W/sdk-unzip"
    rmdir "$SDK/target/include_common" 2>/dev/null || true
    fetch "$W/fork" "$FORK_REPO" "$FORK_COMMIT" '/include/' '/host/bin/' '/platform/src/' '/platform/include/' '/LICENSE'
    # (As the fork's platform/tools/setup-sdk.sh installs it: its compiler wrappers, its
    # headers, and the platform layer, built here with the flags its Makefile uses.)
    install -m 0755 "$W/fork/host/bin/prospero-clang" "$W/fork/host/bin/prospero-clang++" "$SDK/bin/"
    cp -r "$W/fork/include/freebsd/." "$SDK/target/include"
    cp -r "$W/fork/include/khronos/." "$SDK/target/include"
    mkdir -p "$SDK/target/include/ps5" "$SDK/target/include/ps5platform" "$SDK/target/user/homebrew/include"
    cp -r "$W/fork/include/ps5/." "$SDK/target/include/ps5"
    ln -sfn user/homebrew/include "$SDK/target/include_common"
    cp "$W/fork/platform/include/ps5platform/"*.h "$SDK/target/include/ps5platform/"
    rm -rf "$W/platform-obj"
    mkdir -p "$W/platform-obj"
    PCF=(-target x86_64-sie-ps5 -fPIC -fno-plt -fno-stack-protector -femulated-tls
         -fvisibility-nodllstorageclass=default -isystem "$W/fork/include/freebsd" -I"$W/fork/platform/include" -std=c11 -O2)
    for f in "$W"/fork/platform/src/*.c; do clang-18 "${PCF[@]}" -c "$f" -o "$W/platform-obj/$(basename "$f" .c).o"; done
    for f in "$W"/fork/platform/src/regex/*.c; do clang-18 "${PCF[@]}" -w -c "$f" -o "$W/platform-obj/regex_$(basename "$f" .c).o"; done
    clang-18 "${PCF[@]}" -w -c "$W/fork/platform/src/dlmalloc/mspace.c" -o "$W/platform-obj/mspace.o"
    llvm-ar-18 rcs "$SDK/target/lib/libps5platform.a" "$W"/platform-obj/*.o
    echo "$FORK_COMMIT" > "$SDK/.fork"
fi

say "Mesa (PS5_Mesa at ${MESA_COMMIT:0:12})"
SRC="$HERE/$W/mesa"
fetch "$SRC" "$MESA_REPO" "$MESA_COMMIT"

say "mesa_clc and vtn_bindgen2, for the build machine"
CLC="$HERE/$W/clc-build"
if [ ! -f "$CLC/build.ninja" ]; then
    meson setup "$CLC" "$SRC" -Dbuildtype=release -Dmesa-clc=enabled -Dinstall-mesa-clc=true \
        -Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
        -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dllvm=enabled -Dshared-llvm=enabled \
        -Dbuild-tests=false -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dxmlconfig=disabled \
        -Dtools= > "$W/clc-setup.log" 2>&1 || { tail -30 "$W/clc-setup.log" >&2; exit 1; }
fi
ninja -C "$CLC" src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2 > "$W/clc-build.log" 2>&1 ||
    { grep -E "error|FAILED" "$W/clc-build.log" | head -30 >&2; exit 1; }
mkdir -p "$W/clc-bin"
ln -sf "$CLC/src/compiler/clc/mesa_clc" "$W/clc-bin/mesa_clc"
ln -sf "$CLC/src/compiler/spirv/vtn_bindgen2" "$W/clc-bin/vtn_bindgen2"
export PATH="$HERE/$W/clc-bin:$PATH"

say "RADV for the PS5 (release: no assertions)"
cat > "$W/cross.ini" <<EOF
[constants]
sdk = '$SDK'

[binaries]
c = sdk / 'bin/prospero-clang'
cpp = sdk / 'bin/prospero-clang++'
ar = sdk / 'bin/prospero-ar'
strip = sdk / 'bin/prospero-strip'
pkg-config = sdk / 'bin/prospero-pkg-config'

[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[built-in options]
default_library = 'static'

[properties]
needs_exe_wrapper = true
EOF
BUILD="$HERE/$W/radv-build"
if [ ! -f "$BUILD/build.ninja" ]; then
    meson setup "$BUILD" "$SRC" --cross-file "$W/cross.ini" \
        -Dvulkan-drivers=amd -Dgallium-drivers= -Dplatforms= -Dradv-winsys=ps5 \
        -Dllvm=disabled -Damd-use-llvm=false -Dvideo-codecs= \
        -Dbuildtype=debugoptimized -Db_ndebug=true \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
        -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dzlib=enabled --force-fallback-for=zlib \
        -Dexpat=disabled -Dxmlconfig=disabled -Dshader-cache=enabled -Dbuild-tests=false -Dvulkan-layers= -Dtools= \
        -Dmesa-clc=system -Dradv-build-id="$MESA_COMMIT" > "$W/radv-setup.log" 2>&1 ||
        { tail -30 "$W/radv-setup.log" >&2; exit 1; }
fi
ninja -C "$BUILD" src/amd/vulkan/libvulkan_radeon.a > "$W/radv-build.log" 2>&1 ||
    { grep -E "error|FAILED" "$W/radv-build.log" | head -30 >&2; exit 1; }

rm -rf "$W/radv"
mkdir -p "$W/radv/lib" "$W/radv/include"
cp "$BUILD/src/amd/vulkan/libvulkan_radeon.a" "$W/radv/lib/libvulkan_radeon.ps5.a"
cp -r "$SRC/include/vulkan" "$SRC/include/vk_video" "$W/radv/include/"
cp "$SRC/docs/license.rst" "$W/radv/MESA-LICENSE.rst" 2>/dev/null || true
echo "$STAMP" > "$W/radv/STAMP"
say "Done: $HERE/$W/radv/lib/libvulkan_radeon.ps5.a"
