#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tim Samshuijzen
#
# build_compiler.sh - clang and ld.lld for the FM-1's processor (pi32v2) as WebAssembly, from the source of
# pi32v2-llvm: the firmware compiler of FM-1 Studio, src/fwsc/llvm.js and src/fwsc/llvm.wasm.
#
#   firmware/web/build_compiler.sh
#
# It runs on Linux x86-64 (on Windows: in WSL). It needs git, curl, xz, python3, and for the build:
#   sudo apt install -y g++ cmake ninja-build
# The first run takes about half an hour with 6 compile jobs, and 6 GB in ~/.cache/fm1studio/wasm (FM1_WASM
# gives another place). A second run does only the steps that are not done. Nothing is installed outside
# that folder.
#
# The steps:
#   1. Gets the source of the pi32v2 LLVM fork (github.com/jvitkauskas/pi32v2-llvm) at a fixed commit.
#   2. Gets the Emscripten SDK (github.com/emscripten-core/emsdk) at a fixed version.
#   3. Gets compiler-rt's builtins for pi32v2 from the fork's release package (checked by hash).
#   4. Builds LLVM's tablegen tools for this computer: the build of step 5 runs them.
#   5. Builds LLVM's multicall tool "llvm" (clang and lld in one program) with Emscripten.
#   6. Links it two times: for a browser worker (llvm.js: the function createLLVM, a file system in memory)
#      and for Node with the real file system (llvm-node.js: for opentc.py).
#   7. Copies llvm.js, llvm.wasm and the licence texts to src/fwsc.
set -euo pipefail

LLVM_URL=https://github.com/jvitkauskas/pi32v2-llvm.git
LLVM_REV=60b0340f47083cf088903030a9b55d8a65525dde   # the commit of release pi32v2-23.1.5-5 (LLVM 23.1.5)
EMSDK_URL=https://github.com/emscripten-core/emsdk.git
EMSDK_VERSION=7.0.0
RELEASE=pi32v2-llvm-23.1.5-5-linux-x64.tar.xz
RELEASE_URL=https://github.com/jvitkauskas/pi32v2-llvm/releases/download/pi32v2-23.1.5-5/$RELEASE
RELEASE_SHA256=a20e257b840eb6b212761bfa93a7f3804502940d0035d62790da3d42fefa31e3   # from the release's SHA256SUMS
JOBS=${JOBS:-6}                                     # a compile job of clang's larger files takes about 1 GB of memory
export GIT_TERMINAL_PROMPT=0

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(dirname "$(dirname "$HERE")")
W=${FM1_WASM:-$HOME/.cache/fm1studio/wasm}
L=$W/llvm-project

die() { echo "build_compiler: $*" >&2; exit 1; }
step() { printf '\n== %s\n' "$*"; }

