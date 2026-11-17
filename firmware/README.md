# firmware: fm1.h programs as firmware for the M-VAVE FM-1

This directory makes firmware for a real M-VAVE FM-1 from a C program that is written against `fm1.h`:
an API of three functions to write (`setup`, `audio`, `loop`) and a small set to call (screen, keys,
LEDs, encoders, MASTER, MIDI). It has three parts:

- **An adapter**: an app for [fm1-base](https://github.com/charlesvestal/fm1-base), the open platform
  for FM-1 firmware. It gives a program the functions of `fm1.h`.
- **`make_app.py`**: makes a full fm1-base app from a zip file with the sources of a program.
- **`build.sh`**: gets fm1-base and JieLi's compiler, builds the app, runs it in fm1-base's host
  simulator, and packs it as an installable `.fwsc` package.

The folder [web](web/README.md) has the scripts for a second, open compiler that runs in a browser:
[pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) as WebAssembly.

## Contents

| File | What |
| --- | --- |
| `adapter/app.h`, `adapter/app.c` | The adapter |
| `adapter/fm1_names.h` | Gives the names of the program a prefix (see "How the adapter works") |
| `adapter/libc/`, `adapter/fm1_libc.c` | `math.h`, `string.h` and `stdlib.h`: the compiler for the FM-1 has no C library |
| `make_app.py` | A project zip to an fm1-base app. It uses the Python library only, and runs nothing |
| `build.sh` | The full build, on Linux x86-64 |
| `build.cmd` | Starts `build.sh` in WSL, on Windows |
| `web/` | The scripts that make `src/fwsc`: a compiler for the FM-1 as WebAssembly |

## Origins

The files in this directory are written for this repository. Two things come from other places:

- **`fm1.h`** is not in this directory. `make_app.py` takes it from the text block `fm1.h` of
  `src/index.html` (the `--index` argument gives another file).
- **The platform and the tools.** `build.sh` downloads them, one time, into `~/.cache/fm1studio`. The
  versions are at the top of `build.sh`:

| What | Version | From | Checked by |
| --- | --- | --- | --- |
| [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0): the platform above the hardware (screen, keys, LEDs, sound, USB MIDI, the update entry, safe mode) and the build tool | Commit `c8e05997ce970875038156d2bbd69e712eac1543` | github.com | The commit |
| [felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0): the chip and board layer, the update loader and the package format | Commit `18eadadb8c364abd026accd2b1ea1c14ca347f06`, the one that fm1-base links | github.com | The commit |
| JieLi's compiler: clang 4.0.1 for pi32v2, some hundred MB | What fm1-base's `tools/get_toolchain.sh` gets | pkgman.jieliapp.com, JieLi's own server | fm1-base's script |
| Three files of JieLi's AC79 SDK (Apache-2.0). They go into each package | Tag `AC79NN_SDK_V1.2.1_2023-12-13`, commit `d179b4484759423312073f5fbb232501aa491047` | gitee.com/Jieli-Tech, JieLi's own repository | The commit, and the SHA-256 of each file |

Nothing is installed outside `~/.cache/fm1studio` (in WSL: inside WSL). To remove it all, delete the
folder.

## How the adapter works

fm1-base calls an app through the functions of `app.h`. The adapter makes the three functions of the
program from them:

| fm1-base calls | When | The adapter |
| --- | --- | --- |
| `app_ui_init` | When the screen is ready | Clears the screen and calls `setup()`, one time. A second call comes after an update that was cancelled: then the FM-1 starts again |
| `app_render` | For each audio block of 256 frames, in an interrupt | Calls `audio()`. Before `setup()` is done, and when an update is near, it gives silence |
| `app_frame`, `app_input_only` | In the main loop | Calls `loop()` one time for each audio block, then sends the LEDs that changed |
| `app_tick` | 1000 times a second | Reads the keys and buttons, so that a short press is not lost while `loop()` draws |

The functions that a program calls go to fm1-base's platform (`plat.h`, `lcd.c`):

| `fm1.h` | fm1-base | What the adapter adds |
| --- | --- | --- |
| `fm1_lcd_fill_rect` | `lcd_fill` | The check of the rectangle |
| `fm1_lcd_blit` | `lcd_blit`, `lcd_sync` | Strips of 2400 pixels, each copied with its bytes swapped: the panel takes the high byte first |
| `fm1_in`, `fm1_input_edges`, `fm1_input_note_edges` | `plat_buttons`, `plat_keys` | The button numbers of `fm1.h`, and the record of what changed |
| `fm1_led_key` | `plat_lights` | The LED numbers of `fm1.h` |
| `fm1_enc_take` | `plat_enc` | The encoder numbers of `fm1.h` |
| `fm1_master` | `plat_master` | The range 0 to 1023 |
| `fm1_midi_take` | `plat_midi_in` | Nothing: it is the same USB-MIDI packet |

Two more facts:

- **Names.** felucca-bsp has functions with the names of `fm1.h`. So the names of the program get the
  prefix `fm1x_`: `fm1_names.h` has a `#define` for each, and the build puts it in front of each file of
  the program.
- **Two compile units.** `app.c` is a part of fm1-base's unity build (`device/unity.c`). The files of the
  program are compiled apart from it, each as its own object.

## Use

### The full build

```
firmware\build.cmd src\examples\neonreef\neonreef.zip          (Windows: it starts the build in WSL)
firmware/build.sh src/examples/neonreef/neonreef.zip           (Linux, or inside WSL)
```

A second argument gives the name on the FM-1's start screen: `firmware\build.cmd my.zip "MY SYNTH"`.
Without it, the name of the zip file is the name.

The result is in `build/fwsc/NAME/`:

| File | What |
| --- | --- |
| `NAME.fwsc` | The package to install on the FM-1 |
| `NAME.bin`, `SHA256SUMS`, `build-log.txt` | The program image, the checksum of the package, and the text of the build |
| `host/1-start.png` ... `host/4-knobs.png` | Pictures of the screen from a run in fm1-base's host simulator |
| `host/sound.wav` | The sound of that run |

The steps of `build.sh`:

1. It gets fm1-base and felucca-bsp.
2. It gets JieLi's compiler and the three SDK files.
3. It makes the app with `make_app.py`.
4. It builds the app for fm1-base's host simulator and runs it with the script `tests/smoke.txt` of the
   app: some keys and knobs, four pictures, the sound.
5. It builds the app for the FM-1 with fm1-base's `tools/build.py`, which also checks and packs it.

Steps 1 and 2 are done one time. The first build takes long, because of the downloads.

| Variable | What | Default |
| --- | --- | --- |
| `FM1_WORK` | The folder for the downloads and the apps | `~/.cache/fm1studio` |
| `JIELI_TOOLCHAIN` | A toolchain folder that is there already | Downloaded |
| `AC79_SDK` | A folder with the SDK files that is there already | Downloaded |
| `SDK_URL` | Another place for the SDK repository | JieLi's repository on gitee.com |

### One time: WSL

JieLi's compiler runs on Linux x86-64 only. On Windows the build runs in WSL (the Linux that Microsoft
gives with Windows 10 and 11).

