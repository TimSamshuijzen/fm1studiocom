#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tim Samshuijzen
"""Makes src/fwsc/platform.json: all that the page needs, besides the compiler, to build a .fwsc package.

    python3 firmware/web/make_platform.py

It runs on Linux (on Windows: in WSL), after firmware/build.sh was run one time (that gets fm1-base,
felucca-bsp and the three files of JieLi's SDK into ~/.cache/fm1studio) and after
firmware/web/build_compiler.sh. It needs Pillow with Raqm, as fm1-base's build does.

platform.json has:

    text      path -> text:
                fm1-base/device, include, lib             the platform (fm1-base, MPL-2.0)
                fm1-base/bsp/chip, bsp/boards/fm1         the chip and board layer (felucca-bsp, MPL-2.0)
                app/                                      the adapter (firmware/adapter), placed as make_app.py does
                gen/                                      the two headers that fm1-base's build generates
                res/include/                              the C headers of clang (stdint.h, stddef.h, ...)
                and the licence files of fm1-base and felucca-bsp
    binary    path -> base64:
                ota.bin                                   the update loader, built here with the same compiler
                sdk/                                      the three files of JieLi's AC79 SDK (Apache-2.0)
                rt/libclang_rt.builtins.a                 compiler-rt's builtins for pi32v2
    about     the versions of the parts

The update loader and the two generated headers are the same for each program, so they are made here
one time. The page compiles all C and assembly files itself.
"""
import base64
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
WORK = Path(os.environ.get("FM1_WORK") or Path.home() / ".cache/fm1studio")
BASE = WORK / "fm1-base"
BSP = BASE / "bsp"
SDK = Path(os.environ.get("AC79_SDK") or WORK / "ac79-sdk")

sys.dont_write_bytecode = True     # (no __pycache__ folders in firmware/)
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(BASE / "tools"))
import opentc      # noqa: E402  (beside this file)
import make_app    # noqa: E402  (firmware/make_app.py)

# The headers of clang that a C program for a small computer can use. The other 500 are for other processors.
C_HEADERS = re.compile(r"(__std\w+|__float\w+|std\w+|float|limits|iso646|inttypes|tgmath)\.h")
LOADER_FLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]   # build.py CFLAGS


def fail(message):
    raise SystemExit(f"make_platform: {message}")


def revision(folder):
    done = subprocess.run(["git", "-C", str(folder), "rev-parse", "HEAD"], capture_output=True, text=True)
    return done.stdout.strip() or "unknown"


def run_tool(code_and_text, what):
    code, text = code_and_text
    sys.stderr.write(text)
    if code:
        fail(f"{what} failed")


def generated(temp):
    """The two headers of fm1-base's generate(): the fonts, and the words of the screen reader (none)."""
    env = {**os.environ, "FM1_FONT_FULL": "", "FM1_FONT_TRIM": "", "FM1_FONT_PACK": ""}
    (temp / "words.tsv").write_text(make_app.WORDS, newline="\n")
    for command in ([BASE / "tools/gen_font.py", temp / "felucca_font.h"],
                    [BASE / "tools/gen_words.py", temp / "words.tsv", temp / "web/words", temp / "ann_words.h"]):
        done = subprocess.run([sys.executable, *map(str, command)], capture_output=True, text=True, env=env)
        if done.returncode:
            sys.stderr.write(done.stderr)
            fail(f"{command[0].name} failed")
    return {name: (temp / name).read_bytes() for name in ("felucca_font.h", "ann_words.h")}


def loader(temp):
    """ota.bin: felucca-bsp's update loader, as build_loader() of fm1-base's build.py makes it."""
    import build                                     # fm1-base/tools/build.py: ldr_wrap
    source = "fm1-base/bsp/loader"
    run_tool(opentc.clang(["-target", "pi32v2", "-c", f"{source}/crt0_ldr.S", "-o", str(temp / "crt0_ldr.o")], WORK),
             "the loader's crt0_ldr.S")
    run_tool(opentc.clang(["-target", "pi32v2", *LOADER_FLAGS, "-Ifm1-base/bsp/chip", "-Ifm1-base/bsp/boards/fm1",
                           "-c", f"{source}/loader.c", "-o", str(temp / "loader.o")], WORK), "the loader's loader.c")
    run_tool(opentc.lld(["--gc-sections", "-e", "_start", "-T", f"{source}/loader.ld", str(temp / "crt0_ldr.o"),
                         str(temp / "loader.o"), "-o", str(temp / "loader.elf")], WORK), "the loader's link")
    elf = (temp / "loader.elf").read_bytes()
    # the loader writes the flash again: none of it can be at a flash address
    shoff, = struct.unpack_from("<I", elf, 0x20)
    size, count = struct.unpack_from("<HH", elf, 0x2E)
    for index in range(count):
        head = struct.unpack_from("<10I", elf, shoff + index * size)
        if head[5] and head[3] >= 0x02000000:
            fail(f"the loader has a section at {head[3]:#x}: that is not RAM")
    image = opentc.section(elf, ".text")
    if not image or len(image) > 0x14000:
        fail("the loader's image is empty or too large")
    return build.ldr_wrap(image)


