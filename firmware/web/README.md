# firmware/web: the scripts that make src/fwsc

This directory has the scripts that make the files of [src/fwsc](../../src/fwsc/README.md): clang and
ld.lld for JieLi's pi32v2 processor as WebAssembly, and `platform.json`. It also has a tool that gives
this compiler to fm1-base's build (`tools/build.py`) in place of JieLi's compiler, and a script that
makes a reference package with it.

The scripts run on Linux x86-64 (on Windows: in WSL). They write to `~/.cache/fm1studio`, to `src/fwsc`
and to `build/`. Nothing is installed outside those folders.

## Contents

| File | Input | Output |
| --- | --- | --- |
| `build_compiler.sh` | Three downloads (see "Origins") | `src/fwsc/llvm.js`, `llvm.wasm` and `licences/`. In `~/.cache/fm1studio/wasm`: the same compiler for Node (`out/llvm-node.js`), clang's headers, the builtins library |
| `make_platform.py` | fm1-base, felucca-bsp and the SDK files in `~/.cache/fm1studio`; `firmware/adapter`; the compiler for Node | `src/fwsc/platform.json` |
| `opentc.py` | The compiler for Node | A toolchain folder for fm1-base's `tools/build.py` |
| `reference.sh` | A project zip | `build/fwsc-reference/NAME/`: the package, the image, `SHA256SUMS`, `build-log.txt` |
| `reference_device.sh` | A checkout of a firmware with its own build (AURORA) | `build/fwsc-reference/NAME/`: the package, `SHA256SUMS`, `build-log.txt` |

## Origins

The scripts are written for this repository. `build_compiler.sh` downloads these. The versions are at
the top of the script:

| What | Version | From | Checked by |
| --- | --- | --- | --- |
| The source of [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) (LLVM 23.1.5 with a pi32v2 target) | Commit `60b0340f47083cf088903030a9b55d8a65525dde`, release `pi32v2-23.1.5-5` | github.com | The commit |
| The Emscripten SDK | 7.0.0 | github.com/emscripten-core/emsdk | The version. The commit of the emsdk repository itself is not fixed |
| `pi32v2-llvm-23.1.5-5-linux-x64.tar.xz`, for one file: `libclang_rt.builtins.a` | Release `pi32v2-23.1.5-5` | The releases of pi32v2-llvm | SHA-256 `a20e257b840eb6b212761bfa93a7f3804502940d0035d62790da3d42fefa31e3` |

`make_platform.py` downloads nothing. It uses fm1-base, felucca-bsp and the three files of JieLi's SDK
that `firmware/build.sh` got. [firmware/README.md](../README.md) gives their versions.

## Make the files

The computer needs:

- One run of `firmware/build.sh`, with the packages that it needs. `make_platform.py` uses its
  downloads, and Pillow with Raqm for the font header.
- `sudo apt install -y g++ cmake ninja-build`
- 6 GB of disk space. A compile job takes up to 1 GB of memory, a link 2 GB.

```
firmware/web/build_compiler.sh
python3 firmware/web/make_platform.py
```

Then put the new hashes in the table of `src/fwsc/README.md`.

`build_compiler.sh` takes about half an hour with 6 compile jobs. It does a step only when the result of
that step is not there:

1. It gets the source of pi32v2-llvm.
2. It gets and installs the Emscripten SDK.
3. It gets the builtins library from the release package.
4. It builds LLVM's tablegen tools for this computer. Step 5 runs them.
5. It builds LLVM's multicall tool `llvm`, with clang and lld only, with Emscripten.
6. It links that tool two times: for a browser (`llvm.js`: the function `createLLVM`, files in memory)
   and for Node (`llvm-node.js`: the files of the computer).
7. It copies `llvm.js`, `llvm.wasm` and the licence texts to `src/fwsc`.

`make_platform.py` collects the text files, makes the two headers that fm1-base's build generates (with
fm1-base's own tools), builds the update loader with the compiler for Node (as `build_loader` of
`build.py` does), and checks the SDK files by hash. Run it again after a change in `firmware/adapter`
or of `WORDS` in `firmware/make_app.py`, after a change of the fixed commits in `firmware/build.sh`, and
after a new build of the compiler.

| Variable | What | Default |
| --- | --- | --- |
| `FM1_WORK` | The folder of `firmware/build.sh` | `~/.cache/fm1studio` |
| `FM1_WASM` | The folder of `build_compiler.sh` | `~/.cache/fm1studio/wasm` |
| `JOBS` | Compile jobs at the same time | 6 |
| `AC79_SDK` | The folder with the SDK files | `~/.cache/fm1studio/ac79-sdk` |

