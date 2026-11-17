# src/fwsc: a compiler for the M-VAVE FM-1 that runs in a browser

This directory has clang and ld.lld for the processor of the M-VAVE FM-1 (JieLi pi32v2), built as
WebAssembly. It also has one file with all sources and binary parts that a firmware on
[fm1-base](https://github.com/charlesvestal/fm1-base) needs. With these files, a web page can compile a
C program and make an installable `.fwsc` package. It needs no server side and no installed compiler.

The files are made by the scripts in [firmware/web](../../firmware/web/README.md). Do not change them by
hand. They are the work of others, under their own licences. Not affiliated with M-VAVE or JieLi.

## Contents

| File | Size | What |
| --- | --- | --- |
| `llvm.js` | 71 KB | The loader that Emscripten made. It defines the function `createLLVM` |
| `llvm.wasm` | 51.9 MB | clang 23.1.5 and ld.lld in one program |
| `platform.json` | 1.8 MB | The platform: 144 text files and 5 binary files (see "platform.json") |
| `licences/` | | The licence texts of LLVM, Emscripten and musl |

## Origins

| File | From | Licence |
| --- | --- | --- |
| `llvm.wasm`, `llvm.js` | [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) by Julius Vitkauskas: LLVM with a target for pi32v2. Commit `60b0340f47083cf088903030a9b55d8a65525dde` (release `pi32v2-23.1.5-5`), built with [Emscripten](https://emscripten.org) 7.0.0 by `firmware/web/build_compiler.sh` | Apache-2.0 with LLVM exceptions; Emscripten's runtime and musl: MIT and others (`licences/`) |
| `platform.json` | See the table below | |

The parts of `platform.json`:

| Part | What | From | Licence |
| --- | --- | --- | --- |
| `fm1-base/device`, `include`, `lib` | The platform of the firmware: screen, keys, LEDs, sound, USB MIDI, update entry, safe mode | [fm1-base](https://github.com/charlesvestal/fm1-base), commit `c8e0599` | MPL-2.0. Its `LICENSE`, `LICENSING.md` and `CREDITS.md` are in the file |
| `fm1-base/bsp` | The chip and board layer | [felucca-bsp](https://github.com/hugelton/felucca-bsp), commit `18eadad` | MPL-2.0, and two Apache-2.0 files of JieLi's SDK. Its `LICENSE`, `LICENSING.md` and `NOTICE` are in the file |
| `gen/felucca_font.h` | The fonts of the platform, as bitmaps | Made by fm1-base's `tools/gen_font.py` from Barlow Semi Condensed (The Barlow Project Authors) | SIL OFL 1.1 (`fm1-base/assets/fonts/Barlow-OFL.txt` in the file) |
| `gen/ann_words.h` | The words of the screen reader (none) | Made by fm1-base's `tools/gen_words.py` | MPL-2.0 |
| `app/` | An fm1-base app: the adapter that gives a program the API `fm1.h` | [firmware/adapter](../../firmware/README.md) of this repository | GPL-3.0-only |
| `res/include` | The C headers of clang (`stdint.h`, `stddef.h`, `stdarg.h`, ...) | pi32v2-llvm | Apache-2.0 with LLVM exceptions |
| `rt/libclang_rt.builtins.a` | compiler-rt's builtins for pi32v2 | The release package of pi32v2-llvm, with the runtime of [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) | Apache-2.0 with LLVM exceptions; MIT-0 |
| `ota.bin` | The update loader of felucca-bsp, built with the compiler above | felucca-bsp `loader/` | MPL-2.0 |
| `sdk/uboot.boot`, `sdk/cfg_tool.bin`, `sdk/eq_cfg_hw.bin` | Files that each package must have | JieLi's AC79 SDK, tag `AC79NN_SDK_V1.2.1_2023-12-13`, [gitee.com/Jieli-Tech](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) | Apache-2.0 (`fm1-base/LICENSES/Apache-2.0.txt` in the file) |

The `about` part of `platform.json` has the full commit of fm1-base, felucca-bsp and pi32v2-llvm.

## Use the compiler

`llvm.js` defines `createLLVM(options)`. It gives a promise of an Emscripten module with a file system
in memory (`FS`) and the function `callMain(args)`. It is built for a page, a worker and Node (there,
`require('./llvm.js')` gives the function). It was used in a worker and in Node.

The rules:

- **The first argument selects the tool:** `clang` or `ld.lld`.
- **One run for each instance.** A tool cannot run two times in one instance. Compile `llvm.wasm` one
  time, and give the compiled module to each new instance with `instantiateWasm`.
- **No processes.** clang's driver starts a process for each job, and WebAssembly cannot do that. Run
  `clang -### ...`: it prints the jobs and runs none. Then run each job as a call of its own. A C file is
  one job. An assembly file (`.S`) is two jobs: the first writes a file in `/tmp` that the second reads,
  so copy that file to the next instance.
- **Files.** Each instance starts with an empty file system. Write the input files before the call, make
  the folder of each output file (`FS.mkdirTree`), and read the output files after the call.
- **Headers.** Give `-resource-dir /res` and write the `res/` files of `platform.json` to `/res`. There
  is no C library and no C++ library: no `stdio.h`, no `malloc`. The adapter in `app/` has the `float`
  maths and the string functions.
- **No objcopy and no objdump.** To get the bytes of a section, read the ELF file (32-bit, low byte
  first) in your own code.
- **The text of a tool** comes through `print` and `printErr`. It can have colour codes.

This example compiles one C file to an object file. It is the body of an `async` function in a worker:

```js
importScripts('fwsc/llvm.js');                             // defines createLLVM
const platform = await (await fetch('fwsc/platform.json')).json();
const compiled = await WebAssembly.compileStreaming(fetch('fwsc/llvm.wasm'));

// One run of a tool, in a new instance. `files` is path -> text or bytes.
async function run(args, files) {
  let text = '';
  const llvm = await createLLVM({
    instantiateWasm(imports, done) {
      WebAssembly.instantiate(compiled, imports).then((instance) => done(instance, compiled));
      return {};
    },
    print: (line) => { text += line + '\n'; },
    printErr: (line) => { text += line + '\n'; },
  });
  for (const [path, data] of Object.entries(files)) {
    llvm.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
    llvm.FS.writeFile(path, data);
  }
  llvm.FS.chdir('/work');
  let code;
  try {
    code = llvm.callMain(args);
  } catch (error) {
    if (typeof error.status !== 'number') throw error;
    code = error.status;                                   // the tool called exit()
  }
  return { code, text, FS: llvm.FS };
}

// clang's C headers, and one source file
const files = { '/work/add.c': 'int add(int a, int b) { return a + b; }\n' };
for (const [key, text] of Object.entries(platform.text)) {
  if (key.startsWith('res/')) files['/' + key] = text;
}

// The driver prints its jobs. Each job is a run of its own.
const args = ['-target', 'pi32v2', '-mcpu=r3', '-Os', '-c', 'add.c', '-o', 'add.o'];
const listed = await run(['clang', '-###', '-resource-dir', '/res', ...args], files);
const jobs = listed.text.split('\n').filter((line) => line.startsWith(' "')).map((line) =>
  [...line.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((word) => word[1].replace(/\\(.)/g, '$1')));
let object;
for (const job of jobs) {
  const done = await run(job.slice(1), files);             // job[0] is the path of the program
  if (done.code) throw new Error(done.text);
  object = done.FS.readFile('/work/add.o');                // an ELF object file for pi32v2
}
```

A link is one call: `run(['ld.lld', '-e', 'add', 'add.o', '-o', 'add.elf', '/rt/libclang_rt.builtins.a'], files)`,
with the object file and `rt/libclang_rt.builtins.a` (decoded from base64) in `files`.

## platform.json

The file is one JSON object with three parts:

| Part | What |
| --- | --- |
| `text` | path -> the text of the file |
| `binary` | path -> the bytes of the file, as base64 |
| `about` | The versions of the parts |

The paths, and where a build puts each file in the compiler's file system:

| Path | Files | What | Place for the compiler |
| --- | --- | --- | --- |
| `fm1-base/device/`, `fm1-base/include/`, `fm1-base/lib/` | 48 | fm1-base | `/work/fm1-base/...` |
| `fm1-base/bsp/chip/`, `fm1-base/bsp/boards/fm1/` | 44 | felucca-bsp | `/work/fm1-base/bsp/...` |
| `fm1-base/LICENSE` and 7 more | 8 | Licence files | Not needed for a build |
| `app/` | 7 | The adapter | The folder of the app: `/work/apps/web/fm1app/` |
| `gen/` | 2 | The generated headers | `build/gen/` in the folder of the app |
| `res/include/` | 35 | clang's C headers | `/res/include/` |
| `rt/libclang_rt.builtins.a` | 1 (binary) | The last file of each link | `/rt/` |
| `ota.bin` | 1 (binary) | The update loader, with its header | Not for the compiler: it goes into the package |
| `sdk/` | 3 (binary) | JieLi's files | Not for the compiler: they go into the package |

The compiler runs in `/work` and gets paths relative to it. The reference build
(`firmware/web/reference.sh`) uses the same relative paths. With other paths, a package can have other
bytes.

To make a firmware from these files, do the steps of `build_app` in fm1-base's `tools/build.py`: compile
three assembly files of `bsp/chip`, `device/unity.c` (the platform and the app), and the files of the
program; link with `device/app_packed.ld`; make the image from three sections; pack it with the update
loader and the SDK files. The worker `firmware-worker.js` in `src/index.html` is a full implementation
in JavaScript. The [README of the repository](../../README.md) lists its steps.

## Make the files again

[firmware/web/README.md](../../firmware/web/README.md) tells how. A change of a file here needs a new
table of hashes below.

## Check the files

```
c9113eb13497667a7241278ca29b6d6c89f32007ce47ab00b5867c53367cd9ee  llvm.js
3e20f2e1e664e81c8856912c0981df95a424c688d38a49ba32a8523144088af3  llvm.wasm
46d3926ee24877d59715919d0a17209fe22b51cedeab53498821e447cdb4ec31  platform.json
```

`sha256sum -c` with this text, in this directory, checks the three files.

To check what the compiler makes: `firmware/web/reference.sh PROJECT.zip` builds a project with fm1-base's
own build and this compiler (run by Node). A package that a page makes from the same project must be the
same bytes. For four `fm1.h` projects it is. `firmware/web/reference_device.sh` does the same for a
firmware with its own build: for each firmware that FM-1 Studio builds that way (AURORA, CS-1, FM-106,
FMINI-1 and Felucca, at the versions of its gallery) the bytes are the same too (November 2026).

## Limits

- **Tested on one FM-1.** Packages from this compiler were tested on an FM-1 with the newer flash chip
  (B36014) and m-bootloader 2.4-395e6cc (November 2026). Not tried on the first flash chip. The author
  of pi32v2-llvm calls the compiler experimental.
- **Use `-Os` for the code of a program.** At `-O2`, this compiler makes a division by a constant with a
  64-bit multiply. felucca-bsp's `docs/CPU.md` tells that such multiplies beside loads can change a
  register on the FM-1, and fm1-base's build gives a note when it finds them. JieLi's compiler does not
  make them, and this compiler at `-Os` does not make them. How fast `-Os` code runs on the FM-1 is not
  known.
- **C, assembly and LLVM IR only.** LLVM IR as the builds of Felucca and FMINI-1 give it (their size
  pass). Nothing else was tried.
- **The files were built one time.** It is not known if a second build gives the same bytes.
- **`llvm.wasm` is large.** A page must load it only when it is needed.

## Licences

The tables in "Origins" give the licence of each part. A package that is made with these files has
compiled code of fm1-base, felucca-bsp and the adapter, the update loader, and the three SDK files.

## Used by

The firmware compiler of [FM-1 Studio](https://github.com/TimSamshuijzen/fm1studiocom): the worker
`firmware-worker.js` in `src/index.html`.