1. Open a terminal as administrator and run `wsl --install`. Restart the computer when it asks. At the
   first start of Ubuntu, choose a user name and a password.
2. In Ubuntu, install what the build needs:

       sudo apt update && sudo apt install -y git curl xz-utils python3 python3-pil libraqm0 libfribidi0 gcc libc6-dev

After that, `firmware\build.cmd` works from a normal Windows terminal. `build.sh` tells you when
something is missing. Another Linux x86-64 computer needs only step 2.

### Only the app

```
python3 firmware/make_app.py --zip PROJECT.zip --out APP_DIR [--name NAME] [--index src/index.html] [--repo URL]
```

`APP_DIR` is then an app that each fm1-base checkout of the same version can build:
`python3 fm1-base/tools/build.py APP_DIR` (with `--host` for the host simulator).

| In `APP_DIR` | What |
| --- | --- |
| `fm1.toml` | The settings of fm1-base's build |
| `app.h`, `app.c`, `adapter/` | The adapter |
| `fm1.h` | The API |
| `fm1_app_name.h` | `APP_NAME`: the name on the start screen |
| `fm1_prefix.h` | Put in front of each file of the program: the names, and the `define` of `fm1project.json` |
| `project/` | The files of the zip |
| `words.tsv` | For fm1-base's screen reader (no words) |
| `tests/smoke.txt` | The script for the host simulator |
| `name.txt` | The name of the build's files |

The name on the start screen has capitals, digits, space, `+` and `-`, and 14 characters at most. The
identity of the package is `FM-1_58`, one digit that is made from the name, and `00`.

### The project zip

A project zip has the text files of one program. A file that is not text is left out. Without a file
`fm1project.json`, each `.c` file is compiled, and a C++ file stops the build. With it:

| Key of `fm1project.json` | On the FM-1 |
| --- | --- |
| `sources` | The files to compile |
| `include` | Folders for `#include` |
| `define` | `NAME` or `NAME=VALUE` |
| `flags` | Compiler flags, after those of the build |
| `stack` | Not used |
| `web` | Refused: such a project is a firmware with its own build (`device`), which `web/reference_device.sh` runs |

