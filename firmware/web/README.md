# The firmware build in the page

The FIRMWARE button of FM-1 Studio makes a `.fwsc` package in the browser. It needs three files in
`src/fwsc`. The scripts in this folder make them. They run on Linux x86-64 (on Windows: in WSL).

| File in `src/fwsc` | What | Made by |
| --- | --- | --- |
| `llvm.js`, `llvm.wasm` | clang and ld.lld for the FM-1's processor (pi32v2), as WebAssembly | `build_compiler.sh` |
| `platform.json` | The sources of fm1-base and felucca-bsp, the adapter, the update loader, the SDK files | `make_platform.py` |

> **Status: not tried on an FM-1.** The packages of the page are the same bytes as those of fm1-base's own
> build with this compiler, and they pass that build's checks (November 2026). No package was installed
> on a real FM-1 yet. The compiler is new: its authors call it experimental.

## Why another compiler

JieLi's compiler for the FM-1 is a program for Linux x86-64 with no source. It cannot run in a browser.
[pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) by Julius Vitkauskas is LLVM 23 with a target
for the same processor, with source. Its documents say that its objects use the conventions of JieLi's
compiler, and that firmware built with it runs on an FM-1. `build_compiler.sh` builds that source with
Emscripten.

So the two builds of this repository use different compilers:

| | `firmware/build.sh` | The FIRMWARE button |
| --- | --- | --- |
| Compiler | JieLi's clang 4.0.1 | pi32v2-llvm, clang 23.1.5 |
| The program is compiled at | `-O2` | `-Os` |
| Runs the program in fm1-base's host simulator | yes | no |
| All checks of fm1-base's build | yes | the ones that a program can fail (below) |

## Make the files

One time, `firmware/build.sh` must have run: it gets fm1-base, felucca-bsp and the three SDK files into
`~/.cache/fm1studio`. Then:

    sudo apt install -y g++ cmake ninja-build       (one time)
    firmware/web/build_compiler.sh                  (about half an hour the first time, 6 GB)
    python3 firmware/web/make_platform.py

`build_compiler.sh` downloads the source of pi32v2-llvm, the Emscripten SDK, and the fork's release
package (for one library), all at fixed versions, into `~/.cache/fm1studio/wasm`. Nothing is installed
outside that folder. Run `make_platform.py` again after a change in `firmware/adapter`, or with a newer
fm1-base.

## Compare with fm1-base's own build

    firmware/web/reference.sh PROJECT.zip

This builds the project with fm1-base's `tools/build.py`, as `firmware/build.sh` does, but with the
compiler of the page (`opentc.py` gives it to `build.py` as a toolchain folder, run by Node). It prints
the SHA-256 of the package, and puts it in `build/fwsc-reference/NAME/`. The page must make the same bytes
from the same project with the same name. Tested with the three examples, and with a project that has a
folder, an include path, defines and a flag.

All checks of `build.py` run in this build, also the ones that the page does not have. Use it before
you give a package to other persons.

## What the page does

The worker (`firmware-worker.js` in `src/index.html`) does the steps of `firmware/make_app.py` and of
`build_app` in fm1-base's `tools/build.py`, with the same file names and compiler flags:

1. It makes the app: the files of the project, `fm1.h`, the adapter, `fm1.toml` and two small headers.
2. It calculates the number of the build, a hash of the sources, as `build.py` does.
3. It compiles three assembly files, `device/unity.c` (the platform and the adapter), each C file of the
   program, and `fm1_libc.c`.
4. It links them with fm1-base's linker script.
5. It makes the image from three sections of the ELF file.
6. It runs the checks.
7. It packs the image, the update loader and the SDK files (`fm1pkg_make.py` of fm1-base, in JavaScript).

Things that are special for WebAssembly:

- **One program.** `llvm.wasm` is LLVM's multicall tool: the first argument (`clang`, `ld.lld`) selects
  the tool. It has no objcopy: the worker takes the bytes of a section from the ELF file itself.
- **A new instance for each run.** A tool cannot run two times in one instance. The module is compiled
  one time.
- **No processes.** clang's driver starts a process for each job, which a browser cannot do. So the
  worker runs `clang -###`, which prints the jobs, and then runs each job itself.

## The checks

| Check of fm1-base's build | In the page |
| --- | --- |
| The entry point and the first bytes of the image | yes |
| No `double` routines | yes |
| The image fits the flash; 8 KB of memory stays free | yes |
| No hardware address in the program (pointer cast to `volatile`, assembly, register names) | yes, for the files of the program |
| No calls in the RAM code | no: the program cannot put code there (see below) |
| No address of the chip's ROM | no |
| 64-bit multiplies beside loads (a note) | no: see `-Os` below |

The platform is the same source for each program, and it passes all checks in the reference build. The
page checks that an object file of the program has only sections for code and data: so a program cannot
put code or data in the parts of the memory that are for the platform.

## Why `-Os`

At `-O2`, pi32v2-llvm makes a division by a constant with a 64-bit multiply. felucca-bsp's `docs/CPU.md`
tells that such multiplies beside loads can change a register on the FM-1, and fm1-base's build gives a
note when it finds them. JieLi's compiler does not make them. At `-Os`, pi32v2-llvm does not make them
also. So the page compiles the program at `-Os`. A project can add `"flags": ["-O2"]` to its
`fm1project.json`: then build it with `reference.sh` too, and read the note.

How fast a program runs on the FM-1 at `-Os` is not known.

## Licences

The scripts in this folder are GPL-3.0-only, as FM-1 Studio. `src/fwsc/README.md` lists the parts of
`src/fwsc` and their licences.
