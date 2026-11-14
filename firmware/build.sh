#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tim Samshuijzen
#
# build.sh - a project zip of FM-1 Studio to a .fwsc package for a real M-VAVE FM-1.
#
#   firmware/build.sh PROJECT.zip [NAME]
#
# NAME is the name on the FM-1's start screen (default: the name of the zip).
# It runs on Linux x86-64. On Windows that is WSL: firmware\build.cmd starts
# this script there. firmware/README.md tells what it needs and what it makes.
#
# The steps:
#   1. Gets fm1-base and felucca-bsp from GitHub, at the fixed commits below.
#   2. Gets JieLi's compiler from JieLi's server, and three files of JieLi's
#      SDK from JieLi's repository on gitee.com.
#   3. Makes an fm1-base app from the zip (firmware/make_app.py).
#   4. Builds the app for fm1-base's host simulator and runs it for a few
#      seconds: pictures of the screen and a recording of the sound.
#   5. Builds the app for the FM-1 and packs it: NAME.fwsc.
# Steps 1 and 2 are done one time; they are kept in ~/.cache/fm1studio
# (set FM1_WORK for another place).
set -euo pipefail

BASE_URL=https://github.com/charlesvestal/fm1-base
BASE_REV=c8e05997ce970875038156d2bbd69e712eac1543   # 2026-11-13
BSP_URL=https://github.com/hugelton/felucca-bsp
BSP_REV=18eadadb8c364abd026accd2b1ea1c14ca347f06    # the commit that fm1-base links
# JieLi's AC79 SDK, from JieLi's own repository on gitee.com. (felucca-bsp's get_sdk.sh takes it from
# github.com/Jieli-Tech, but that copy is gone since November 2026.) SDK_URL gives another place.
SDK_URL=${SDK_URL:-https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git}
SDK_TAG=AC79NN_SDK_V1.2.1_2023-12-13
SDK_REV=d179b4484759423312073f5fbb232501aa491047    # the commit of that tag: the version that felucca-bsp was tested with
export GIT_TERMINAL_PROMPT=0                        # a repository that is not there must fail, not ask for a login

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(dirname "$HERE")
WORK=${FM1_WORK:-$HOME/.cache/fm1studio}
BASE=$WORK/fm1-base

die() { echo "build: $*" >&2; exit 1; }
step() { printf '\n== %s\n' "$*"; }

[ $# -ge 1 ] || { echo "usage: firmware/build.sh PROJECT.zip [NAME]" >&2; exit 2; }
ZIP=$1
NAME_SHOWN=${2:-}
case "$ZIP" in
    [A-Za-z]:[\\/]*) ZIP=$(wslpath -u "$ZIP") ;;      # a Windows path, in WSL
esac
[ -f "$ZIP" ] || die "there is no file $ZIP"

# ---- What the computer must have ----

[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] ||
    die "JieLi's compiler runs on Linux x86-64 only (on Windows: WSL). This is $(uname -s) $(uname -m)."
missing=()
for tool in git curl tar xz python3 cc sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
# the C compiler of this computer needs the C library's headers too (Ubuntu: libc6-dev), for the host simulator
if command -v cc >/dev/null 2>&1; then
    printf '%s\n' '#include <stdint.h>' '#include <stdio.h>' '#include <math.h>' \
        'int main(void) { return (int)sqrt(0.0); }' |
        cc -x c - -o /dev/null -lm 2>/dev/null || missing+=("C-library-headers")
fi
# fm1-base draws its fonts with Pillow, and needs Pillow's Raqm text layout for that
python3 -c "from PIL import features; raise SystemExit(0 if features.check('raqm') else 1)" 2>/dev/null ||
    missing+=("Pillow-with-Raqm")
