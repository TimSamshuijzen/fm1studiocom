#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tim Samshuijzen
#
# reference_device.sh - the package that the firmware compiler of FM-1 Studio (src/fwsc) must make for a
# firmware with its own build ("device" in its fm1project.json), made by that firmware's own build.
#
#   firmware/web/reference_device.sh CHECKOUT [ARGUMENT...]
#
# CHECKOUT is a folder with the firmware's repository at the commit of the gallery: it has tools/build.py.
# Each ARGUMENT goes to that build.py, as --release 1.8.2 for the release build of Felucca.
# For a project with changes, unzip its project zip over the checkout first.
# The script copies CHECKOUT below ~/.cache/fm1studio/device-ref and runs its tools/build.py there with the
# firmware compiler (opentc.py). All checks of build.py run, also those that need a disassembly (with JieLi's
# objdump). The result is in build/fwsc-reference/NAME/, NAME being the name of the folder CHECKOUT: the
# package, build-log.txt and SHA256SUMS. The browser's package of the same project must have that hash.
# It runs on Linux x86-64 (on Windows: in WSL), after firmware/build.sh and firmware/web/build_compiler.sh.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(dirname "$(dirname "$HERE")")
WORK=${FM1_WORK:-$HOME/.cache/fm1studio}
export FM1_WASM=${FM1_WASM:-$WORK/wasm}

[ $# -ge 1 ] || { echo "usage: firmware/web/reference_device.sh CHECKOUT [ARGUMENT...]" >&2; exit 2; }
SRC=$1
shift                                                 # the other arguments are for build.py
case "$SRC" in
    [A-Za-z]:[\\/]*) SRC=$(wslpath -u "$SRC") ;;      # a Windows path, in WSL
esac
[ -f "$SRC/tools/build.py" ] || { echo "reference_device: $SRC has no tools/build.py" >&2; exit 1; }
[ -d "$WORK/ac79-sdk" ] || { echo "reference_device: run firmware/build.sh one time first" >&2; exit 1; }

# the firmware compiler (src/fwsc, run by Node) as a toolchain folder
[ -e "$FM1_WASM/opentc/pi32v2/bin/clang" ] || python3 "$HERE/opentc.py" --make "$FM1_WASM/opentc" > /dev/null

NAME=$(basename "$(cd "$SRC" && pwd)")
COPY=$WORK/device-ref/$NAME
rm -rf "$COPY"                                        # the copy of the build before this one
mkdir -p "$WORK/device-ref"
cp -r "$SRC" "$COPY"
rm -rf "$COPY/.git" "$COPY/build"
DEST=$REPO/build/fwsc-reference/$NAME
rm -rf "$DEST"                                        # the reference of the build before this one
mkdir -p "$DEST"

export JIELI_TOOLCHAIN=$FM1_WASM/opentc JIELI_DOCKER=0
export AC79_SDK=${AC79_SDK:-$WORK/ac79-sdk}
(cd "$COPY" && python3 tools/build.py "$@") 2>&1 | tee "$DEST/build-log.txt"
cp "$COPY"/build/*.fwsc "$DEST/"
(cd "$DEST" && sha256sum ./*.fwsc | tee SHA256SUMS)
