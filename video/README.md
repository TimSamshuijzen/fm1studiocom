# video: the promotional video of FM-1 Studio

This directory makes the promotional video of FM-1 Studio: a screen recording of the app, with videos of
a real FM-1 as picture-in-picture (PiP). The video is not edited by hand. A script makes it from a
timeline file and the recordings, so each change is an edit of the timeline and a new render.

The state now: NIGHT DRIVE, the program of the video, is made (`night-drive/`). The screen takes S1, S2 and S4
and the music A1 are recorded by `capture.mjs`, and the preview uses them. Not there yet, and placeholders in
the render: the AI chat (S3), the install (S5) and the videos of the real FM-1 (F1, F2, F3).

## Contents

| File | What |
| --- | --- |
| `STORYBOARD.md` | The idea, the storyboard, the shot list and the words on the screen |
| `timeline.json` | The edit: the recordings, their sync, the scenes, the texts, the sound |
| `render.py` | Makes the video from `timeline.json` with ffmpeg |
| `sync.py` | Finds the offset between two recordings from their sound, and makes a clip to check it |
| `capture.mjs` | Records the screen takes of FM-1 Studio with a script, in Chrome, and renders the music |
| `night-drive/` | NIGHT DRIVE, the program of the video: the VIBE prompt, the answers of the AI, and `main.c` |
| `footage/` | The recordings: `screen/`, `fm1/`, `audio/` (not in Git) |
| `out/` | The renders, and a cache of the scenes (not in Git) |

## What you need

- **Python 3.10 or later**, with numpy for `sync.py find` (`pip install numpy`).
- **ffmpeg** with ffprobe, a recent version (tested with 9.0.2: the scripts read a filter graph from a file
  with `-/filter_complex`). On Windows: `winget install --id Gyan.FFmpeg.Essentials -e`.
  `render.py` finds it on the PATH, in the folder where winget puts it, or at `$FFMPEG` and `$FFPROBE`.
- **Node.js 22 or later and Chrome (or Edge)** for `capture.mjs`. It needs no npm package.
- **OBS Studio** for the screen takes that a script cannot make (the AI chat, the install on the real FM-1), and
  a phone or a camera on a stand for the FM-1.
- The fonts of `"fonts"` in the timeline: Arial Narrow, Arial and Consolas of Windows, the fonts that the app
  shows on Windows. On another computer, change those paths.

## The method

1. **Plan.** Change the story in `STORYBOARD.md` and the same in `timeline.json`.
2. **Look at it.** `python video/render.py` makes `out/preview.mp4` (960 x 540) in some seconds. A recording
   that is not there is a placeholder with its name, its note and the time.
3. **Record.** `node video/capture.mjs --all` records the screen takes that a script plays, and the music. Make
   the other shots of the shot list by hand (see "Record"). Put each file at its path of `"sources"`.
4. **Sync.** `python video/sync.py find screen-install fm1-install --write` puts the offset of the FM-1's video
   in the timeline. `python video/sync.py check screen-install fm1-install --at 12` makes a clip to see it.
5. **Edit.** Set `"in"` (the time in the recording where a scene starts), `"duration"`, `"speed"`, the views
   and the texts. Look at one scene with `--scene ID`, or at one picture with `--frame SECONDS`.
6. **Make the video.** `python video/render.py --final` makes `out/fm1studio-promo.mp4`.
7. **Change and make it again.** Only the scenes that changed render again: their segments are in `out/cache`.

## Record

### The screen, with a script

`capture.mjs` records the takes S1, S2 and S4 of the shot list, and makes the music A1:

```
node video/capture.mjs --all              all takes
node video/capture.mjs s4-paste           one take (s1-app, s2-vibe, s4-paste, a1-night-drive)
node video/capture.mjs --show s2-vibe     with a visible window, to watch it
node video/capture.mjs --list             the takes
```

- **How:** Chrome runs without a window, with a profile of its own in a temporary folder, and gets the page from
  a small web server of the script that serves `src/` (`--url` for another address). The page is 1920 x 1080. A
  take is a script of clicks, typing, keys and knobs (`TAKES` in `capture.mjs`), with a cursor drawn in the page:
  a browser without a window has no mouse pointer. Ctrl+V shows its two keys on the screen.
