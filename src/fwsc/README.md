# The files of the FIRMWARE button

These files are made by the scripts in `firmware/web` of
[FM-1 Studio](https://github.com/TimSamshuijzen/fm1studiocom). Do not change them by hand. They are the
work of others, under their own licences. Not affiliated with M-VAVE or JieLi.

| File | What | From | Licence |
| --- | --- | --- | --- |
| `llvm.wasm`, `llvm.js` | clang 23.1.5 and ld.lld with a target for JieLi's pi32v2 processor | [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm), commit `60b0340` (release `pi32v2-23.1.5-5`), built with [Emscripten](https://emscripten.org) 7.0.0 | Apache-2.0 with LLVM exceptions; Emscripten's runtime and musl: MIT and others (`licences/`) |
| `platform.json` | See below | | |

The parts of `platform.json`:

| Part | What | From | Licence |
| --- | --- | --- | --- |
| `fm1-base/device`, `include`, `lib` | The platform of the firmware | [fm1-base](https://github.com/charlesvestal/fm1-base), commit `c8e0599` | MPL-2.0. Its `LICENSE`, `LICENSING.md` and `CREDITS.md` are in the file |
| `fm1-base/bsp` | The chip and board layer | [felucca-bsp](https://github.com/hugelton/felucca-bsp), commit `18eadad` | MPL-2.0, and two Apache-2.0 files of JieLi's SDK. Its `LICENSE`, `LICENSING.md` and `NOTICE` are in the file |
| `gen/felucca_font.h` | The fonts of the platform, as bitmaps | Made by fm1-base's `tools/gen_font.py` from Barlow Semi Condensed (The Barlow Project Authors) | SIL OFL 1.1 |
| `gen/ann_words.h` | The words of the screen reader (none) | Made by fm1-base's `tools/gen_words.py` | MPL-2.0 |
| `app/` | The adapter: `fm1.h` on fm1-base's platform | `firmware/adapter` of FM-1 Studio | GPL-3.0-only |
| `res/include` | The C headers of clang | pi32v2-llvm | Apache-2.0 with LLVM exceptions |
| `rt/libclang_rt.builtins.a` | compiler-rt's builtins for pi32v2 | The release package of pi32v2-llvm, with the runtime of [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) | Apache-2.0 with LLVM exceptions; MIT-0 |
| `ota.bin` | The update loader of felucca-bsp, built with the compiler above | felucca-bsp `loader/` | MPL-2.0 |
| `sdk/uboot.boot`, `sdk/cfg_tool.bin`, `sdk/eq_cfg_hw.bin` | Files that each package must have | JieLi's AC79 SDK, tag `AC79NN_SDK_V1.2.1_2023-12-13`, [gitee.com/Jieli-Tech](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) | Apache-2.0 (the text is in the file: `fm1-base/LICENSES/Apache-2.0.txt`) |

A package that the FIRMWARE button makes has compiled code of fm1-base, felucca-bsp and the adapter, the
update loader, and the three SDK files.
