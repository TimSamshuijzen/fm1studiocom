# FM-1 Studio

A code workstation for the M-VAVE FM-1 synthesizer that runs in the browser: **[fm1studio.com](https://fm1studio.com)**.

Write a program in C, press COMPILE, and play it on a simulated FM-1. The compiler, the program
and the simulator all run in the page. There is no server side and nothing to install. It is a simulation
of the FM-1 API, not an emulation of the CPUs. A second compiler, also in the page, makes an installable
package for a real FM-1 from a project (FIRMWARE), and an installer writes it to an FM-1 over USB
(INSTALL). Code that works in this simulator does not guarantee it will work on a real FM-1.

- **Code editor** with a file list, syntax colours and projects of more than one file.
- **Simulator compiler**: clang 8.0.1 and wasm-ld as WebAssembly (wasm-clang), in a Web Worker. The output
  is one WebAssembly module.
- **FM-1 simulator**: the front panel with its 240 x 240 screen, 27 note keys, 14 buttons, 7 encoders,
  the MASTER pot and the LEDs, in the six colours of the device.
- **Sound** on the audio thread of the browser (an AudioWorklet), with a scope and a level meter.
- **MIDI input** from the keyboards and controllers of the computer (Web MIDI).
- **Firmware gallery**: loads a demo project, or the source code of open-source FM-1 firmware from
  GitHub, compiles it and runs it.
- **Firmware compiler**: clang 23.1.5 and ld.lld for the FM-1's processor as WebAssembly (pi32v2-llvm),
  in a second Web Worker. It makes a `.fwsc` package of a project for a real FM-1.
- **Installer**: fm1-base's installer writes the package to an FM-1 over USB MIDI (Chrome or Edge).

## The compilers

Both compilers are LLVM's clang and linker, built as WebAssembly, and run in the browser. They come from
these places:

| | Simulator compiler | Firmware compiler |
| --- | --- | --- |
| What | clang 8.0.1 and wasm-ld, for WebAssembly | clang 23.1.5 and ld.lld, for the FM-1's processor (JieLi pi32v2) |
| Project | [wasm-clang](https://github.com/binji/wasm-clang) by Ben Smith, a demo of his CppCon 2019 talk | [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) by Julius Vitkauskas: LLVM with a target for pi32v2, which its author calls experimental |
| Version | The files of wasm-clang's demo site at the time of the load. In its repository, `clang` and `lld` last changed in commit `88838f3` (May 2020) | Commit `60b0340f47083cf088903030a9b55d8a65525dde`, release `pi32v2-23.1.5-5` |
| Built by | Ben Smith, from his LLVM fork ([binji/llvm-project](https://github.com/binji/llvm-project)), compiled for WASI; `memfs` gives it a file system in memory | This repository: `firmware/web/build_compiler.sh` builds it from source with Emscripten 7.0.0 |
| Loaded from | `https://binji.github.io/wasm-clang/`: `clang`, `lld`, `memfs`, `sysroot.tar`, `shared.js` (about 60 MB) | `src/fwsc` of this site: `llvm.js`, `llvm.wasm` (52 MB), `platform.json` |
| Runs in | `compiler-worker.js` in `src/index.html` | `firmware-worker.js` in `src/index.html` |
| Makes | `fm1.wasm`, which the simulator runs | A `.fwsc` package for a real FM-1 |
| Licence | Apache-2.0 | Apache-2.0 with LLVM exceptions; Emscripten's runtime and musl: MIT and others |

The page loads the simulator compiler when it opens, and the firmware compiler at the first FIRMWARE or
INSTALL. The WebAssembly logo in the PROGRAM panel and the one in the FIRMWARE panel name their compiler
in their hints.
[src/fwsc/README.md](src/fwsc/README.md) lists all parts of the firmware compiler, and
[firmware/web/README.md](firmware/web/README.md) tells how to build it again and how to check its
packages. On a computer, `firmware/build.sh` builds the same kind of package with JieLi's own compiler
(clang 4.0.1 for pi32v2, a binary from JieLi's server, without source code):
[firmware/README.md](firmware/README.md).

## Use

Open the page. The first time, it downloads the simulator compiler (about 60 MB). It then compiles the demo project
and runs it. Browsers block sound until a click or a key press, so click the page one time.

| Control | How |
|---|---|
| Note keys, buttons | Click or touch |
| Encoders, MASTER | Mouse wheel, or drag up and down |
| Notes C4 to F5 | Computer keys `A` to `'` (white keys) and the row above (black keys) |
| OCT- and OCT+ | Computer keys `Z` and `X` |
| Compile and run | The COMPILE button, or `Ctrl`+`Enter` |
| MIDI | The MIDI IN button asks the browser for permission |

A project is a name and a set of text files. SAVE downloads it as a zip file and LOAD reads one back.
PROG-1, the first card of the gallery, loads a demo project in place of the project: a progressive rock
band in an FM-1, by Hans Dam. SPACE-1, the second card, loads an original piece in the spirit
of space sounds and synth bass, with laser shots, a flying saucer and sonar pings on the buttons. 
GAME-1, the third card, loads a game: NEON REEF, a vertical shooter. Fly with the computer keys `A`, `W`, `S` and `D`, and fire with `J`.
The page keeps nothing between visits: save a project before you close the page.

## Write a program

A program is written against `fm1.h` (the VIEW FM1.H button shows it, with the full documentation). It
defines three functions:

```c
#include "fm1.h"

void setup(void) { }                           /* one time, before the first audio block */
void audio(int32_t *out, int frames) { }       /* each block: 256 stereo frames, signed 24-bit */
void loop(void) { }                            /* again and again: controls and screen */
```

The API gives the program:

| Part | Functions |
|---|---|
| Screen, RGB565 | `fm1_lcd_fill_rect`, `fm1_lcd_blit` |
| Keys and buttons | `fm1_in`, `fm1_input_edges`, `fm1_input_note_edges` |
| LEDs | `fm1_led_key` |
| Encoders | `fm1_enc_take` |
| MASTER pot | `fm1_master` |
| MIDI input | `fm1_midi_take` |

The screen and input functions have the names and signatures of
[felucca-bsp](https://github.com/hugelton/felucca-bsp). The demo project `main.c` uses all of the API:
a small FM synthesizer with an arpeggiator, an echo and its own screen.

### On a real FM-1

The FIRMWARE button makes a `.fwsc` package of the project with the firmware compiler, and downloads it.
The first time, the page loads that compiler (52 MB, less when the server compresses it); after that, a
package takes some seconds. The NAME of the
project gives the name on the FM-1's start screen. An adapter makes the functions of `fm1.h` from
[fm1-base](https://github.com/charlesvestal/fm1-base) and felucca-bsp. The program must be C, with `float`
and no `double`; the LOG panel tells when a program does not fit the FM-1.

**FM-1 Studio was tested on a real FM-1, with all its features**: FIRMWARE and INSTALL, for `fm1.h`
programs and for the firmwares of the gallery that the table in "The gallery's firmware on a real FM-1"
marks as tested. That FM-1 has the newer flash chip and m-bootloader 2.4-395e6cc, installed with M-Flasher
(see "Which FM-1"). Read this section to its end before you install a package.
[firmware/README.md](firmware/README.md) tells what a program must keep to on the FM-1.

The folder `firmware` builds the same kind of package on your own computer, from a project zip (the file
that SAVE gives), with JieLi's own compiler: `firmware\build.cmd PROJECT.zip` on Windows (in WSL),
`firmware/build.sh PROJECT.zip` on Linux. That build also runs the program in fm1-base's host simulator.

#### Install the package

The INSTALL button builds the project and writes the package to an FM-1, over USB MIDI. A dialog shows
the steps:

1. **Read.** The page makes the package of the project and checks it. You read what an install does,
   and agree. For a firmware of the gallery, you also agree that it is the work of its authors and that
   you keep to its licence; the box links to its repository. The dialog tells you to work
   with [M-Flasher](https://github.com/jvitkauskas/m-flasher) first: make a backup, check the flash chip and
   the boot loader, and install [m-bootloader](https://github.com/jvitkauskas/m-bootloader). The page cannot
   see the boot loader: a firmware that runs does not tell it.
2. **Connect.** Connect the FM-1 with a USB cable that carries data, and let it run as it is. CONNECT
   asks the browser for MIDI with SysEx messages, finds the FM-1, and tells which firmware it runs.
3. **Install.** INSTALL NOW starts the install. The firmware on the FM-1 checks the package and starts
   the update loader of the package. The loader writes the program, and the FM-1 starts again. The
   page then asks the FM-1 for its identity and compares it with the package.

The button needs Chrome or Edge (Web MIDI), and the page from `https:` or `localhost`. Do not disconnect
the cable during the install. An install that stops after the FM-1 went to update mode can continue:
connect the cable again and go through the steps again. The code that talks to the FM-1 is fm1-base's
own installer, not changed: [src/install/README.md](src/install/README.md). INSTALL is off while a
firmware of the gallery is open that the firmware compiler cannot build (see "The gallery's firmware on a
real FM-1").

##### Back to the firmware of M-VAVE

The INSTALL dialog installs the program of the project only. To put the firmware of M-VAVE back, download
[FM-1.fwsc](https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc) from M-VAVE
(version 16 in November 2026) and install it with one of the other tools below. The update loader of
M-VAVE writes all of the flash memory, also the boot loader. We do not know if version 15 knows the newer
flash chip (see "Which FM-1"): use the file from the link.

##### Other tools

Other tools install the file that FIRMWARE downloads. They are felucca-bsp's and M-VAVE's, not ours:

- `tools/fm1_install.py PACKAGE.fwsc` of felucca-bsp installs over USB MIDI. It needs Python with `mido`
  and `python-rtmidi`. Run it in Windows, not in WSL: WSL does not see the FM-1.
- felucca-bsp's notes say that M-VAVE's own updater (M-UPGRADE) takes such packages too.

The package of an `fm1.h` program holds the program and fm1-base's update loader; that of a firmware of
the gallery holds the firmware and its own update loader. After the install, the FM-1 runs only this
program, until you install another firmware.

#### Which FM-1

The FM-1 was made with two flash chips: the Puya P25Q80 (ID 856014) and, in units sold since autumn 2026,
the UC25HQ80 (ID B36014). The fm1-base version that a package is built on takes both
(`device/fm1_flash_ok.h`), and so does the package. A firmware that knows only the first chip starts on a
unit with the second one, but then cannot be replaced over USB: that is why this matters.

The update loader in the package writes only the program area of the flash, not the boot loader below
it. M-VAVE's stock firmware exists as V15 and V16, each with its own boot loader. fm1-base says it starts
under either. [m-bootloader](https://github.com/jvitkauskas/m-bootloader) is another boot loader, in place
of M-VAVE's.

The packages of FM-1 Studio were tested on one FM-1: the newer chip (B36014), with m-bootloader
2.4-395e6cc, installed with M-Flasher (November 2026). We did not try a package on the first chip, or
under the boot loader of V15 or V16.

#### If it goes wrong

This is what fm1-base and felucca-bsp say about their own safety nets. We did not try them.

- A firmware built on fm1-base keeps the update entry: an installer can replace it while it runs.
- **REC held at power-on** goes to the chip's own update mode, if the firmware starts at all. That mode
  is the one that `fm1_rescue` (below) talks to: do not go there without need.
- After two starts that fail, the FM-1 goes to a safe mode that makes no sound and takes an update. If
  safe mode fails too, it goes to the chip's update mode.
- `fm1_install.py FM-1.fwsc` of felucca-bsp, with M-VAVE's official file, puts the stock firmware back
  (see "Back to the firmware of M-VAVE" above).
- For an FM-1 that is in the chip's update mode and takes nothing else, felucca-bsp has `fm1_rescue`.
  That tool is for macOS only. We know of no such tool for Windows.

Keep M-VAVE's `FM-1.fwsc` on your computer before you install anything.

#### The gallery's firmware on a real FM-1

The firmware compiler can build the firmwares of the gallery whose `fm1project.json` has a `device` part
(the column "Real FM-1" of the gallery's table in ABOUT). That part tells the firmware worker to do the
steps of the firmware's own `tools/build.py`, with the files of the project: changes that you make in its
files go into the package. The package has the firmware's own update loader and identity:

| Firmware | Build (`device`) | Package | Identity | Same bytes | On a real FM-1 |
| --- | --- | --- | --- | --- | --- |
| CS-1 0.3.4 | `"dinghy"`, with a change for the newer flash chip | `cs1.fwsc` | `FM-1_950` | Yes | Tested |
| FM-106 1.0 | `"dinghy"`, without Bluetooth, with its boot stub | `fm106.fwsc` | `FM-1_907` | Yes | Tested |
| AURORA 0.9.1 | `"dinghy"` | `aurora.fwsc` | `FM-1_4747` | Yes | Tested |
| FMINI-1 v2.5.12 | `"felucca"` | `fmini-1-fm1-2.5.12.fwsc` | `FM-1_900`, as its releases | Yes | Tested |
| FELUCCA v1.8.2 | `"felucca"`, as its release build (`--release 1.8.2`) | `felucca-1.8.2.fwsc` | `FM-1_918`, as its release | Yes | Not tried |

These are not the released packages: the authors build with JieLi's compiler, FM-1 Studio with the
firmware compiler. "Same bytes": unchanged and with a change, the page's package is the same bytes as the
one that the firmware's own `build.py` makes with the firmware compiler, and that build passes all its
checks (`firmware/web/reference_device.sh`). "Tested": the page's package was installed and played on the
FM-1 of "Which FM-1".

**The page builds FM-106 without Bluetooth.** FM-106's own build links JieLi's closed Bluetooth libraries
(with link-time optimisation, in Docker); the page does the build that its `build.py` makes with
`FELUCCA_BLE=0`, which has no Bluetooth MIDI or audio. FM-106 1.0 takes both flash chips. It has a boot
stub in front of the app (`firmware/stub`): the chip's boot code starts the stub on every start, and the
stub starts the app only when the commit marker that the packer writes is right. `firmware/stub/stub.lock`
has the hash of the stub that JieLi's compiler made. The page's stub, from the firmware compiler, is another
one, and the log says so, as FM-106's `build.py` does: an install then also writes the flash sector of
the stub.

**The gallery's CS-1 has a change for the newer flash chip.** CS-1 0.3.4 as released knows only the
first chip (`0x856014` in `platform.c`, `recovery.c` and `loader/loader.c`), and The Firmware Hub lists
it so. On a unit with the newer chip, its update loader does not write (the install stops, and the
firmware that was there stays), and a CS-1 that is there cannot keep its settings or take an update over
USB. AURORA, by the same author on the same platform, takes both chips: `FL_JEDEC_OK` in its
`hal/fm1_flash_id.h`, used in the same three places. The `patches` of CS-1's entry make the same change
when the page loads CS-1: `FL_JEDEC_OK` in `hal/fm1_flash.h`, and the three checks. Each changed file
says so at its top. CS-1's update loader is then the same bytes as AURORA's.

**Felucca and FMINI-1: the `"felucca"` build.** FMINI-1 is made from Felucca, and the `build.py` of both
has more steps than AURORA's: the headers in `build/gen` (in the page: from the extras file, which the
firmware's own generators made), the code of the second core (`fm1_cpu1.S`), and a size pass. That pass
compiles the unity file `felucca.c` to LLVM IR, gives LLVM's `minsize` to the functions of the main loop
(those of the files that `tools/size_fns.py` names, but not the functions that an interrupt can run), and
then compiles the IR at `-Os`. The firmware worker's `"felucca"` build does these steps; `"size"` in
`device` is the list of files of `size_fns.py`. Felucca's hardware layer is in three folders, in this
sequence: its own `firmware/hal`, and the chip and board folders of the felucca-bsp submodule
(`firmware/bsp`); `"hal"` in `device` names them. The firmware compiler calls `__udivdi3` for FMINI-1's
64-bit divisions, so this build links the compiler's helper routines (`libclang_rt.builtins.a`), as
`opentc.py` does. Both take both flash chips (FMINI-1: `FL_JEDEC_OK` in `hal/fm1_flash_map.h`; Felucca
1.8.2: `project.c`).

### fm1project.json

Without this file, each `.c` and `.cpp` file of the project is compiled. With it, a project says how it
is built. All parts are optional:

```json
{
  "sources": ["main.c"],
  "include": ["firmware/src"],
  "define":  ["APP_VER=\"1.0\""],
  "flags":   ["-Wno-unused-function"],
  "stack":   1048576,
  "web":     { },
  "device":  { "build": "dinghy", "name": "aurora", "product": "FM-1_4747",
               "sources": ["firmware/src/aurora.c"], "define": ["APP_VER=\"0.9.1\""] }
}
```

`web` is for firmware that has its own browser emulator (see below). `device` is the build of such a
firmware for a real FM-1: the firmware compiler then makes its package with the steps of its own build.
Two builds are known now: `"dinghy"`, the build of AURORA, CS-1 and FM-106, and `"felucca"`, the build of
Felucca and FMINI-1. FM-106's `device` also has `"flags"` (more options for each C file), `"dualCore": 0`
(one core) and `"stub": true` (its boot stub). Those of Felucca and FMINI-1 have `"size"` (the files
whose functions are built for size), and Felucca's has `"hal"` (the folders of its hardware layer);
`fm1.h` tells which files each build takes. The file list marks the files that the compiler gets with
BUILD.

## Firmware gallery

The first three cards, PROG-1, SPACE-1 and GAME-1, load the demo projects of the page (see "Use"). Each
other card loads one firmware from its own repository on GitHub, at a fixed tag or commit, and compiles it in the
page.

| Firmware | Version | What | Repository |
|---|---|---|---|
| CS-1 | v0.3.4 | a CS-80 in spirit | [twochannels/CS-1](https://github.com/twochannels/CS-1) |
| FM-106 | 1.0 | a Juno-106 | [PatttF/fm-106](https://github.com/PatttF/fm-106) |
| AURORA | 0.9.1 | epic film sounds | [twochannels/AURORA](https://github.com/twochannels/AURORA) |
| FMINI-1 | v2.5.12 | a Minimoog Model D | [ruppert/fmini-1](https://github.com/ruppert/fmini-1) |
| GR!MM | v2.0.2 | dark ambient and dungeon synth | [GRiMMBIENT/grimm](https://github.com/GRiMMBIENT/grimm) |
| SLOOP | v2.8.5 | a live groovebox | [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) |
| FELUCCA | v1.8.2 | a multi-engine synthesizer | [hugelton/Felucca](https://github.com/hugelton/Felucca) |
| SKIFF | v2.5.1 | a six-track groovebox | [illuminus5/skiff](https://github.com/illuminus5/skiff) |
| JIANT | V10.7.23 | a bio-synthetic groovebox | [juanjiant-bit/JIANT](https://github.com/juanjiant-bit/JIANT) |

A compile takes from a few seconds (CS-1) to more than a minute (SKIFF, JIANT) on a fast computer.

These firmwares do not use `fm1.h`. Each one has a browser emulator with the `web_` interface of
Felucca's emulator: the program has its own FM-1 inside, and the page gives it the keys and takes the
sound, the screen and the LEDs. The `web` part of `fm1project.json` tells the page that a program has
this interface, and how it numbers its buttons and encoders.

Three things that such a repository does not have come from this one:

- **`fm1project.json`**: the `manifest` of the entry in `GITHUB_PROJECTS`.
- **Generated files**: the tables, fonts and pictures that the build of the firmware makes with its own
  tools. They are in `src/extras/<id>.js`. Each file tells at its top where its parts come from. For
  SLOOP and GR!MM, the file also has the emulator source that FM-1 Studio compiles
  (`fm1studio/sloop_web.c`, `fm1studio/grimm_web.c`).
- **Patches**: text changes in a loaded file, for firmware that cannot run here as it is. GR!MM has four.
  CS-1 has nine, for the newer flash chip of the FM-1 (see "The gallery's firmware on a real FM-1").
  Each change is marked with "FM-1 Studio" in a comment.

## How it works

The application is one file, `src/index.html`, the firmware compiler in `src/fwsc`, and the installer
in `src/install`. The page has the styles, the page script, and these text blocks that the page script
reads:

| Block | What |
|---|---|
| `fm1.h` | The API |
| `fm1_runtime.c` | The simulator side of the API, linked into each program |
| `fm1project.json`, `main.c` | The demo project |
| `prog-1/fm1project.json`, `prog-1/main.c` | The demo project of the PROG-1 card, by Hans Dam |
| `space-1/fm1project.json`, `space-1/main.c` | The demo project of the SPACE-1 card |
| `game-1/fm1project.json`, `game-1/main.c` | The demo project of the GAME-1 card: NEON REEF (`src/examples/neonreef`) |
| `compiler-worker.js` | The compiler worker: the simulator compiler |
| `fm1-worklet.js` | The audio worklet that runs the program |
| `firmware-worker.js` | The firmware worker: the firmware compiler and the build of a package |

1. The page starts the **compiler worker**. It loads the simulator compiler,
   [wasm-clang](https://github.com/binji/wasm-clang) (clang 8.0.1), from `binji.github.io/wasm-clang` and
   compiles `fm1_runtime.c` one time.
2. For each compile, the worker makes a new file system in memory, writes the project files to it,
   compiles the sources and links them to `fm1.wasm`.
3. The page sends the module to the **audio worklet**, which makes an instance of it. For each block of
   256 frames it calls `loop()` and then `audio()`. System calls that the C library imports all fail: an
   FM-1 has none.
4. The worklet sends the screen, the LEDs, the level and the memory size to the page. The page sends
   the keys, encoders, MASTER position and MIDI messages to the worklet.

The page is made so that it also works when `index.html` is opened from disk: the worker and the worklet
start from text in the page, and the extras are scripts. Only the firmware compiler and the installer
need a web server.

### The firmware worker

The **firmware worker** starts at the first FIRMWARE or INSTALL. It loads the firmware compiler,
`src/fwsc/llvm.wasm` (clang 23.1.5 and ld.lld for the FM-1's pi32v2 processor, built from
[pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm)), and `src/fwsc/platform.json` (the sources of
fm1-base and felucca-bsp, the adapter, the update loader and three files of JieLi's SDK).
[src/fwsc/README.md](src/fwsc/README.md) tells how a page calls this compiler.

Then the worker does the steps of `firmware/make_app.py` and of `build_app` in fm1-base's
`tools/build.py`, with the same file names and compiler flags:

1. It makes the app: the files of the project, `fm1.h`, the adapter, `fm1.toml` and two small headers.
2. It calculates the number of the build, a hash of the sources, as `build.py` does.
3. It compiles three assembly files, `device/unity.c` (the platform and the adapter), each C file of the
   program, and `fm1_libc.c`.
4. It links them with fm1-base's linker script.
5. It makes the image from three sections of the ELF file.
6. It runs the checks.
7. It packs the image, the update loader and the SDK files (`fm1pkg_make.py` of fm1-base, in JavaScript).

The package is the same bytes as the one that fm1-base's build makes with this compiler.
[firmware/web/README.md](firmware/web/README.md) tells how the files of `src/fwsc` are made, and how to
compare.

The worker has the checks of fm1-base's build that a program can fail:

| Check of fm1-base's build | In the worker |
| --- | --- |
| The entry point and the first bytes of the image | yes |
| No `double` routines | yes |
| The image fits the flash; 8 KB of memory stays free | yes |
| No hardware access in the program (pointer cast to `volatile`, assembly, register names) | yes |
| No calls in the RAM code | no: a program cannot put code there |
| No address of the chip's ROM | no |
| 64-bit multiplies beside loads (a note) | no: the program is compiled at `-Os` |

The platform is the same source for each program, and it passes all checks in the build of
`firmware/web/reference.sh`. The worker checks that an object file of the program has only sections for
code and data: so a program cannot put code or data in the parts of the memory that are for the platform.
A project can add `"flags": ["-O2"]` to its `fm1project.json`: then build it with `reference.sh` too, and
read the note.

For a project with `device` in its `fm1project.json` (a firmware of the gallery), the worker does the
steps of that firmware's own `tools/build.py` in place of the steps above: it compiles and links the update
loader and packs it as LZ4 blocks, compiles the firmware's assembly files and C file, links them with the
firmware's linker script, makes the image, and packs it as the firmware's `fm1pkg_make.py` does. For
FM-106 it also compiles and links the boot stub, puts it in front of the image, and writes the commit
marker (a CRC-32 of the app) into the package, and compares the stub with `firmware/stub/stub.lock`. In
the `"felucca"` build it compiles the C file through LLVM IR with the size pass of `tools/size_fns.py`,
adds the code of the second core to the image, and checks the memory budgets that `firmware/app.ld`
gives. As with JieLi's toolchain, the `"dinghy"` build links no helper routines of the compiler; the
`"felucca"` build links them. Its checks are those of the firmware's `build.py` but the three that need a disassembly (no calls
from RAM code into the flash, no calls in the loop of the second core, no address of the chip's ROM).
`firmware/web/reference_device.sh` runs the firmware's `build.py` with the firmware compiler and all its
checks; the page's package must have the same hash.

## Run it here

There is no build step. Serve the folder `src`:

```bash
python -m http.server 8765 --bind 127.0.0.1 --directory src
```

Then open `http://127.0.0.1:8765`. The page needs the internet for the simulator compiler (from
wasm-clang's demo site) and for the gallery.

```
src/index.html                 the application
src/extras/                    files for the firmware of the gallery
src/fwsc/                      the firmware compiler: clang for the FM-1 as WebAssembly, and the platform
src/install/                   the installer of fm1-base
src/error.html                 the page for an address that does not exist
src/staticwebapp.config.json   configuration of the host
.github/workflows/             deployment
firmware/                      fm1.h on a real FM-1: the adapter, and the build on a computer (not deployed)
firmware/web/                  the scripts that make src/fwsc
```

A push to `main` deploys `src` to Azure Static Web Apps as it is.

## Add a firmware to the gallery

Add an entry to `GITHUB_PROJECTS` in `src/index.html`. The comments above it describe each part.

The firmware must have a C source that gives the `web_` interface: `web_boot`, `web_render`,
`web_now_ms`, `web_out_l`, `web_out_r`, `web_keys`, `web_buttons`, `web_enc`, `web_master`, `web_midi`,
`web_screen`, `web_screen_draws`, and `web_leds_buttons` and `web_leds_keys` (or `web_lit_buttons` and
`web_lit_keys`). A repository that has its generated headers needs only the entry. One that makes them
in its build also needs an extras file: a script that calls `fm1Extras({ path: text, ... })`.

A demo project is an entry in `DEMO_PROJECTS` (`id`, `label` and `about`), and its files as text blocks
of `src/index.html` whose id is the `id`, a slash and a file path, as `prog-1/main.c`. Its card shows
"demo" in place of a licence, and has no link.

## Limits

- The simulator compiler is clang 8.0.1. C++ files are compiled as C++14. It has no Asyncify, so a
  program whose main loop does not return cannot run as it is.
- The page loads the simulator compiler from wasm-clang's demo site, not from this repository: COMPILE
  needs that site, and has the files that the site has at the time.
- A project holds text files only, and the simulator compiler has room for about 570 files and folders.
- GitHub gives the list of files of a repository 60 times in an hour to one address. The first LOAD of
  a firmware uses one such request for each item in its `sources` (four for FELUCCA). After that, the
  page has the files until it is closed.
- The LEDs are on or off: the dim levels of some firmware are not shown.
- The firmware compiler is for `fm1.h` programs in C, and for the firmwares of the gallery in the table of
  "The gallery's firmware on a real FM-1". The other firmwares of the gallery have their own builds in
  their repositories: FIRMWARE and INSTALL are off for them, and their hints tell why. INSTALL installs
  the program of the project only, not other files such as M-VAVE's firmware. The package of an `fm1.h`
  program has no USB audio and no Bluetooth; USB MIDI in works.
- The gallery's CS-1 is CS-1 0.3.4 with a change for the newer flash chip (see "The gallery's firmware on
  a real FM-1"): its package is not the same as CS-1's own build of 0.3.4.
- The page's FM-106 has no Bluetooth, and its boot stub is not the one of FM-106's `stub.lock`.
- The page's FELUCCA package was not tried on a real FM-1.
- For a changed firmware of the gallery, the firmware worker does not do the checks of its build that
  need a disassembly: run `firmware/web/reference_device.sh` for those.
- The installer needs Chrome or Edge on a computer (Web MIDI).
- The packages of the firmware compiler and the installer were tested on one FM-1 only: the newer flash
  chip, with m-bootloader (see "Which FM-1").

## Licence

Copyright (C) 2026 Tim Samshuijzen

FM-1 Studio is free software: you can redistribute it and/or modify it under the terms of the GNU
General Public License, version 3, as published by the Free Software Foundation (GPL-3.0-only). It
comes without any warranty. See [LICENSE](LICENSE) for the full text.

### Other work

The firmwares of the gallery are the work of their authors, under GPL-3.0-only, and parts of them have
other licences: see the `LICENSE`, `LICENSING.md` and `LICENSES/` files that load with each project. The
page takes their code from their repositories when you press LOAD. The files in `src/extras` are made
from that code, and their licences apply to them.

The progressive rock band of the PROG-1 card (`prog-1/main.c` in `src/index.html`, and
`src/examples/prog-1`) is by Hans Dam.

The simulator compiler is [wasm-clang](https://github.com/binji/wasm-clang) by Ben Smith (Apache-2.0),
loaded from its demo site (see "The compilers").

The folder `src/fwsc` has work of others, the firmware compiler: LLVM with the pi32v2 target of Julius
Vitkauskas (Apache-2.0 with LLVM exceptions), built with Emscripten; the sources of fm1-base and
felucca-bsp (MPL-2.0); and three files of JieLi's AC79 SDK (Apache-2.0). A package of an `fm1.h` program
has code of fm1-base and felucca-bsp and those three files; a package of a firmware of the gallery has
the code of that firmware and the three files. [src/fwsc/README.md](src/fwsc/README.md) lists the parts,
their versions and their licences. Not affiliated with M-VAVE or JieLi.

The folder `src/install` has the installer of fm1-base (MPL-2.0):
[src/install/README.md](src/install/README.md).