## What a program must keep to

The adapter gives the API of `fm1.h`, but the FM-1 has limits that a simulation in a browser has not.
The build stops with a message for most of these.

| Subject | On the FM-1 |
| --- | --- |
| Language | C only. No C++ |
| Numbers | `float` only. A `double` fails the build: write `0.5f`, `sinf`, `powf`. `M_PI` is a `float` here |
| C library | What `adapter/libc` has: the `float` maths, the string functions, `abs`, `rand`. No `malloc`, no `printf` |
| Memory | 464 KB of RAM for the program and the platform together, of which 8 KB must stay free. A 240 x 240 picture in memory is 115 KB |
| Program size | 581 KB of flash for program and platform |
| Stack | 24 KB for `loop()`, about 7 KB for `audio()` |
| `audio()` | It is an interrupt, and has 5.8 ms for 256 frames. Do not draw in it |
| Screen | A draw call takes time. A full screen, 30 times a second, can be too much: draw what changed |
| `loop()` | One call for each audio block. A call that takes longer makes the next ones late |
| Hardware addresses | The build refuses pointer casts to `volatile` and inline assembly in a program |

`fm1.h` has a second implementation: the simulator of FM-1 Studio (`fm1_runtime.c` in `src/index.html`).
These things are different on the FM-1:

- The accuracy of the maths is about six digits (`atan2f`, `asinf`, `acosf`: about five).
- After an update that was cancelled, the FM-1 starts again.
- USB audio and Bluetooth are not built in. USB MIDI in works: `fm1_midi_take`.
- PLAY has a green LED and a red one. `fm1_led_key` lights the green one.

## Check a build

- **The checks of fm1-base's build.** `tools/build.py` stops when one fails: the entry point, no
  `double` routines, the size of the image and of the memory, no hardware access in the program
  (pointer casts to `volatile`, assembly, register names), no calls in the RAM code, no address of the
  chip's ROM. It gives a note, not an error, for 64-bit multiplies beside loads. `build-log.txt` has the
  text of the checks.
- **The host simulator.** Look at the four pictures and listen to `sound.wav`. The host simulator runs
  the program on the processor of the computer: it does not tell how fast the program is on the FM-1.

## Two compilers

A program can also be built with the open compiler of [web](web/README.md) and `src/fwsc`:

| | `build.sh` | `web/reference.sh`, or a browser with `src/fwsc` |
| --- | --- | --- |
| Compiler | JieLi's clang 4.0.1 for pi32v2 | [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) by Julius Vitkauskas, clang 23.1.5 and ld.lld, release `pi32v2-23.1.5-5` |
| Source | A binary from JieLi's server (pkgman.jieliapp.com), through fm1-base's `tools/get_toolchain.sh`. No source code, no licence file | Built from source as WebAssembly with Emscripten 7.0.0 by `web/build_compiler.sh`; the result is in `src/fwsc` |
| The program is compiled at | `-O2` | `-Os` |
| Runs on | Linux x86-64 | Node, or a browser |
| Host simulator | Yes | No |

The adapter and `make_app.py` are the same for both.

## Limits

- **Tested on an FM-1 with the open compiler only.** Packages of `fm1.h` programs from the open compiler
  (`src/fwsc`) were tested on an FM-1 with the newer flash chip (B36014) and m-bootloader 2.4-395e6cc
  (November 2026). No package of `build.sh` (JieLi's compiler) was installed on a real FM-1. With
  `build.sh`, the three example projects (NEON REEF, GLOAMING, GWAVE) build to a package, pass the
  checks, and give the right pictures and sound in the host simulator.
- **The speed on the FM-1 is not known.** The two example games draw the full screen for each picture,
  and count on 29 pictures a second.
- **`fm1.h` comes from `src/index.html`.** Without that file, give `make_app.py` another file with the
  same text block.
- **How to install a package**, which FM-1 units it is for, and what to do when an install goes wrong:
  see "On a real FM-1" in the [README of the repository](../README.md). Read it before the first
  install.

## Licences

The files in this directory are GPL-3.0-only. A package also has code of fm1-base and felucca-bsp
(MPL-2.0, which can be combined with GPL code) and three files of JieLi's AC79 SDK (Apache-2.0). Their
licence files come with the downloads in `~/.cache/fm1studio`. Not affiliated with M-VAVE or JieLi.

## Used by

The firmware compiler of [FM-1 Studio](https://github.com/TimSamshuijzen/fm1studiocom), for `fm1.h`
projects: the adapter is in `src/fwsc/platform.json`, and `make_app.py` gives the steps that its worker
`firmware-worker.js` does in `src/index.html`.
