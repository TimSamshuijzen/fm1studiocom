# From a project zip to a real FM-1

This folder builds an installable `.fwsc` package for the M-VAVE FM-1 from a project zip of FM-1 Studio
(the file that the SAVE button gives). The build runs on your own computer.

    firmware\build.cmd src\examples\neonreef\neonreef.zip          (Windows: it starts the build in WSL)
    firmware/build.sh src/examples/neonreef/neonreef.zip           (Linux, or inside WSL)

The result is in `build/fwsc/NAME/`:

| File | What |
| --- | --- |
| `NAME.fwsc` | The package to install on the FM-1 |
| `NAME.bin`, `SHA256SUMS`, `build-log.txt` | The program image, the checksum of the package, and what the build said |
| `host/1-start.png` ... `host/4-knobs.png` | Pictures of the screen from a run on your computer |
| `host/sound.wav` | The sound of that run |

A second argument gives the name on the FM-1's start screen: `firmware\build.cmd my.zip "MY SYNTH"`.

**There is a second way: the FIRMWARE button of the page.** It makes a package in the browser, with
another compiler (an open one, built as WebAssembly), and it needs nothing on your computer. This build
here uses JieLi's own compiler, and it also runs the program in a simulator of fm1-base.
[web/README.md](web/README.md) tells the differences. The rest of this page is true for the packages of
both ways, but "How it works" and "One time: WSL" are about this build.

> **Status: not tried on an FM-1.** The three examples (NEON REEF, GLOAMING, GWAVE) build to a package, pass
> the checks of fm1-base's build, and run in fm1-base's host simulator with the right pictures and sound
> (November 2026). No package was installed on a real FM-1 yet. The first one you install is the test. Read
> "If it goes wrong" first.

## How it works

The FM-1's processor is a JieLi chip. Its compiler is JieLi's own and runs on Linux only. The firmware is
built on two open projects:

- [felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0): the chip and board layer, the update
  loader and the package format.
- [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0): the platform above the hardware
  (screen, keys, LEDs, sound, USB MIDI, the update entry, safe mode) and the build tool.

`fm1.h` is the same on the FM-1 as in the browser. The adapter in `adapter/` is what `fm1_runtime.c` is
in the simulator: it makes the functions of `fm1.h` from fm1-base's platform.

| File | What |
| --- | --- |
| `build.sh` | The build: gets the tools, makes the app, builds and packs it |
| `build.cmd` | Starts `build.sh` in WSL |
| `make_app.py` | Makes an fm1-base app from the zip. It takes `fm1.h` from `src/index.html` |
| `adapter/app.h`, `adapter/app.c` | The adapter |
| `adapter/fm1_names.h` | Gives the program's names a prefix: felucca-bsp has functions with the names of `fm1.h` |
| `adapter/libc/`, `adapter/fm1_libc.c` | `math.h`, `string.h` and `stdlib.h`: the FM-1's compiler has no C library |
| `web/` | The scripts that make the files of the page's FIRMWARE button (`src/fwsc`) |

The build downloads these, one time, into `~/.cache/fm1studio` (in WSL: inside WSL, not in this folder):

| What | From | Size |
| --- | --- | --- |
| fm1-base and felucca-bsp, at fixed commits | github.com | small |
| JieLi's compiler (clang for pi32v2) | pkgman.jieliapp.com, JieLi's own server | some hundred MB |
| Three files of JieLi's AC79 SDK, checked by hash: they go into the package | gitee.com/Jieli-Tech, JieLi's own repository | small |

Nothing is installed outside that folder. To remove it all, delete the folder.

## One time: WSL

JieLi's compiler has no Windows version that fm1-base can use, so on Windows the build runs in WSL (the
Linux that Microsoft gives with Windows 10 and 11).

1. Open a terminal as administrator and run `wsl --install`. Restart the computer when it asks. At the
   first start of Ubuntu, choose a user name and a password.
2. In Ubuntu, install what the build needs:

       sudo apt update && sudo apt install -y git curl xz-utils python3 python3-pil libraqm0 libfribidi0 gcc libc6-dev

After that, `firmware\build.cmd` works from a normal Windows terminal. `build.sh` tells you when
something is missing. The first build takes long, because of the downloads.

Another Linux x86-64 computer works the same way, without WSL.

## Install the package

The tools for this are felucca-bsp's and M-VAVE's, not ours:

- `tools/fm1_install.py PACKAGE.fwsc` of felucca-bsp installs over USB MIDI. It needs Python with `mido`
  and `python-rtmidi`. Run it in Windows, not in WSL: WSL does not see the FM-1.
- felucca-bsp's notes say that M-VAVE's own updater (M-UPGRADE) takes such packages too.

The package holds the program and fm1-base's update loader. After the install, the FM-1 runs only this
program, until you install another firmware.

## Which FM-1

The FM-1 was made with two flash chips: the Puya P25Q80 (ID 856014) and, in units sold since autumn 2026,
the UC25HQ80 (ID B36014). The fm1-base version that this build uses takes both (`device/fm1_flash_ok.h`),
and so does the package. A firmware that knows only the first chip starts on a unit with the second one,
but then cannot be replaced over USB: that is why this matters.

The update loader in the package writes only the program area of the flash, not the boot loader below
it. M-VAVE's stock firmware exists as V15 and V16, each with its own boot loader. fm1-base says it starts
under either. We did not try a package on either chip or under either boot loader.

## If it goes wrong

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

## What a program must keep to

A program that runs in the simulator does not always fit the FM-1. The build stops with a message for
most of these.

| Subject | On the FM-1 |
| --- | --- |
| Language | C only. No C++ |
| Numbers | `float` only. A `double` fails the build: write `0.5f`, `sinf`, `powf`. `M_PI` is a `float` here |
| C library | What `adapter/libc` has: the `float` maths, the string functions, `abs`, `rand`. No `malloc`, no `printf` |
| Memory | 464 KB of RAM for the program and the platform together, of which 8 KB must stay free. A 240 x 240 picture in memory is 115 KB |
| Program size | 581 KB of flash for program and platform |
| Stack | 24 KB for `loop()`, about 7 KB for `audio()`. The `"stack"` of `fm1project.json` is not used |
| `audio()` | It is an interrupt, and has 5.8 ms for 256 frames. Do not draw in it |
| Screen | A draw call takes time. A full screen, 30 times a second, can be too much: draw what changed |
| `loop()` | One call for each audio block, as in the simulator. A call that takes longer makes the next ones late |
| `fm1project.json` | `sources`, `include`, `define` and `flags` are used. A project with `web` is a firmware of its own: its repository builds it |
| Hardware addresses | The build refuses pointer casts to `volatile` and inline assembly in a program |

Things that are different from the simulator:

- The accuracy of the maths is about six digits (`atan2f`, `asinf`, `acosf`: about five).
- After an update that was cancelled, the FM-1 starts again.
- USB audio and Bluetooth are not built in. USB MIDI in works: `fm1_midi_take`.
- PLAY has a green LED and a red one. `fm1_led_key` lights the green one.

How fast the two example games run on a real FM-1 is not known: the host simulator does not tell. They draw the full screen for each
picture, and count on 29 pictures a second.

## Licences

The files in this folder are GPL-3.0-only, as FM-1 Studio. A package also holds code of fm1-base and
felucca-bsp (MPL-2.0, which can be combined with GPL code) and three files of JieLi's AC79 SDK
(Apache-2.0). Their licence files come with the downloads in `~/.cache/fm1studio`. Not affiliated with
M-VAVE or JieLi.
