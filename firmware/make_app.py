#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tim Samshuijzen
"""Makes an fm1-base app from a project zip of FM-1 Studio.

    make_app.py --zip PROJECT.zip --out APP_DIR [--name NAME] [--index src/index.html] [--repo URL]

A project zip has the text files of one FM-1 Studio project: an fm1.h program
(setup, audio, loop) and, optionally, fm1project.json. APP_DIR gets what
fm1-base's tools/build.py needs to build it for a real M-VAVE FM-1:

    fm1.toml            the build settings
    app.h, app.c        the adapter: fm1.h on fm1-base's platform (firmware/adapter)
    adapter/            the names header and the C library parts
    fm1.h               the API, taken from src/index.html: the same text as in the simulator
    fm1_app_name.h      APP_NAME
    fm1_prefix.h        put in front of each file of the program: the names, and the "define" of fm1project.json
    project/            the files of the zip
    words.tsv           for the screen reader (empty)
    tests/smoke.txt     a script for fm1-base's host simulator
    name.txt            the name of the build's files

It uses the Python library only, and runs nothing.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = "fm1project.json"
MAX_FILES = 570          # a zip file with more is no project
MAX_BYTES = 8 << 20      # of one file

# words.tsv. (web/make_platform.py makes the header of these words for the build in the browser.)
WORDS = """\
# The words of a screen reader (fm1-reader). An fm1.h program says nothing to it,
# and no words are published at this address.
# url: https://fm1studio.com/words/
"""

SMOKE = """\
# The program in fm1-base's host simulator (tools/build.py APP_DIR --host):
# it starts, takes keys and knobs, draws and makes sound. The pictures and the
# sound are kept with the package, to look at and to listen to.
wav sound.wav
wait 1200
shot 1-start.png
tap PLAY
wait 800
shot 2-play.png
key 18 down
wait 400
key 11 down
wait 1500
key 11 up
key 7 down
wait 1500
key 7 up
key 18 up
shot 3-keys.png
turn PRESET 2
turn K1 3
tapkey 12
wait 600
shot 4-knobs.png
leds
wavstop
"""


def fail(message):
    raise SystemExit(f"make_app: {message}")


def read_zip(path):
    """The text files of the zip: path -> text. A file that is not text is left out."""
    files, skipped = {}, []
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as error:
        fail(f"{path}: {error}")
    with archive:
        for info in archive.infolist():
            name = info.filename
            if info.is_dir():
                continue
            parts = name.split("/")
            if "\\" in name or ":" in name or name.startswith("/") or any(p in ("", ".", "..") for p in parts):
                fail(f"{path}: bad path in the zip: {name!r}")
            if any(ch in name for ch in '*?["'):
                fail(f"{path}: a file name with one of * ? [ \" cannot be built: {name!r}")
            if info.file_size > MAX_BYTES:
                fail(f"{path}: {name} is larger than {MAX_BYTES} bytes")
            data = archive.read(info)
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError:
                skipped.append(name)
                continue
            if "\0" in text:
                skipped.append(name)
                continue
            files[name] = text
    if len(files) > MAX_FILES:
        fail(f"{path}: {len(files)} files; a project has at most {MAX_FILES}")
    return files, skipped


def read_manifest(files):
    if MANIFEST not in files:
        return {}
    try:
        manifest = json.loads(files[MANIFEST])
    except json.JSONDecodeError as error:
        fail(f"{MANIFEST}: {error}")
    if not isinstance(manifest, dict):
        fail(f"{MANIFEST}: must be an object")
    for key in ("sources", "include", "define", "flags"):
        value = manifest.get(key, [])
        if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
            fail(f'{MANIFEST}: "{key}" must be a list of texts')
    return manifest


def api_header(index):
    """fm1.h as the simulator has it: the text block of the page."""
    try:
        page = Path(index).read_text(encoding="utf-8")
    except OSError as error:
        fail(f"{index}: {error}")
    found = re.search(r'<script type="text/plain" id="fm1\.h">\r?\n(.*?)</script>', page, re.S)
    if not found:
        fail(f"{index}: no fm1.h block")
    return found.group(1)


def toml_list(items):
    for item in items:
        if '"' in item or "\n" in item:
            fail(f"fm1.toml cannot hold {item!r} (a quote); put it in the \"define\" of {MANIFEST}")
    return "[" + ", ".join(f'"{item}"' for item in items) + "]"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--zip", required=True, type=Path, help="the project zip")
    parser.add_argument("--out", required=True, type=Path, help="the app directory to make (it is replaced)")
    parser.add_argument("--name", help="the name of the program on the FM-1 (default: the name of the zip)")
    parser.add_argument("--index", type=Path, default=HERE.parent / "src" / "index.html", help="FM-1 Studio's page")
    parser.add_argument("--repo", default="https://github.com/TimSamshuijzen/fm1studiocom", help="the source's address")
    args = parser.parse_args()

    files, skipped = read_zip(args.zip)
    manifest = read_manifest(files)
    if manifest.get("web") is not None:
        fail(f'{MANIFEST} has "web": this project is a firmware with its own FM-1 inside (the web_ interface). '
             "Its own repository builds its .fwsc.")

    sources = manifest.get("sources") or sorted(p for p in files if re.search(r"\.(c|cc|cpp|cxx)$", p, re.I))
    if not sources:
        fail("the project has no .c file")
    for path in sources:
        if path not in files:
            fail(f"{MANIFEST}: the project has no file {path}")
        if not path.lower().endswith(".c"):
            fail(f"{path}: a real FM-1 build takes C only (the FM-1's compiler has no C++ library)")

    # The names: of the FM-1 over USB and on its crash and safe-mode screens (capitals and digits: their fonts
    # have no more), and of the files.
    shown = re.sub(r"[^A-Z0-9 +-]", " ", (args.name or args.zip.stem).upper())
    shown = re.sub(r" +", " ", shown).strip()[:14].strip() or "PROGRAM"
    file_id = re.sub(r"[^a-z0-9]+", "_", shown.lower()).strip("_") or "program"
    # The package identity: FM-1_ and digits, the first three shown by the update tools. 58 is not in
    # the catalogue of thefirmwarehub.com (November 2026); the third digit is from the name.
    identity = "FM-1_58" + str(int(hashlib.sha256(file_id.encode()).hexdigest(), 16) % 10)

    out = args.out
    if out.exists():
        shutil.rmtree(out)
    (out / "project").mkdir(parents=True)
    for path, text in files.items():
        target = out / "project" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")

    adapter = HERE / "adapter"
    shutil.copy(adapter / "app.h", out / "app.h")
    shutil.copy(adapter / "app.c", out / "app.c")
    shutil.copytree(adapter, out / "adapter", ignore=shutil.ignore_patterns("app.h", "app.c"))
    (out / "fm1.h").write_text(api_header(args.index), encoding="utf-8", newline="\n")
    (out / "fm1_app_name.h").write_text(f'/* made by make_app.py */\n#define APP_NAME "{shown}"\n', newline="\n")

    defines = []
    for define in manifest.get("define", []):
        name, equals, value = define.partition("=")
        if not re.fullmatch(r"[A-Za-z_]\w*(\([^)]*\))?", name):
            fail(f'{MANIFEST}: "define" {define!r} is not NAME or NAME=VALUE')
        defines.append(f"#define {name} {value if equals else 1}")
    (out / "fm1_prefix.h").write_text(
        "/* made by make_app.py: in front of each file of the program */\n"
        '#include "adapter/fm1_names.h"\n' + "".join(line + "\n" for line in defines), encoding="utf-8", newline="\n")

    (out / "words.tsv").write_text(WORDS, newline="\n")
    (out / "tests").mkdir()
    (out / "tests" / "smoke.txt").write_text(SMOKE, newline="\n")
    (out / "name.txt").write_text(file_id + "\n", newline="\n")

    include = ["project", *(f"project/{d.strip('/')}" for d in manifest.get("include", []))]
    toml = f"""\