[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || die "this runs on Linux x86-64 (on Windows: WSL)"
missing=()
for tool in git curl xz python3 g++ cmake ninja sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
[ ${#missing[@]} -eq 0 ] ||
    die "this computer does not have: ${missing[*]}. On Ubuntu: sudo apt install -y git curl xz-utils python3 g++ cmake ninja-build"
mkdir -p "$W"

# ---- 1. The source of the LLVM fork ----

if [ "$(git -C "$L" rev-parse HEAD 2>/dev/null || true)" != "$LLVM_REV" ]; then
    step "1. The pi32v2 LLVM fork ${LLVM_REV:0:7} (a download of some hundred MB)"
    mkdir -p "$L"
    [ -d "$L/.git" ] || git -C "$L" init -q
    git -C "$L" fetch -q --depth 1 "$LLVM_URL" "$LLVM_REV"
    git -C "$L" -c advice.detachedHead=false checkout -q FETCH_HEAD
fi

# ---- 2. Emscripten ----

if [ ! -x "$W/emsdk/upstream/emscripten/emcc" ]; then
    step "2. Emscripten $EMSDK_VERSION"
    [ -d "$W/emsdk/.git" ] || git clone -q --depth 1 "$EMSDK_URL" "$W/emsdk"
    (cd "$W/emsdk" && ./emsdk install "$EMSDK_VERSION" > "$W/emsdk-install.log" 2>&1 &&
        ./emsdk activate "$EMSDK_VERSION" >> "$W/emsdk-install.log" 2>&1) || { tail -20 "$W/emsdk-install.log"; die "Emscripten did not install"; }
fi
# shellcheck disable=SC1091
. "$W/emsdk/emsdk_env.sh" > /dev/null 2>&1
emcc --version | head -1

# ---- 3. compiler-rt's builtins for pi32v2 (the fork's release has them built) ----

if [ ! -f "$W/rt/libclang_rt.builtins.a" ]; then
    step "3. The builtins library, from $RELEASE"
    mkdir -p "$W/rt"
    curl -fL --retry 3 -o "$W/rt/$RELEASE" "$RELEASE_URL"
    echo "$RELEASE_SHA256  $W/rt/$RELEASE" | sha256sum -c --quiet - || die "$RELEASE is not the file of the release"
    tar -xJf "$W/rt/$RELEASE" -C "$W/rt" --strip-components=5 --wildcards '*/lib/clang/*/lib/pi32v2/libclang_rt.builtins.a'
    rm "$W/rt/$RELEASE"
    [ -f "$W/rt/libclang_rt.builtins.a" ] || die "the release has no libclang_rt.builtins.a"
fi

# ---- 4. tablegen for this computer ----

if [ ! -x "$W/native/bin/clang-tblgen" ] || [ ! -x "$W/native/bin/llvm-tblgen" ]; then
    step "4. LLVM's tablegen tools for this computer"
    cmake -G Ninja -S "$L/llvm" -B "$W/native" -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD="" -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=PI32V2 \
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF > "$W/native-configure.log" 2>&1 ||
        { tail -30 "$W/native-configure.log"; die "the configuration for this computer failed"; }
    ninja -C "$W/native" llvm-min-tblgen llvm-tblgen clang-tblgen > "$W/native-build.log" 2>&1 ||
        { tail -40 "$W/native-build.log"; die "the tablegen build failed"; }
fi

# ---- 5. clang and lld with Emscripten ----

if [ ! -f "$W/build/build.ninja" ]; then
    step "5. Configuration for Emscripten"
    # The multicall tool takes each tool that can go into it: all but clang and lld are switched off.
    # (With llvm-objcopy in it, cmake stops: its links ask for the file name of a target that is not a program.
    # The page and opentc.py do its work: the bytes of a section of an ELF file.)
    off=()
    for d in $(cd "$L" && grep -rl GENERATE_DRIVER llvm/tools --include=CMakeLists.txt | xargs -n1 dirname | xargs -n1 basename); do
        off+=("-DLLVM_TOOL_$(echo "$d" | tr 'a-z-' 'A-Z_')_BUILD=OFF")
    done
    off+=(-DCLANG_TOOL_CLANG_SCAN_DEPS_BUILD=OFF -DCLANG_TOOL_CLANG_INSTALLAPI_BUILD=OFF)
    emcmake cmake -G Ninja -S "$L/llvm" -B "$W/build" \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DLLVM_HOST_TRIPLE=wasm32-unknown-emscripten \
        -DLLVM_TARGETS_TO_BUILD="" -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=PI32V2 -DLLVM_DEFAULT_TARGET_TRIPLE=pi32v2 \
        -DLLVM_ENABLE_PROJECTS="clang;lld" \
        -DLLVM_TOOL_LLVM_DRIVER_BUILD=ON "${off[@]}" \
        -DLLVM_NATIVE_TOOL_DIR="$W/native/bin" \
        -DLLVM_ENABLE_THREADS=OFF -DLLVM_ENABLE_PIC=OFF -DLLVM_ENABLE_PLUGINS=OFF \
        -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
        -DLLVM_ENABLE_LIBPFM=OFF \
        -DCLANG_PLUGIN_SUPPORT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_ARCMT=OFF \
        -DCLANG_ENABLE_BOOTSTRAP=OFF \
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF \
        -DLLVM_PARALLEL_LINK_JOBS=1 \
        -DCMAKE_CXX_FLAGS="-Dwait4=__syscall_wait4" \
        -DCMAKE_EXE_LINKER_FLAGS="-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB" > "$W/configure.log" 2>&1 ||
        { tail -40 "$W/configure.log"; rm -f "$W/build/build.ninja"; die "the configuration for Emscripten failed"; }
fi
if [ ! -f "$W/build/bin/llvm.wasm" ] || [ -n "$(ninja -C "$W/build" -n llvm-driver clang-resource-headers | grep '^\[' || true)" ]; then
    step "5. The build: $(ninja -C "$W/build" -n llvm-driver clang-resource-headers | grep -c '^\[') steps with $JOBS jobs"
    if ! ninja -C "$W/build" -j "$JOBS" llvm-driver clang-resource-headers > "$W/build.log" 2>&1; then
        grep -n -m3 -B2 -A30 'FAILED:' "$W/build.log" | cut -c1-1500 | head -90
        die "the build stopped (all of its text: $W/build.log)"
    fi
fi

# ---- 6. The two programs ----

# The link command of the build (": && em++ ... && cd ... && cmake -E create_symlink ... && :": only its em++
# part), with other settings and another output file.
variant() {   # variant OUTPUT.js SETTING...
    local out=$1 link
    shift
    link=$(cd "$W/build" && ninja -t commands llvm-driver | grep -E -- '-o bin/llvm(\.js)? ' | tail -1 | sed 's/ && /\n/g' | grep 'em++')
    [ -n "$link" ] || die "the build has no link command for llvm"
    link=$(echo "$link" | sed -E "s# -o bin/llvm(\.js)? # -o $out #")
    (cd "$W/build" && bash -c "$link -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sEXIT_RUNTIME=1 $*")
}
mkdir -p "$W/out"
if [ ! -f "$W/out/llvm.wasm" ] || [ "$W/build/bin/llvm.wasm" -nt "$W/out/llvm.wasm" ]; then
    step "6. Link for Node, and for the browser (each takes some minutes and 2 GB of memory)"
    variant "$W/out/llvm-node.js" -sNODERAWFS=1 -sENVIRONMENT=node
    variant "$W/out/llvm.js" -sMODULARIZE=1 -sEXPORT_NAME=createLLVM -sEXPORTED_RUNTIME_METHODS=FS,callMain \
        -sINVOKE_RUN=0 -sENVIRONMENT=web,worker,node -sFORCE_FILESYSTEM=1
fi
node "$W/out/llvm-node.js" clang --version | head -2

# ---- 7. To the site ----

step "7. src/fwsc"
mkdir -p "$REPO/src/fwsc/licences"
cp "$W/out/llvm.js" "$W/out/llvm.wasm" "$REPO/src/fwsc/"
cp "$L/LICENSE.TXT" "$REPO/src/fwsc/licences/LLVM-LICENSE.TXT"
cp "$W/emsdk/upstream/emscripten/LICENSE" "$REPO/src/fwsc/licences/Emscripten-LICENSE.txt"
cp "$W/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" "$REPO/src/fwsc/licences/musl-COPYRIGHT.txt"
ls -la "$REPO/src/fwsc"
echo "Now make the platform file: python3 firmware/web/make_platform.py"