## Use the compiler with fm1-base's build

`opentc.py` makes a folder with the layout of JieLi's toolchain. `tools/build.py` of fm1-base then uses
the open compiler, without a change in fm1-base:

```
python3 firmware/web/opentc.py --make DIR
JIELI_TOOLCHAIN=DIR JIELI_DOCKER=0 AC79_SDK=~/.cache/fm1studio/ac79-sdk python3 ~/.cache/fm1studio/fm1-base/tools/build.py APP_DIR
```

Each tool in `DIR` is a link to `opentc.py`:

| Tool | What it does |
| --- | --- |
| `pi32v2/bin/clang` | Runs clang from `llvm-node.js`. It gets the jobs with `clang -###` and runs each job, because the driver cannot start a process in WebAssembly. It removes `-mfprev1` (a flag of JieLi's clang) and adds `-std=gnu11` for a C file |
| `pi32v2/bin/ld` | Runs ld.lld, with `libclang_rt.builtins.a` as the last file |
| `common/bin/objcopy` | Only `-O binary -j SECTION ELF OUT`: it reads the section from the ELF file itself |
| `common/bin/objdump` | Runs JieLi's objdump (`FM1_JIELI`, default `~/.cache/fm1studio/jieli/toolchain`). The checks of `build.py` use it |

`FM1_PROGRAM_LEVEL=-Os` compiles the files of the program (`APP_DIR/project`) at that level in place
of `-O2`. A flag of the project itself stays as it is.

## Check a package

```
firmware/web/reference.sh PROJECT.zip
```

This makes the app with `firmware/make_app.py` and builds it with `build.py` and `opentc.py`, with the
program at `-Os`. All checks of `build.py` run. It prints the SHA-256 of the package and of the image.

A package that a browser makes with the files of `src/fwsc` must have the same hash, for the same project
and the same name. The name of the zip file gives the name of the firmware. For three example projects,
and for a project with a folder, an include path, defines and a flag, the hashes are the same
(November 2026).

For AURORA, which has its own build (`device` in its `fm1project.json`):

```
git clone https://github.com/twochannels/AURORA ~/aurora
git -C ~/aurora checkout d55484cd2eb86d3de316c1ec02ee444a7f2c18fe
bash firmware/web/reference_device.sh ~/aurora
```

This copies the checkout below `~/.cache/fm1studio/device-ref` and runs AURORA's own `tools/build.py`
there with `opentc.py`. All checks of that `build.py` run, also those that need a disassembly. For a
project with changes, save it in the page (SAVE) and unzip it over the checkout first. The name of the
folder gives the folder of the result. For AURORA 0.9.1 unchanged and with a change in
`firmware/src/aurora.c`, the page's package has the same hash (November 2026).

## Design choices

- **One program for clang and lld.** LLVM's multicall tool is smaller than two programs, and a page
  loads one file.
- **No llvm-objcopy in that program.** With it, the configuration for Emscripten stops. Its work here
  is small: `section()` in `opentc.py`.
- **Two links of the same objects.** The browser and Node need other settings for files. With the same
  compiled code in both, the reference build tests the compiler of the browser.
- **The builtins library is not built here.** The release of pi32v2-llvm has it.
- **The program at `-Os`.** See "Limits" in [src/fwsc/README.md](../../src/fwsc/README.md).

## Limits

- **Not tried on an FM-1.** No package of this compiler was installed on a real FM-1.
- **Step 3 of `build_compiler.sh` did not run** in the build that made the files of `src/fwsc`. The
  builtins library of that build came from the Windows package of the same release.
- **One build, on one computer.** It is not known if a second build gives the same bytes.
- **The build steps are there two times.** `firmware/make_app.py` and fm1-base's `build.py` do them in
  Python. The page that uses `src/fwsc` does them in JavaScript. A change in one must be made in the
  other, or the hashes are not the same. The same holds for AURORA's `build.py`, `fm1pkg_make.py` and
  `lz4blk.py`, and the `"dinghy"` build of the page.

## Licences

The scripts in this directory are GPL-3.0-only. [src/fwsc/README.md](../../src/fwsc/README.md) lists the
parts of `src/fwsc` and their licences.

## Used by

FM-1 Studio: the files of `src/fwsc` are for its FIRMWARE button.
