#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tim Samshuijzen
"""The WebAssembly build of the open pi32v2 compiler, as a toolchain folder for fm1-base's tools/build.py.

    opentc.py --make DIR      makes DIR with the layout of JieLi's toolchain: pi32v2/bin/clang, pi32v2/bin/ld,
                              common/bin/objcopy, common/bin/objdump. Each one is a link to this file.

    JIELI_TOOLCHAIN=DIR python3 fm1-base/tools/build.py APP_DIR

Then build.py builds a package with the firmware compiler of FM-1 Studio (src/fwsc/llvm.wasm, run by Node
here). That package is the reference for the browser's package: they must be the same bytes.
make_platform.py uses the functions of this file too, for the update loader.

    clang     the driver cannot start a process in WebAssembly. So "clang -###" gives its jobs, and each job
              is a run of its own. The flags are those of JieLi's clang, but -mfprev1 (part of -mcpu=r3), and
              with -std=gnu11 for a C file (as pi32v2-isa's drop-in toolchain does)
    ld        ld.lld, with compiler-rt's builtins at the end
    objcopy   "-O binary -j SECTION ELF OUT" only: the bytes of one section
    objdump   JieLi's own (for the checks of build.py: it reads the same ELF files)

FM1_PROGRAM_LEVEL=-Os compiles the files of the program (APP_DIR/project) at that level in place of -O2:
the level of the browser build (src/fwsc/README.md tells why).
Folders: FM1_WASM (default ~/.cache/fm1studio/wasm), FM1_JIELI (default ~/.cache/fm1studio/jieli/toolchain).
"""
import os
import shlex
import struct
import subprocess
import sys
from pathlib import Path

WASM = Path(os.environ.get("FM1_WASM") or Path.home() / ".cache/fm1studio/wasm")
JIELI = Path(os.environ.get("FM1_JIELI") or Path.home() / ".cache/fm1studio/jieli/toolchain")
TOOLS = ("pi32v2/bin/clang", "pi32v2/bin/ld", "common/bin/objcopy", "common/bin/objdump")


def fail(message):
    raise SystemExit(f"opentc: {message}")


def first(pattern, what):
    found = sorted(WASM.glob(pattern))
    if not found:
        fail(f"no {what} in {WASM} ({pattern}). Run firmware/web/build_compiler.sh first.")
    return found[-1]


def llvm(args, cwd=None):
    """One run of the WebAssembly program: llvm(["clang", ...]) or llvm(["ld.lld", ...])."""
    command = [str(first("emsdk/node/*/bin/node", "Node")), str(first("out/llvm-node.js", "compiler")), *args]
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True)


def clang(args, cwd=None):
    """clang with the arguments of fm1-base's build. Returns (exit code, the compiler's text)."""
    args = [a for a in args if a != "-mfprev1"]
    source = args[args.index("-c") + 1] if "-c" in args else ""
    level = os.environ.get("FM1_PROGRAM_LEVEL")
    if level and "/project/" in source and "-O2" in args:
        args[args.index("-O2")] = level               # the build's own -O2 only: a later one is the project's flag
    if source.endswith(".c"):
        args = ["-std=gnu11", *args]
    resource = first("build/lib/clang/*", "clang headers")
    listed = llvm(["clang", "-###", "-resource-dir", str(resource), *args], cwd)
    jobs = [shlex.split(line) for line in listed.stderr.splitlines() if line.startswith(' "')]
    if listed.returncode or not jobs:
        return listed.returncode or 1, listed.stderr
    text = ""
    for job in jobs:                                  # a job is: "<path of the program>" "clang" "-cc1" ...
        done = llvm(job[1:], cwd)
        text += done.stdout + done.stderr
        if done.returncode:
            return done.returncode, text
    return 0, text


def lld(args, cwd=None):
    """ld.lld with the arguments of GNU ld as fm1-base gives them. Returns (exit code, text)."""
    runtime = WASM / "rt" / "libclang_rt.builtins.a"
    if not runtime.exists():
        fail(f"no {runtime}. Run firmware/web/build_compiler.sh first.")
    done = llvm(["ld.lld", *args, str(runtime)], cwd)
    return done.returncode, done.stdout + done.stderr


def section(data, name):
    """The bytes of the section `name` of a 32-bit little-endian ELF file (none if it is not there, or has no bytes)."""
    shoff, = struct.unpack_from("<I", data, 0x20)
    size, count, names = struct.unpack_from("<HHH", data, 0x2E)
    heads = [struct.unpack_from("<10I", data, shoff + i * size) for i in range(count)]
    strings = heads[names][4]
    for head in heads:
        end = data.index(b"\0", strings + head[0])
        if data[strings + head[0]:end].decode() == name and head[1] != 8:      # 8: SHT_NOBITS
            return data[head[4]:head[4] + head[5]]
    return b""


def make(folder):
    folder = Path(folder)
    if folder.exists():
        fail(f"{folder} is there already")
    for tool in TOOLS:
        (folder / tool).parent.mkdir(parents=True, exist_ok=True)
        (folder / tool).symlink_to(Path(__file__).resolve())
    (folder / "pi32v2/bin/LLVMgold.so").write_bytes(b"")      # (build.py names it only for JieLi's libraries)
    print(folder)


def main():
    tool, args = Path(sys.argv[0]).name, sys.argv[1:]
    if tool == "clang":
        code, text = clang([a for a in args])
    elif tool == "ld":
        code, text = lld(args)
    elif tool == "objcopy":
        if len(args) != 6 or args[:3:2] != ["-O", "-j"] or args[1] != "binary":
            fail(f"objcopy: only -O binary -j SECTION ELF OUT, not {args}")
        Path(args[5]).write_bytes(section(Path(args[4]).read_bytes(), args[3]))
        return 0
    elif tool == "objdump":
        real = JIELI / "common/bin/objdump"
        os.execv(real, [str(real), *args])
    elif args[:1] == ["--make"] and len(args) == 2:
        make(args[1])
        return 0
    else:
        print(__doc__, file=sys.stderr)
        return 2
    sys.stderr.write(text)
    return code


if __name__ == "__main__":
    sys.exit(main())