# made by make_app.py from {args.zip.name}: the settings of fm1-base's tools/build.py
name = "{file_id}"
id_prefix = "{identity}"
usb_manufacturer = "FM-1 Studio"
usb_product = "{shown}"
o2 = {toml_list([*(f"project/{p}" for p in sources), "adapter/fm1_libc.c"])}
o2_include = ["fm1_prefix.h"]
o2_cflags = {toml_list(manifest.get("flags", []))}
include = {toml_list(include)}
device_include = ["adapter/libc"]
mmio_skip = ["project/"]
words = "words.tsv"
ram_layout = "packed"
usb_audio_in = true
usb_audio_out = false
tagline = "an FM-1 Studio program"
repo = "{args.repo}"
licence = "GPL-3.0-only"
"""
    (out / "fm1.toml").write_text(toml, encoding="utf-8", newline="\n")

    print(f"make_app: {args.zip.name} -> {out}")
    print(f"  name      {shown} (files: {file_id}, identity {identity}00)")
    print(f"  sources   {', '.join(sources)}")
    if skipped:
        print(f"  left out  {', '.join(skipped)} (not text)")
    stack = manifest.get("stack")
    if isinstance(stack, int) and stack > 65536:   # (65536 is what each new project has)
        print(f"  WARNING   {MANIFEST} asks for a stack of {stack} bytes. On the FM-1, loop() has 24 KB and "
              "audio() about 7 KB.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
