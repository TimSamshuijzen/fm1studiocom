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
| `reference_device.sh` | A checkout of a firmware with its own build (`device`), and arguments for its `build.py` | `build/fwsc-reference/NAME/`: the package, `SHA256SUMS`, `build-log.txt` |

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

For a firmware with its own build (`device` in its `fm1project.json`), with the commit or tag of the
gallery:

```
git clone https://github.com/twochannels/AURORA ~/aurora
git -C ~/aurora checkout d55484cd2eb86d3de316c1ec02ee444a7f2c18fe
bash firmware/web/reference_device.sh ~/aurora

git clone --branch v0.3.4 https://github.com/twochannels/CS-1 ~/cs-1
bash firmware/web/reference_device.sh ~/cs-1

git clone --branch 1.0 https://github.com/PatttF/fm-106 ~/fm-106
rm ~/fm-106/splash.png
FELUCCA_BLE=0 bash firmware/web/reference_device.sh ~/fm-106

git clone --recurse-submodules --branch v2.5.12 https://github.com/ruppert/fmini-1 ~/fmini-1
bash firmware/web/reference_device.sh ~/fmini-1

git clone --recurse-submodules --branch v1.8.2 https://github.com/hugelton/Felucca ~/felucca
bash firmware/web/reference_device.sh ~/felucca --release 1.8.2

git clone --branch v2.0.2 https://github.com/GRiMMBIENT/grimm ~/grimm
(unzip the project zip of the gallery's GR!MM over ~/grimm, and change tools/stack_check.py: see below)
bash firmware/web/reference_device.sh ~/grimm --release 2.0.2
```

The arguments after the checkout go to its `build.py`.

For FM-106: `FELUCCA_BLE=0` is the build without Bluetooth, the one that the browser build does. Without
`splash.png`, its `build.py` uses the `firmware/src/splash_data.h` of the repository, as the browser
build does (it does not have `splash.png`, and making the header again needs Pillow).

For FMINI-1 and Felucca: their `build.py` makes the headers of `build/gen` with its own generators
(Pillow with Raqm, and fontTools for the icons); the browser build has them from `src/extras/fmini-1.js`
and `src/extras/felucca.js`, and the two must be the same. FMINI-1 shows the date of its build
(`__DATE__`): make its reference on the same day as the browser's package. Felucca is built as its
releases are (`--release 1.8.2`: the identity `FM-1_918`), as the browser build does.

For GR!MM: the gallery changes five of its files (the flash chip, and `grimm.c` for the browser, in
`#ifdef GRIMM_WEB`); the checkout needs the same files, from the project zip that FM-1 Studio saves. Its
`build.py` runs `tools/stack_check.py` on the disassembly, and that check stops the build at two functions
of this compiler's code that call through a pointer and that its `TARGETS` do not have: `fl_erase4k` (it
calls the flash driver's RAM code, as the functions in `TARGETS` with `RAMT` do) and `knob_param` (its
jump table, `tbh`, has bytes that objdump shows as `call r4`). Add `"fl_erase4k": RAMT, "knob_param": []`
to `TARGETS` in the checkout: the check then passes, and the package is the same bytes.

This copies the checkout below `~/.cache/fm1studio/device-ref` and runs the firmware's own
`tools/build.py` there with `opentc.py`. All checks of that `build.py` run, also those that need a
disassembly. For a project with changes, unzip its project zip over the checkout first. The name of the
folder gives the folder of the result. The browser's package had the same hash for AURORA
0.9.1 unchanged and with a change in `firmware/src/aurora.c`, and for CS-1 0.3.4 unchanged and with
AURORA's flash chip change (a new `firmware/hal/fm1_flash_id.h`, four changed lines) (November 2026). CS-1
as FM-1 Studio's gallery loads it, with the `patches` of its entry (the same change, with `FL_JEDEC_OK` in
`firmware/hal/fm1_flash.h`), gives the same package as that second one. For FM-106 1.0 without Bluetooth,
unchanged and with a changed preset name in `firmware/src/fm106_presets.h`, the hashes are the same too.
For FMINI-1 v2.5.12, unchanged and with a changed preset name in `firmware/src/eng_fmini_presets.h`, the
hashes are the same, and the headers that its generators made are the same as those of the extras file.
For Felucca v1.8.2 (release build), unchanged and with a changed menu name in
`firmware/src/menu_items.c`, the same holds. For GR!MM v2.0.2 (release build) with the gallery's changes,
the hashes are the same, also with `grimm.c` as released: the changes for the browser do not reach the
build for the FM-1.

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

- **Tested on one FM-1.** Packages of this compiler were tested on an FM-1 with the newer flash chip
  (B36014) and m-bootloader 2.4-395e6cc (November 2026). Not tried on the first flash chip.
- **Step 3 of `build_compiler.sh` did not run** in the build that made the files of `src/fwsc`. The
  builtins library of that build came from the Windows package of the same release.
- **One build, on one computer.** It is not known if a second build gives the same bytes.
- **The build steps are there two times.** `firmware/make_app.py` and fm1-base's `build.py` do them in
  Python. The browser build that uses `src/fwsc` (`firmware-worker.js` in `src/index.html`) does them in
  JavaScript. A change in one must be made in the other, or the hashes are not the same. The same holds
  for the `build.py`, `fm1pkg_make.py`, `lz4blk.py` and `size_fns.py` of the firmwares with their own
  build, and the `"dinghy"` and `"felucca"` builds of the worker.

## Licences

The scripts in this directory are GPL-3.0-only. [src/fwsc/README.md](../../src/fwsc/README.md) lists the
parts of `src/fwsc` and their licences.

## Used by

The firmware compiler of [FM-1 Studio](https://github.com/TimSamshuijzen/fm1studiocom): the files of
`src/fwsc` that these scripts make. `reference.sh` and `reference_device.sh` check its packages.