if [ ${#missing[@]} -gt 0 ]; then
    echo "build: this computer does not have: ${missing[*]}" >&2
    echo "       On Ubuntu (also in WSL), install them with:" >&2
    echo "       sudo apt update && sudo apt install -y git curl xz-utils python3 python3-pil libraqm0 libfribidi0 gcc libc6-dev" >&2
    exit 3
fi

# ---- 1. fm1-base and felucca-bsp ----

# fetch DIR URL COMMIT: that commit of that repository in DIR, without its history
fetch() {
    if [ "$(git -C "$1" rev-parse HEAD 2>/dev/null || true)" = "$3" ] && [ -e "$1/.git" ]; then
        return
    fi
    case "$1" in
        "$WORK"/?*) rm -rf "$1" ;;                    # (only ever a folder of our own)
        *) die "will not replace $1" ;;
    esac
    mkdir -p "$1"
    git -C "$1" init -q
    git -C "$1" fetch -q --depth 1 "$2" "$3"
    git -C "$1" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

step "fm1-base ${BASE_REV:0:7} and felucca-bsp ${BSP_REV:0:7}"
mkdir -p "$WORK"
fetch "$BASE" "$BASE_URL" "$BASE_REV"
fetch "$BASE/bsp" "$BSP_URL" "$BSP_REV"

# ---- 2. JieLi's compiler and SDK files ----

if [ -z "${JIELI_TOOLCHAIN:-}" ]; then
    if [ ! -x "$WORK/jieli/toolchain/pi32v2/bin/clang" ]; then
        step "JieLi's compiler (from pkgman.jieliapp.com)"
        sh "$BASE/tools/get_toolchain.sh" "$WORK/jieli"
    fi
    export JIELI_TOOLCHAIN=$WORK/jieli/toolchain
fi
[ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || die "no compiler in $JIELI_TOOLCHAIN"

# The three files of the SDK that go into a package (Apache-2.0), with the SDK's licence. Only these are
# taken: the rest of the SDK (0.5 GB) is for firmware with Bluetooth or WiFi, which an fm1.h program has not.
get_sdk() {
    local dir=$1
    mkdir -p "$dir"
    [ -d "$dir/.git" ] || git -C "$dir" init -q
    git -C "$dir" fetch -q --depth 1 --filter=blob:none "$SDK_URL" "$SDK_TAG" || return 1
    git -C "$dir" sparse-checkout init --no-cone
    git -C "$dir" sparse-checkout set /LICENSE /README.md /cpu/wl82/tools/uboot.boot /cpu/wl82/tools/cfg_tool.bin /cpu/wl82/tools/cfg/eq_cfg_hw.bin
    git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD || return 1
    [ "$(git -C "$dir" rev-parse HEAD)" = "$SDK_REV" ] || die "$SDK_TAG at $SDK_URL is not the commit that was tested"
    (cd "$dir/cpu/wl82/tools" && sha256sum -c --quiet - <<'SUMS'
4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4  uboot.boot
276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b  cfg_tool.bin
41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d  cfg/eq_cfg_hw.bin
SUMS
    ) || die "a file of the SDK is not the one that was tested"
}

if [ -z "${AC79_SDK:-}" ]; then
    if [ ! -f "$WORK/ac79-sdk/cpu/wl82/tools/cfg/eq_cfg_hw.bin" ]; then
        step "Three files of JieLi's AC79 SDK (from ${SDK_URL#https://}, checked by hash)"
        got=0
        for try in 1 2 3; do                          # (gitee.com drops a download now and then)
            if get_sdk "$WORK/ac79-sdk"; then
                got=1
                break
            fi
            echo "build: the download of the SDK stopped (try $try of 3)" >&2
            sleep 15
        done
        [ "$got" = 1 ] || die "could not get JieLi's SDK from $SDK_URL"
    fi
    export AC79_SDK=$WORK/ac79-sdk
fi

# ---- 3. The app ----

step "The app, from $(basename "$ZIP")"
slot=$(basename "$ZIP" .zip | tr -c 'A-Za-z0-9_\n-' '_')
APP=$WORK/apps/$slot/fm1app
mkdir -p "$(dirname "$APP")"
if [ -n "$NAME_SHOWN" ]; then
    python3 "$HERE/make_app.py" --zip "$ZIP" --out "$APP" --index "$REPO/src/index.html" --name "$NAME_SHOWN"
else
    python3 "$HERE/make_app.py" --zip "$ZIP" --out "$APP" --index "$REPO/src/index.html"
fi
NAME=$(cat "$APP/name.txt")
[ -n "$NAME" ] || die "make_app.py gave no name"
DEST=$REPO/build/fwsc/$NAME
if [ -d "$DEST" ]; then
    rm -rf "$DEST"                                    # the package of the build before this one
fi
mkdir -p "$DEST/host"

# ---- 4. The host simulator ----

step "The host simulator: the program runs on this computer"
python3 "$BASE/tools/build.py" "$APP" --host 2>&1 | tee "$DEST/host/build-log.txt"
"$APP/build/host/${NAME}_host" "$APP/tests/smoke.txt" "$DEST/host" 2>&1 | tee "$DEST/host/run-log.txt"

# ---- 5. The firmware ----

step "The firmware for the FM-1"
python3 "$BASE/tools/build.py" "$APP" 2>&1 | tee "$DEST/build-log.txt"
cp "$APP/build/$NAME.fwsc" "$APP/build/$NAME.bin" "$DEST/"
(cd "$DEST" && sha256sum "$NAME.fwsc" > SHA256SUMS)

step "Done"
echo "package   $DEST/$NAME.fwsc"
if command -v wslpath >/dev/null 2>&1; then
    echo "          $(wslpath -w "$DEST/$NAME.fwsc")"
fi
echo "host run  $DEST/host (pictures of the screen, sound.wav)"
echo "Read firmware/README.md before you install it on an FM-1."
