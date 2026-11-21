# night-drive: the program of the promotional video

NIGHT DRIVE is a synthwave arpeggiator for the FM-1, written by an AI from the VIBE prompt of FM-1 Studio. It
is the program that the video shows: in the simulator (the screen takes), and on the real FM-1 (the FM-1
videos, which must use this `main.c`).

| File | What |
| --- | --- |
| `prompt.txt` | The prompt, as COPY in the VIBE dialog gives it on fm1studio.com, with the idea of `STORYBOARD.md` |
| `answer-1.md` | The first answer of the AI to that prompt, as it is |
| `main.c` | The code block of that answer, as it is: the program |

## Origins

The AI was Claude (Anthropic), in a chat that had only `prompt.txt`: no files, no tools, no earlier messages
(November 2026). Its first answer is used without a change, so the video can show one prompt and one answer.
A new round with the AI gives `answer-2.md` and a new `main.c`.

## Use

Paste `main.c` on FM-1 Studio (Ctrl+V outside the editor), or load it as a project. Set NAME to NIGHT DRIVE
(the name on the FM-1's start screen) before DOWNLOAD or INSTALL. `answer-1.md` tells how to play it: hold
notes, PLAY for the beat, KNOB 1 to 4 for tempo, filter, echo and drive.

## Checks

- **FM-1 Studio:** it compiles with no warning, has no `double`, and DOWNLOAD makes its package (image 77788 B,
  memory 102592 B of 475136 B).
- **In the simulator, run outside the page** (as the audio worklet runs it, 24 s with chords, PLAY and the
  knobs): `audio()` 0.012 ms on average per block of 256 frames in Node (the FM-1 has 5.8 ms for it, and is much
  slower than a computer), no NaN, no sample at full scale, peak -9.4 dBFS; the beat at 110 BPM; the screen
  changes about 29 times a second.
- **Not checked:** how it runs and sounds on a real FM-1, and its speed there (the screen of the FM-1 is slow).
  Install it and play it before the FM-1 videos.
