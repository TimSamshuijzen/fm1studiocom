# src/wasm-clang: the C library of WASI, from wasm-clang

This directory has a sysroot for WebAssembly: the headers and libraries of the C library of WASI
(wasi-libc), of libc++, and clang 8.0.1's own headers, in one tar file. With it, a compiler for
WebAssembly can compile and link a C program that uses the C library (`stdio.h`, `stdlib.h`, `math.h`,
...) to a WebAssembly module that imports the system calls of WASI.

The files are copies of files of [wasm-clang](https://github.com/binji/wasm-clang) by Ben Smith, a demo
of his CppCon 2019 talk, at a fixed commit. Their bytes are not changed. Do not change them by hand.
They are the work of others, under their own licences.

## Contents

| File | Size | Name in wasm-clang | What |
| --- | --- | --- | --- |
| `sysroot.tar` | 9.3 MB | `sysroot.tar` | The headers and libraries in `/include` and `/lib` (445 files and folders): see below |
| `licences/` | | `LICENSE`, `LICENSE.llvm` | The licence texts of wasm-clang and of LLVM |

The parts of `sysroot.tar`:

| Path | What |
| --- | --- |
| `include/` | The headers of wasi-libc |
| `include/c++/v1/` | The headers of libc++ |
| `lib/wasm32-wasi/libc.a` | wasi-libc, with its maths (`libm.a` is empty) |
| `lib/wasm32-wasi/libc++.a`, `libc++abi.a` | The C++ library |
| `lib/wasm32-wasi/crt1.o` and other libraries | The start code of a WASI program, and more parts of wasi-libc |
| `lib/clang/8.0.1/include/` | clang 8.0.1's own headers (`stddef.h`, `stdint.h`, ...) |
| `lib/clang/8.0.1/lib/wasi/libclang_rt.builtins-wasm32.a` | compiler-rt's builtins for wasm32 |

## Origins

| File | From | Licence |
| --- | --- | --- |
| All | [binji/wasm-clang](https://github.com/binji/wasm-clang), commit `648c4a89997a351eef75cdaec3ef5b89d4937dec` (December 2023), the last commit of its branch `master` in November 2026 | See "Licences" |
| `sysroot.tar` | Last changed in commit `a19ba60` (September 2019) | The tar file has no licence texts of its own |

wasm-clang's site, `binji.github.io/wasm-clang`, serves the files of the same commit: in November 2026,
its `sysroot.tar` had the same SHA-256 as that of the commit.

## Use

Unpack the tar file into the file system of the compiler, or the parts that a build needs. For C, with a
newer clang for WebAssembly, use the headers of wasi-libc and its library, but not clang 8.0.1's headers:

```
clang -target wasm32-wasi -nostdlibinc -isystem SYSROOT/include -O2 -c main.c -o main.o
wasm-ld --no-entry --export-dynamic main.o SYSROOT/lib/wasm32-wasi/libc.a \
  SYSROOT/lib/clang/8.0.1/lib/wasi/libclang_rt.builtins-wasm32.a -o main.wasm
```

The module imports the system calls of WASI from the module `wasi_unstable`: a page gives it those
functions, or functions that fail.

## Make the files again

```bash
for f in sysroot.tar LICENSE LICENSE.llvm; do curl -L -O https://raw.githubusercontent.com/binji/wasm-clang/648c4a89997a351eef75cdaec3ef5b89d4937dec/$f; done
mkdir -p licences; mv LICENSE LICENSE.llvm licences/
```

Run this in this directory. For another commit of wasm-clang, put the new hashes in the table below.

## Check the files

```
2435a7b549af30c2be7ec249c405bc2e911ab0c6003012f0909ec3c131bff867  sysroot.tar
cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30  licences/LICENSE
ebcd9bbf783a73d05c53ba4d586b8d5813dcdf3bbec50265860ccc885e606f47  licences/LICENSE.llvm
```

`sha256sum -c` with this text, in this directory, checks the files. `.gitattributes` keeps Git from a
change of their line ends.

## Limits

- **Not built here.** The tar file is a binary of wasm-clang. How it was built is not known, and the
  build was not made again.
- **Of 2019.** wasi-libc and libc++ are those of September 2019, built with clang 8.0.1. Their system
  calls are those of `wasi_unstable`, the first version of WASI.

## Licences

wasm-clang is Apache-2.0 (`licences/LICENSE`). libc++ and clang's headers are parts of LLVM, under the
Apache License v2.0 with LLVM Exceptions (`licences/LICENSE.llvm`, wasm-clang's copy of LLVM's licence).
wasi-libc is made from musl (MIT). `sysroot.tar` has no licence texts.

## Used by

The simulator compiler of [FM-1 Studio](https://github.com/TimSamshuijzen/fm1studiocom), for a program
with the `web_` interface: the worker `compiler-worker.js` in `src/index.html`.