- **The recording:** the page records itself: the picture of its tab (tab capture), and the sound of the FM-1
  simulator from its audio node. Nothing plays on the speakers, and the clipboard of the computer is not used
  (COPY works as usual, and Ctrl+V gives the page a paste with the text). The VIBE prompt names
  `https://fm1studio.com/`, as on the site.
- **The result:** `footage/screen/<take>.mkv` (1920 x 1080, 30 frames a second, H.264 CRF 14, FLAC) and
  `<take>.marks.json`: the times of the moments of the take, as `{"typing": 4.733, "copy": 32.6}`. In the
  timeline, `"in": "typing-1.0"` is one second before the mark `typing`, so that a new recording of a take
  needs no change in the timeline.
- **The music:** `a1-night-drive` compiles NIGHT DRIVE in the page, then runs the program in Node, as the audio
  worklet runs it, with a script of chords, PLAY and knobs (`STORY.music` in `capture.mjs`):
  `footage/audio/a1-night-drive.wav`, exactly in time, without a capture.
- **S3 and S5** are not made by the script: S3 is a real AI chat, and S5 the install on your real FM-1, at the
  same time as F2. Record those two by hand.

### The screen, by hand

- **The browser:** Chrome or Edge (INSTALL needs Web MIDI), full screen (F11), page zoom 100%, Windows display
  scaling 100% if you can. The page then is 1920 x 1080: the views in `timeline.json` ("simulator", "vibe",
  "install", ...) were measured on the page at that size. At another size, check them with `--grid`.
- **OBS:** Settings, Video: base and output resolution 1920 x 1080, 30 or 60 frames a second. Output (mode
  Advanced), Recording: format Matroska (`.mkv`, which survives a crash), audio tracks 1 and 2, encoder x264
  with CRF 16 (or a hardware encoder at CQP 16 to 18). Source: Display Capture or Window Capture, with the
  cursor.
- **Two audio tracks:** the desktop sound on track 1 only, the microphone on track 2 only (Edit, Advanced
  Audio Properties, Tracks). Track 1 is the sound of the simulator. Track 2 hears the clap for the sync
  (`"syncTrack": 1` of `screen-install`: tracks count from 0).

### The real FM-1

A phone or a camera on a stand, landscape, the focus and exposure locked on the FM-1's screen. See the shot
list in `STORYBOARD.md` for each shot.

### The sync of two recordings

Record the FM-1 and the screen at the same time when they show the same moments (the install). At the start
of such a take, clap one time where the phone and the computer's microphone hear it. `sync.py find` then
finds the offset to 1 ms.

Without a clap, find the offset by eye: a moment that both show (INSTALL NOW on the screen, and UPDATING on
the FM-1). Put `"sync": {"to": "screen-install", "offset": T_FM1 - T_SCREEN}` in the FM-1's source, and check
it with `sync.py check`.

## The timeline file

`timeline.json` is plain JSON. Times are in seconds. Texts, boxes and the PiP are in pixels of the
1920 x 1080 video (the preview scales them). Views are in pixels of the recording.

| Key | What |
| --- | --- |
| `width`, `height`, `fps` | The video: 1920, 1080, 30 |
| `loudness` | The loudness of the result in LUFS (-16), or `null` for none |
| `fonts` | A name for each font file |
| `styles` | The text styles: `font`, `size`, `color` (`#rrggbb` or `#rrggbbaa`), `x` and `y` (pixels, `"center"`, or negative: from the right or bottom), `box` (a colour behind the text), `boxPad`, `shadow`, `align` (`"center"` for each line), `lineSpacing`, `fade` (s) |
| `views` | Names for rectangles `[x, y, w, h]` of a recording, best 16:9 |
| `pip` | The defaults of each PiP: `corner`, `width`, `margin`, `border`, `borderColor`, `label`, `labelStyle` |
| `music` | `file`, `in`, `volume`, `fadeOut`: the music under the whole video |
| `sources` | Each recording: `file` (from this folder), `note` (what it shows; the placeholder shows it), `audioTrack` (the track for the sound, default 0), `syncTrack` (the track for `sync.py`), `sync` |
| `scenes` | The scenes, one after the other |