def main():
    for folder in (BASE / "device", BSP / "chip", SDK / "cpu/wl82/tools"):
        if not folder.is_dir():
            fail(f"no {folder}. Run firmware/build.sh one time first: it gets fm1-base, felucca-bsp and the SDK files.")
    import build                                     # fm1-base/tools/build.py: SDK_SHA256
    text, binary = {}, {}

    def add(key, data):
        """A file goes to "text" if it is text that comes back as the same bytes, else to "binary"."""
        try:
            decoded = data.decode("utf-8")
        except UnicodeDecodeError:
            decoded = None
        if decoded is not None and "\0" not in decoded and decoded.encode("utf-8") == data:
            text[key] = decoded
        else:
            binary[key] = base64.b64encode(data).decode("ascii")

    def add_tree(folder, key):
        for path in sorted(folder.rglob("*")):
            parts = path.relative_to(folder).parts
            if path.is_file() and "build" not in parts and "__pycache__" not in parts:
                add(f"{key}/{'/'.join(parts)}", path.read_bytes())

    for name in ("device", "include", "lib"):
        add_tree(BASE / name, f"fm1-base/{name}")
    add_tree(BSP / "chip", "fm1-base/bsp/chip")
    add_tree(BSP / "boards/fm1", "fm1-base/bsp/boards/fm1")
    for name in ("LICENSE", "LICENSING.md", "CREDITS.md", "LICENSES/Apache-2.0.txt", "assets/fonts/Barlow-OFL.txt",
                 "bsp/LICENSE", "bsp/LICENSING.md", "bsp/NOTICE"):
        if (BASE / name).is_file():
            add(f"fm1-base/{name}", (BASE / name).read_bytes())

    adapter = REPO / "firmware/adapter"
    for path in sorted(adapter.rglob("*")):
        if path.is_file():
            rel = path.relative_to(adapter).as_posix()
            add(f"app/{rel}" if rel in ("app.h", "app.c") else f"app/adapter/{rel}", path.read_bytes())

    resource = opentc.first("build/lib/clang/*", "clang headers") / "include"
    for path in sorted(resource.iterdir()):
        if path.is_file() and C_HEADERS.fullmatch(path.name):
            add(f"res/include/{path.name}", path.read_bytes())

    for rel, sha in build.SDK_SHA256.items():
        data = (SDK / "cpu/wl82/tools" / rel).read_bytes()
        if hashlib.sha256(data).hexdigest() != sha:
            fail(f"the SDK file {rel} is not the one of AC79NN_SDK_V1.2.1 that fm1-base was tested with")
        binary[f"sdk/{Path(rel).name}"] = base64.b64encode(data).decode("ascii")
    runtime = opentc.WASM / "rt/libclang_rt.builtins.a"
    binary["rt/libclang_rt.builtins.a"] = base64.b64encode(runtime.read_bytes()).decode("ascii")

    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        for name, data in generated(temp).items():
            add(f"gen/{name}", data)
        ota = loader(temp)
    binary["ota.bin"] = base64.b64encode(ota).decode("ascii")

    llvm = opentc.WASM / "llvm-project"
    about = {
        "fm1-base": revision(BASE),
        "felucca-bsp": revision(BSP),
        "pi32v2-llvm": revision(llvm),
        "sdk": "AC79NN_SDK_V1.2.1_2023-12-13",
        "loader": f"{len(ota)} bytes, sha256 {hashlib.sha256(ota).hexdigest()}",
    }
    out = REPO / "src/fwsc/platform.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps({"about": about, "text": text, "binary": binary}, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":")) + "\n"
    out.write_text(data, encoding="utf-8", newline="\n")
    print(f"make_platform: {out}")
    print(f"  {len(text)} text files, {len(binary)} binary files, {len(data.encode('utf-8'))} bytes")
    for key, value in about.items():
        print(f"  {key:12} {value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
