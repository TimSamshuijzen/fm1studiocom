# FM-1 Studio

A code workstation for the M-VAVE FM-1 synthesizer that runs in the browser: **[fm1studio.com](https://fm1studio.com)**.

Write a program in C, press COMPILE, and play it on a simulated FM-1. The compiler, the program
and the simulator all run in the page. There is no server side and nothing to install. It is a simulation 
of the FM-1 API, not an emulation of the CPUs. The FIRMWARE button makes an installable package for a
real FM-1 from a project, also in the page (see below).
Code that works in this simulator does not guarantee it will work on a real FM-1.

- **Code editor** with a file list, syntax colours and projects of more than one file.
- **Compiler**: clang and wasm-ld as WebAssembly, in a Web Worker. The output is one WebAssembly module.
- **FM-1 simulator**: the front panel with its 240 x 240 screen, 27 note keys, 14 buttons, 7 encoders,
  the MASTER pot and the LEDs, in the six colours of the device.
- **Sound** on the audio thread of the browser (an AudioWorklet), with a scope and a level meter.
- **MIDI input** from the keyboards and controllers of the computer (Web MIDI).
- **Firmware gallery**: loads the source code of open-source FM-1 firmware from GitHub, compiles it and
  runs it.
- **Firmware for a real FM-1**: a second compiler in the page, clang for the FM-1's processor, makes a
  `.fwsc` package of a project.

## Use

Open the page. The first time, it downloads the compiler (about 60 MB). It then compiles the demo project
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

The FIRMWARE button makes a `.fwsc` package of the project and downloads it. The first time, it loads a
second compiler (52 MB, less when the server compresses it); after that, a package takes some seconds. The NAME of the
project gives the name on the FM-1's start screen. An adapter makes the functions of `fm1.h` from
[fm1-base](https://github.com/charlesvestal/fm1-base) and felucca-bsp. The program must be C, with `float`
and no `double`; the compiler panel tells when a program does not fit the FM-1.

**No package was tried on a real FM-1 yet.** Read this section to its end before you install one.
[firmware/README.md](firmware/README.md) tells what a program must keep to on the FM-1.

The folder `firmware` builds the same kind of package on your own computer, from a project zip (the file
that SAVE gives), with JieLi's own compiler: `firmware\build.cmd PROJECT.zip` on Windows (in WSL),
`firmware/build.sh PROJECT.zip` on Linux. That build also runs the program in fm1-base's host simulator.

#### Install the package

The tools for this are felucca-bsp's and M-VAVE's, not ours:

- `tools/fm1_install.py PACKAGE.fwsc` of felucca-bsp installs over USB MIDI. It needs Python with `mido`
  and `python-rtmidi`. Run it in Windows, not in WSL: WSL does not see the FM-1.
- felucca-bsp's notes say that M-VAVE's own updater (M-UPGRADE) takes such packages too.

The package holds the program and fm1-base's update loader. After the install, the FM-1 runs only this
program, until you install another firmware.

#### Which FM-1

The FM-1 was made with two flash chips: the Puya P25Q80 (ID 856014) and, in units sold since autumn 2026,
the UC25HQ80 (ID B36014). The fm1-base version that a package is built on takes both
(`device/fm1_flash_ok.h`), and so does the package. A firmware that knows only the first chip starts on a
unit with the second one, but then cannot be replaced over USB: that is why this matters.

The update loader in the package writes only the program area of the flash, not the boot loader below
it. M-VAVE's stock firmware exists as V15 and V16, each with its own boot loader. fm1-base says it starts
under either. We did not try a package on either chip or under either boot loader.

#### If it goes wrong

This is what fm1-base and felucca-bsp say about their own safety nets. We did not try them.

- A firmware built on fm1-base keeps the update entry: an installer can replace it while it runs.
- **REC held at power-on** goes to the chip's own update mode, if the firmware starts at all. That mode
  is the one that `fm1_rescue` (below) talks to: do not go there without need.
- After two starts that fail, the FM-1 goes to a safe mode that makes no sound and takes an update. If
  safe mode fails too, it goes to the chip's update mode.
- `fm1_install.py FM-1.fwsc`, with M-VAVE's official file, puts the stock firmware back. fm1-base's
  installer page has the link to that file.
- For an FM-1 that is in the chip's update mode and takes nothing else, felucca-bsp has `fm1_rescue`.
  That tool is for macOS only. We know of no such tool for Windows.

Keep M-VAVE's `FM-1.fwsc` on your computer before you install anything.

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
  "web":     { }
}
```

`web` is for firmware that has its own browser emulator (see below). The file list marks the files that
the compiler gets with BUILD.

## Firmware gallery

Each card loads one firmware from its own repository on GitHub, at a fixed tag or commit, and compiles
it in the page.

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
  Each change is marked with "FM-1 Studio" in a comment.

## How it works

The application is one file, `src/index.html`, and the files of the FIRMWARE button in `src/fwsc`. The
page has the styles, the page script, and six text blocks that the page script reads:

| Block | What |
|---|---|
| `fm1.h` | The API |
| `fm1_runtime.c` | The simulator side of the API, linked into each program |
| `fm1project.json`, `main.c` | The demo project |
| `compiler-worker.js` | The compiler worker |
| `fm1-worklet.js` | The audio worklet that runs the program |
| `firmware-worker.js` | The worker of the FIRMWARE button |

1. The page starts the **compiler worker**. It loads
   [wasm-clang](https://github.com/binji/wasm-clang) (clang 8) from `binji.github.io/wasm-clang` and
   compiles `fm1_runtime.c` one time.
2. For each compile, the worker makes a new file system in memory, writes the project files to it,
   compiles the sources and links them to `fm1.wasm`.
3. The page sends the module to the **audio worklet**, which makes an instance of it. For each block of
   256 frames it calls `loop()` and then `audio()`. System calls that the C library imports all fail: an
   FM-1 has none.
4. The worklet sends the screen, the LEDs, the level and the memory size to the page. The page sends
   the keys, encoders, MASTER position and MIDI messages to the worklet.

The page is made so that it also works when `index.html` is opened from disk: the worker and the worklet
start from text in the page, and the extras are scripts. Only the FIRMWARE button needs a web server.

### The firmware worker

The **firmware worker** starts at the first click on FIRMWARE. It loads `src/fwsc/llvm.wasm` (clang 23
and ld.lld for the FM-1's pi32v2 processor, built from
[pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm)) and `src/fwsc/platform.json` (the sources of
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

## Run it here

There is no build step. Serve the folder `src`:

```bash
python -m http.server 8765 --bind 127.0.0.1 --directory src
```

Then open `http://127.0.0.1:8765`. The page needs the internet for the compiler and for the gallery.

```
src/index.html                 the application
src/extras/                    files for the firmware of the gallery
src/fwsc/                      a compiler for the FM-1 as WebAssembly, and the platform (for FIRMWARE)
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

## Limits

- The compiler is clang 8. C++ files are compiled as C++14. It has no Asyncify, so a program whose main
  loop does not return cannot run as it is.
- A project holds text files only, and the compiler has room for about 570 files and folders.
- GitHub gives the list of files of a repository 60 times in an hour to one address. The first LOAD of
  a firmware uses one such request for each item in its `sources` (four for FELUCCA). After that, the
  page has the files until it is closed.
- The LEDs are on or off: the dim levels of some firmware are not shown.
- FIRMWARE is for `fm1.h` programs in C. A firmware of the gallery has its own build in its repository.
  The package has no USB audio and no Bluetooth; USB MIDI in works.

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

The compiler is [wasm-clang](https://github.com/binji/wasm-clang) by Ben Smith, loaded from its demo
site.

The folder `src/fwsc` has work of others, for the FIRMWARE button: LLVM with the pi32v2 target of
Julius Vitkauskas (Apache-2.0 with LLVM exceptions), built with Emscripten; the sources of fm1-base and
felucca-bsp (MPL-2.0); and three files of JieLi's AC79 SDK (Apache-2.0). A package that the button makes
has code of fm1-base and felucca-bsp and those three files. [src/fwsc/README.md](src/fwsc/README.md)
lists the parts, their versions and their licences. Not affiliated with M-VAVE or JieLi.