`"sync": {"to": "A", "offset": o}` in source B: a moment at time t in A is at t + o in B. A PiP or a sound of
B without `"in"` then gets the time that goes with the main picture A, at the same speed. Sources can be in
sync through others (C to B, B to A).

The keys of a scene:

| Key | What |
| --- | --- |
| `id`, `duration`, `note` | The name, the length, and what it shows |
| `main` | The picture: `{"source", "in", "speed", "view", "viewTo", "move"}`, or `{"color": "#171615"}` for a card. `in` is seconds, or a mark of the recording with seconds to add (`"copy-1.8"`). With `viewTo`, the view moves from `view` to `viewTo` during `move` `[from, to]` (default: the whole scene) |
| `pip` | `{"source", "view", "width", "corner", "x", "y", "from", "to", "label", ...}`; `"in"` and `"speed"` only for a PiP without sync |
| `sound` | `{"source", "level"}`: one more recording of sound, in sync with the main picture (for example the FM-1's audio output with F3) |
| `audio` | The levels: `main` (default 1, and 0 when `speed` is not 1), `pip` (default 0), `music` (default 0) |
| `texts` | `[{"text", "style", "from", "to", ...}]`; other keys change the style for this text. A text fades in and out inside its scene, and cuts at the start and end of the scene |
| `boxes` | `[[x, y, w, h, "#colour"], ...]`, for example the four stripes of the cards |
| `fadeIn`, `fadeOut` | A fade from and to black, picture and sound (s) |

## Commands

```
python video/render.py                          a preview of the whole timeline: out/preview.mp4
python video/render.py --final                  the video: out/fm1studio-promo.mp4
python video/render.py --scene play             one scene: out/scene-play.mp4 (with --final: full size)
python video/render.py --frame 41.5             one picture of the timeline: out/frame-41.50.png
python video/render.py --grid screen-vibe 12    a picture of a recording with a grid of its pixels, to measure a view
python video/render.py --list                   the scenes with their times, the missing recordings, the syncs
python video/render.py --strict --final         stops when a recording is missing
python video/render.py --timeline other.json    another timeline (a variant)
python video/sync.py find A B [--write]         the offset of B to A, from the sound of both
python video/sync.py check A B --at SECONDS     out/sync-A-B.mp4: A and B side by side with their times, A's sound left, B's right
```

`sync.py find` takes the first 120 s of each recording (`--from-a`, `--from-b`, `--length`) and looks for an
offset of up to 60 s (`--max`). It says when the match is not clear.

## The result

`out/fm1studio-promo.mp4`: MP4 with H.264 High (level 4.1, 4.2 above 30 frames a second), 1920 x 1080, 4:2:0,
BT.709, x264 preset slow with CRF 18; AAC 192 kb/s, 48 kHz, stereo, loudness -16 LUFS; `faststart` for the web.

Each scene renders to its own segment in `out/cache/final` (or `preview`), with the sound uncompressed. The
segments join without a new encode of the picture; the sound is mixed with the music and encoded one time.
A segment renders again when its scene, its recordings, the styles, the views or `render.py` change.

## Limits

- The PiP in sync and `sync.py` were tested with synthetic recordings only (a screenshot of the app with a
  clock, a test picture, a clap), at 1x and 8x. The screen takes of `capture.mjs` are in the preview; no video
  of a real FM-1 was used yet.
- `capture.mjs` records in real time: a slow computer can drop frames of the tab capture (the footage then
  repeats a frame). Its cursor is drawn in the page, and the keys and the paste come from the script.
- Cuts and fades to black only, no cross-fade between scenes.
- A moving view moves by whole pixels of the recording: a slow zoom into a 1080p recording can shake a
  little. A recording at a higher resolution (with its views multiplied) gives sharper and smoother zooms.
- The music level changes in 0.5 s at a cut. The loudness is one pass of ffmpeg's `loudnorm`.
- The cache does not see a change in a font file.
