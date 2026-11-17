#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tim Samshuijzen
#
# reference.sh - the package that the firmware compiler of FM-1 Studio (src/fwsc) must make for an fm1.h
# project, made by fm1-base's own build.
#
#   firmware/web/reference.sh PROJECT.zip
#
# It makes the app with firmware/make_app.py and builds it with fm1-base's tools/build.py, as
# firmware/build.sh does, but with the firmware compiler (src/fwsc/llvm.wasm, run by Node: opentc.py) and
# the program at -Os. All checks of build.py run. The result is in build/fwsc-reference/NAME/, with the
# SHA-256 of the package: the browser's package of the same project must have that hash.
#
# The name of the zip file gives the name of the firmware: in FM-1 Studio, the NAME field of the project.
# It runs on Linux x86-64 (on Windows: in WSL), after firmware/build.sh and firmware/web/build_compiler.sh.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(dirname "$(dirname "$HERE")")
WORK=${FM1_WORK:-$HOME/.cache/fm1studio}
export FM1_WASM=${FM1_WASM:-$WORK/wasm}

[ $# -eq 1 ] || { echo "usage: firmware/web/reference.sh PROJECT.zip" >&2; exit 2; }
ZIP=$1
case "$ZIP" in
    [A-Za-z]:[\\/]*) ZIP=$(wslpath -u "$ZIP") ;;      # a Windows path, in WSL
esac
[ -f "$ZIP" ] || { echo "reference: there is no file $ZIP" >&2; exit 1; }
[ -d "$WORK/fm1-base/tools" ] || { echo "reference: run firmware/build.sh one time first" >&2; exit 1; }

# the firmware compiler (src/fwsc, run by Node) as a toolchain folder
[ -e "$FM1_WASM/opentc/pi32v2/bin/clang" ] || python3 "$HERE/opentc.py" --make "$FM1_WASM/opentc" > /dev/null

# the same place below ~/.cache/fm1studio as in the page's file system
APP=$WORK/apps/web/fm1app
python3 "$HERE/../make_app.py" --zip "$ZIP" --out "$APP" --index "$REPO/src/index.html"
NAME=$(cat "$APP/name.txt")
DEST=$REPO/build/fwsc-reference/$NAME
if [ -d "$DEST" ]; then
    rm -rf "$DEST"                                    # the reference of the build before this one
fi
mkdir -p "$DEST"

export JIELI_TOOLCHAIN=$FM1_WASM/opentc JIELI_DOCKER=0 FM1_PROGRAM_LEVEL=-Os
export AC79_SDK=${AC79_SDK:-$WORK/ac79-sdk}
python3 "$WORK/fm1-base/tools/build.py" "$APP" 2>&1 | tee "$DEST/build-log.txt"
cp "$APP/build/$NAME.fwsc" "$APP/build/$NAME.bin" "$DEST/"
(cd "$DEST" && sha256sum "$NAME.fwsc" "$NAME.bin" | tee SHA256SUMS)
