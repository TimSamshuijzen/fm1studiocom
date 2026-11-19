# src/wasm-clang: a C and C++ compiler for WebAssembly that runs in a browser

This directory has clang 8.0.1 and wasm-ld, built as WebAssembly for WASI, with a file system in memory
and a C and C++ library. With these files, a web page can compile C and C++ to a WebAssembly module. It
needs no server side and no installed compiler.

The files are copies of the files of [wasm-clang](https://github.com/binji/wasm-clang) by Ben Smith, a
demo of his CppCon 2019 talk, at a fixed commit. Three of them have another name here; their bytes are
not changed. Do not change them by hand. They are the work of others, under their own licences.

## Contents

| File | Size | Name in wasm-clang | What |
| --- | --- | --- | --- |
| `clang.wasm` | 31.2 MB | `clang` | clang 8.0.1, a WASI program |
| `lld.wasm` | 19.5 MB | `lld` | lld, a WASI program: `wasm-ld` |
| `memfs.wasm` | 345 KB | `memfs` | A file system in memory: the WASI calls of the two programs |
| `sysroot.tar` | 9.3 MB | `sysroot.tar` | The headers and libraries in `/include` and `/lib`: the C library of WASI (wasi-libc), `libc++`, `libc++abi`, clang's headers (445 files and folders) |
| `shared.js` | 24 KB | `shared.js` | The JavaScript that runs the programs: the class `API` |
| `licences/` | | `LICENSE`, `LICENSE.llvm` | The licence texts of wasm-clang and of LLVM |

## Origins

| File | From | Licence |
| --- | --- | --- |
| All | [binji/wasm-clang](https://github.com/binji/wasm-clang), commit `648c4a89997a351eef75cdaec3ef5b89d4937dec` (December 2023), the last commit of its branch `master` in November 2026 | See "Licences" |
| `clang.wasm`, `lld.wasm` | Last changed in commit `88838f3` (May 2020). Built by Ben Smith from his LLVM fork, [binji/llvm-project](https://github.com/binji/llvm-project), for WASI. His notes on the build: [a gist](https://gist.github.com/binji/b7541f9740c21d7c6dac95cbc9ea6fca) | Apache-2.0 with LLVM exceptions (`licences/LICENSE.llvm`) |
| `memfs.wasm`, `shared.js` | Last changed in commit `8e78cdb` (May 2020). The source of `memfs` is in the folder `binji` of binji/llvm-project | Apache-2.0 (`licences/LICENSE`) |
| `sysroot.tar` | Last changed in commit `a19ba60` (September 2019) | The tar file has no licence texts of its own |

wasm-clang's site, `binji.github.io/wasm-clang`, serves the files of the same commit: in November 2026,
the five files there had the same SHA-256 as those of the commit.

## Use the compiler

`shared.js` defines the class `API`. A page, or a worker, makes one with the addresses of the files:

```js
importScripts('wasm-clang/shared.js');                     // defines API
const api = new API({
  readBuffer: async (url) => (await fetch(url)).arrayBuffer(),
  compileStreaming: async (url) => WebAssembly.compile(await (await fetch(url)).arrayBuffer()),
  hostWrite: (text) => console.log(text),                  // the text of the tools
  clang: 'wasm-clang/clang.wasm',
  lld: 'wasm-clang/lld.wasm',
  memfs: 'wasm-clang/memfs.wasm',
  sysroot: 'wasm-clang/sysroot.tar',
});
await api.ready;                                           // memfs runs, and has the files of sysroot.tar

api.memfs.addFile('add.c', new TextEncoder().encode('int add(int a, int b) { return a + b; }\n'));
await api.run(await api.getModule(api.clangFilename), 'clang', '-cc1', '-emit-obj', '-isysroot', '/',
  '-internal-isystem', '/include', '-internal-isystem', '/lib/clang/8.0.1/include', '-O2',
  '-x', 'c', '-o', 'add.o', 'add.c');
await api.run(await api.getModule(api.lldFilename), 'wasm-ld', '--no-threads', '--no-entry',
  '--export=add', 'add.o', '-o', 'add.wasm');
const wasm = api.memfs.getFileContents('add.wasm');        // a WebAssembly module
```

The rules:

- **clang runs as `clang -cc1`.** The driver cannot start a process. Give the options of the compile
  job yourself, as above.
- **The libraries** are in `/lib/wasm32-wasi`: give `-Llib/wasm32-wasi` and `-lc` (`-lc++`, `-lc++abi`
  for C++) to `wasm-ld`.
- **The file system** has room for about 1000 files and folders, of which `sysroot.tar` uses 445, and
  it cannot delete a file. Make a new `API` for a new set of files; the compiled programs can be kept
  between them (`moduleCache`).
- **The clock.** `shared.js` gives the programs no `clock_time_get`. clang asks for the time for
  `__DATE__` and `__TIME__`, and when it stops on an error: without a clock, it then stops with no
  message. Give it one in the imports of each program (`compiler-worker.js` in FM-1 Studio's
  `src/index.html` shows how).
- **The server.** `compileStreaming` above compiles from the bytes, so the server can give the files with
  each type. A page on another site needs the header `Access-Control-Allow-Origin` for this folder.

## Make the files again

```bash
for f in clang lld memfs sysroot.tar shared.js LICENSE LICENSE.llvm; do curl -L -O https://raw.githubusercontent.com/binji/wasm-clang/648c4a89997a351eef75cdaec3ef5b89d4937dec/$f; done
mv clang clang.wasm; mv lld lld.wasm; mv memfs memfs.wasm; mkdir -p licences; mv LICENSE LICENSE.llvm licences/
```

Run this in this directory. For another commit of wasm-clang, put the new hashes in the table below.

## Check the files

```
2a466f0e990329d3230b869d04fc20803eae96a7feb3a3f6c93e25a77b8aed1d  clang.wasm
36419ed202011765222098d7701218378b67f634d50f0a4625059ae2c9860f48  lld.wasm
2c72ee42bd9430029dda8c6bafc9f37143f6fe88d5f1ea950a70259ab748bcfe  memfs.wasm
2435a7b549af30c2be7ec249c405bc2e911ab0c6003012f0909ec3c131bff867  sysroot.tar
4de0be862bda1a7571c1f0632abe3c62c47a56e734cc6b703155805a4b160b1c  shared.js
cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30  licences/LICENSE
ebcd9bbf783a73d05c53ba4d586b8d5813dcdf3bbec50265860ccc885e606f47  licences/LICENSE.llvm
```

`sha256sum -c` with this text, in this directory, checks the files. `.gitattributes` keeps Git from a
change of their line ends.

## Limits

- **Not built here.** The files are binaries of wasm-clang. The commit of binji/llvm-project that they
  were built from is not known, and the build was not made again.
- **clang 8.0.1 and wasm-ld only.** No Binaryen tools, so no Asyncify: a program that does not return
  cannot stop between two calls.
- **Alpha.** wasm-clang's README calls it "very much alpha demoware".

## Licences

wasm-clang is Apache-2.0 (`licences/LICENSE`). clang, lld and libc++ are parts of LLVM, under the
Apache License v2.0 with LLVM Exceptions (`licences/LICENSE.llvm`, wasm-clang's copy of LLVM's licence).
The C library of WASI in `sysroot.tar` (wasi-libc) is made from musl (MIT). `sysroot.tar` has no licence
texts.

## Used by

The simulator compiler of [FM-1 Studio](https://github.com/TimSamshuijzen/fm1studiocom): the worker
`compiler-worker.js` in `src/index.html`.
