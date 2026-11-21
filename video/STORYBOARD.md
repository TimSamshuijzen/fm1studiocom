# The promotional video of FM-1 Studio: storyboard

`timeline.json` is this storyboard as data: the scenes, their times, the words on the screen and the
sound. When one changes, change the other. `python video/render.py --list` prints the times of the timeline.

## The video

| | |
| --- | --- |
| Length | 99 seconds (a 30-second cut and a vertical cut are variants, see the end) |
| Format | MP4, H.264 High, 1920 x 1080, 30 frames a second, AAC stereo, -16 LUFS |
| Picture | Mostly one screen recording of FM-1 Studio, in one take, with videos of a real FM-1 as picture-in-picture (PiP) |
| Camera | The full screen, with slow and slight pushes in (to 1.11 times). No close-ups, and no jump of the view inside the take |
| Words | Short captions on the screen, no voice: most people watch a video on a social site without sound |
| Sound | The FM-1 itself: the sound of the programs in the video, from the simulator and from the real FM-1 |
| Rhythm | The music is NIGHT DRIVE at 110 BPM, and each cut is on one of its beats |
| Who for | People with an FM-1, and people who make synths, music and games with code or with an AI |

**The story in one line:** you describe a synth, an AI writes it, you play it in the browser, and it runs
on your real FM-1.

The video has three parts:

1. **The promise** (14 s). The real FM-1 plays a synth that did not exist before, the title, and the app: an
   editor, a compiler and an FM-1 simulator in one page.
2. **What is there already** (11 s). The gallery: a band, a game, and open-source firmware from GitHub. Each
   plays in the browser.
3. **Make your own** (74 s). The four steps, with the colours of the four stripes of the app:
   1. **Describe your idea** (VIBE), and copy the prompt
   2. **An AI chat writes the code**
   3. **Paste it: it compiles and runs** (the simulator). Play it, change the code, play again
   4. **Install it on your FM-1** (INSTALL, over USB)

   Then the real FM-1 plays it, and the end card tells where.

The video starts with the end: the real FM-1 plays NIGHT DRIVE. Part 3 shows how it was made, and ends where
the video started.

## The idea that the AI writes

The video needs a program that looks good on the small screen, sounds good at once, uses the keys, the
knobs and the LEDs, and runs well on the real FM-1. Three ideas:

| Idea | What it is | For the video | Risk |
| --- | --- | --- | --- |
| **NIGHT DRIVE** (recommended) | A synthwave arpeggiator: held notes play as 16ths over a bass line and a beat; a neon sun over a grid road that moves with the tempo | Clear at once, in a picture and in sound; it plays music by itself, which is good under captions | The moving road redraws a part of the screen each frame: the real FM-1 is slow at that (77 ms for a full screen) |
| STARSEED | Calm ambient: each note key plants a star that rings its note; the stars make shapes | Beautiful, and small parts of the screen change | Slow; less energy for a 75-second promo |
| BEAT PADS | A drum machine: the 12 buttons are pads, a 16-step grid on the screen, the LEDs follow the step | Shows the LEDs and buttons best | Looks more like a tool than a "wow" |

The idea for NIGHT DRIVE, as you type it in the VIBE dialog (it fits the display without scrolling):

> NIGHT DRIVE: a synthwave arpeggiator. Hold notes and they play as 16th notes over a pulsing bass. PLAY
> starts and stops a beat (kick, snare, hats). KNOB 1 is the tempo, KNOB 2 the filter, KNOB 3 the echo,
> KNOB 4 the drive. The screen shows a neon sun over a grid road that moves with the tempo; the held notes
> are stars in the sky. The LEDs of the held keys pulse on the beat.

**NIGHT DRIVE is made:** `night-drive/` has the prompt that COPY gives, the first answer of an AI (Claude, in
a chat with only that prompt) and its `main.c`, used without a change. It compiles, makes its package and
plays in the simulator (`night-drive/README.md`). **Before the FM-1 videos:** install it on the FM-1 (NAME:
NIGHT DRIVE) and play it. If the road moves too slowly on the real FM-1, ask the AI to move only the lines of
the road, and record the screen takes again with the new `main.c`: the simulator and the FM-1 must show the
same program.

## Storyboard

The lengths are in beats of 110 BPM (11 beats are 6 seconds). The music starts at the title, which is bar 0.
Scenes 3 to 18 are parts of one screen take (S), in its sequence: a cut there is a jump in time only. The
view is the same before and after it.

| # | Time | Scene (`id`) | Picture | View | Words on the screen | Sound |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0:00 | `cold-open`, 5 s | F1: the real FM-1 plays NIGHT DRIVE, close, a slow move. Fade in from black | | "What if you could just describe a synth?" | The FM-1 |
| 2 | 0:05 | `title`, 8 beats | Title card: FM-1 STUDIO, the four stripes | | CODE · SIMULATE · INSTALL / for the M-VAVE FM-1, in your browser | Music: the arpeggio alone |
| 3 | 0:09 | `app`, 8 beats | FM-1 Studio, the demo is played | A slow push in starts at the full screen. It keeps the top of the page in view | "Code, compile and play. All in the browser." | The demo |
| 4 | 0:14 | `gallery-band`, 8 beats | The gallery: LOAD of PROG-1, the band plays | The push in goes on | "Load a band from the gallery" | PROG-1 |
| 5 | 0:18 | `gallery-game`, 6 beats | GAME-1 (NEON REEF) is played | The push in goes on | "…or a game" | The game |
| 6 | 0:21 | `gallery-firmware`, 6 beats | AURORA, loaded from GitHub, is played | The push in ends: 1.11 times | "…or open-source FM-1 firmware, from GitHub" | AURORA |
| 7 | 0:25 | `vibe`, 6 beats | The cursor goes to the VIBE button, a click, the VIBE dialog opens | Still | "Or make your own, with VIBE" | Music: the beat comes in |
| 8 | 0:28 | `idea`, 10 beats | CLEAR, the idea is typed (3.3x) | Still | **1** Describe your idea; **×3.3** | Music |
| 9 | 0:33 | `prompt`, 6 beats | The cursor goes to the PROMPT, COPY, the check mark | Still | **1** Copy the prompt: your idea and the rules of the FM-1 (at the top) | Music |
| 10 | 0:37 | `ai`, 12 beats | An AI chat, as another tab: the prompt goes in, the answer comes (6x) | The full screen | **2** An AI chat writes the code; **×6** (at the top) | Music: the filter closes |
| 11 | 0:43 | `ai-copy`, 6 beats | The end of the answer, COPY | The full screen | **2** Copy the answer (at the top) | Music |
| 12 | 0:46 | `paste`, 8 beats | Back in FM-1 Studio: Ctrl+V (the keys show), the compile, it runs | A new slow push in starts at the full screen | **3** Paste it: it compiles and runs | The music stops: the simulator |
| 13 | 0:51 | `play`, 18 beats | After NAME: NIGHT DRIVE and COMPILE: a chord, the arpeggio, PLAY and the beat | The push in ends: 1.11 times | **3** Play it in the FM-1 simulator | The simulator |
| 14 | 1:01 | `edit`, 8 beats | In the editor, 50 becomes 62 (the radius of the sun), Ctrl+Enter (the keys show) | Still | **3** Want a bigger sun? Change the code | Music, low, the filter closed |
| 15 | 1:05 | `edit-play`, 6 beats | The bigger sun, a chord | Still | **3** …and play it again | The simulator |
| 16 | 1:08 | `install-steps`, 10 beats | INSTALL: the package is made, READ, the box, CONNECT, the FM-1 is found (3.5x). PiP: F2 | Still | **4** Install it over USB; **×3.5** (at the top right) | Music: the filter opens |
| 17 | 1:14 | `install-write`, 14 beats | INSTALL NOW: the FM-1 checks the package, goes to update mode, the bar, the FM-1 starts again (7.3x). PiP: F2 | Still | **4** Install it over USB; **×7.3** | Music: more echo and drive |
| 18 | 1:21 | `install-done`, 4 beats | The dialog says that the install is done. PiP: F2, NIGHT DRIVE starts | Still | **4** Install it over USB | Music |
| 19 | 1:24 | `payoff`, 16 beats | F3: hands play NIGHT DRIVE on the real FM-1 | | "Your idea. Your synth. On a real FM-1." | The FM-1 |
| 20 | 1:32 | `end`, 12 beats | End card: fm1studio.com, the stripes. Fade to black | | Free and open source. It runs in your browser. / own risk, not affiliated | Music: the beat stops, the last chord rings |

**The view.** The take is shown as it is: the full screen. Two times, the view goes in slowly and at one
speed, over some scenes, to 1.11 times, and stays there. It goes back to the full screen only where the picture
changes as a whole: at the AI chat (another tab), and when FM-1 Studio comes back. There is no close-up of a
part of the screen.

Picture-in-picture is used where it tells the story: the install on the screen and on the real FM-1 at
the same moment. The PiP has the label REAL FM-1, so that nobody takes it for the simulator.

**The sound tells the same story.** The music is NIGHT DRIVE, played by a script (A1), and it is written to these
scenes: the arpeggio alone under the title, the beat at step 1, a closing filter while the AI writes. At the
paste it stops, and the simulator plays the same program, by hand: a chord, then PLAY. It comes back low and
dark under the change in the code, and opens with the install. Where the app or the real FM-1 sounds, the music is silent.

**Not there yet** (placeholders in the render): scenes 1 and 19 and the PiP of scenes 16 to 18, which need the
real FM-1 (F1, F3, F2). In the take, the AI chat is a replay of the real answer, and the install runs on a
stand-in FM-1 (see the shot list).

## Shot list

Record each shot longer than the scene: the timeline picks the part. File names are those of
`timeline.json` ("sources"); put the files in `video/footage/`.

### The screen: one take (see README.md, "Record")

| Shot | File | What it does | Length |
| --- | --- | --- | --- |
| S | `screen/screen.mkv` | All of the screen of the video, in one take. **Made by `capture.mjs screen`** | 5 minutes |

The take, in its sequence (the names are its marks, which `timeline.json` uses):

1. **The app.** The demo runs. Chords with the computer keys (`play`), ARP on (`arp`), KNOB 1 and 2, ARP off.
2. **The gallery.** LOAD of PROG-1 (`band_load`, `band`): the band plays. LOAD of GAME-1, PLAY (`game`): fly and
   fire with A, D, W, S and J. The gallery scrolls, LOAD of AURORA (`firmware_load`): it comes from GitHub and
   compiles (`firmware`); chords (`firmware_play`).
3. **VIBE.** The cursor goes to the VIBE button (`to_vibe`), a click: the dialog opens (`vibe`). CLEAR, the idea is
   typed (`typing`, `typed`), COPY (`copy`), CLOSE.
4. **The AI chat**, in front of the page as another tab (`chat`): Ctrl+V of the prompt (`prompt`), SEND (`send`),
   the answer comes (`answer`, `answered`), COPY (`copy_answer`), and back to FM-1 Studio (`back`).
5. **Paste.** Ctrl+V on the page (`paste`): the project "no name" compiles and runs (`running`). NAME: NIGHT DRIVE
   (`named`), COMPILE (`compiled`). A chord, PLAY (`beat`), four chords, KNOB 2 (`knobs`), PLAY.
6. **A change.** In the editor, `#define SUN_R 50` becomes 62 (`edit`, `edited`), Ctrl+Enter (`enter`), it runs
   (`changed`), two chords.
7. **INSTALL** (`install`): the package is made (`package`), the box (`agree`), NEXT, CONNECT (`connect`, `found`),
   NEXT (`write_step`), INSTALL NOW (`go`), done (`done`), CLOSE (`closed`).

- **The AI chat** is `ai-chat.html`: a plain chat page, of no company, that replays the real prompt
  (`night-drive/prompt.txt`) and the real first answer of the AI (`night-drive/answer-1.md`) at 400 characters
  a second. Its top line says that it is a replay. For a real chat in the video: record it (hide what is
  private: the list of your other chats, your name and picture), add it as a source, and give it to the scenes
  `ai` and `ai-copy`. If the code needed more than one round, show that too (see "Keep it true").
- **The install** is the real INSTALL dialog of the page, with `stand-in-fm1.js` in the place of the FM-1: a MIDI
  device made by a script, which answers the update protocol as an FM-1 does (M-VAVE's firmware version 16
  before, the identity of the package after) and keeps nothing. No MIDI device of the computer is used and no
  FM-1 is written. Its times are a guess.
- **A real install in the video:** record the screen (OBS) and the FM-1 (F2) at the same time, with a clap at the
  start. Add the screen recording as a source, give it to the scenes `install-steps`, `install-write` and
  `install-done`, and run `sync.py` on it and F2. A PiP of the real FM-1 cannot be in sync with the simulated
  install: the timeline has it there as a placeholder.

### Videos of the real FM-1 (a phone or a camera on a stand)

| Shot | File | What | Record |
| --- | --- | --- | --- |
| F1 | `fm1/f1-hero.mp4` | NIGHT DRIVE plays (PLAY on, a held chord). Close, from the front and a bit above; a slow move or a slow push in. The screen and the LEDs must be clear | 30 s |
| F2 | `fm1/f2-install.mp4` | At the same time as a real install on the screen: the FM-1 from above, its screen well in the picture. The same clap at the start. Keep it running from the clap to NIGHT DRIVE on the screen after the restart | The full install |
| F3 | `fm1/f3-play.mp4` | Hands play NIGHT DRIVE: a chord, the knobs, PLAY. The best take of the shoot | 60 s |

- Lock the focus and the exposure on the FM-1's screen: it is bright, and the phone makes it white.
- Light from the side, not from the front: the screen and the keys shine.
- Landscape, 1080p or 4K, 30 or 60 frames a second. A phone video with a changing frame rate is fine.
- Sound: the phone's microphone is enough for the clap. For F1 and F3, a recording of the FM-1's audio
  output is better than the microphone: see `sound` in README.md.

### Sound

| Shot | File | What |
| --- | --- | --- |
| A1 | `audio/a1-night-drive.wav` | 97 s of NIGHT DRIVE as music, written to the bars of the storyboard (`STORY.music` in `capture.mjs`). **Made by `capture.mjs a1-night-drive`** from the program in the simulator. When the lengths of the scenes change, change the bars there too |

## Words on the screen

All words are in `timeline.json`. Some other choices:

| Place | In the timeline | Other choices |
| --- | --- | --- |
| Hook (scene 1) | What if you could just describe a synth? | Describe a synth. Play it on your FM-1. / This synth started as one sentence. |
| Payoff (scene 19) | Your idea. Your synth. On a real FM-1. | From an idea to your FM-1. / Made with words. Played with hands. |
| End line (scene 20) | Free and open source. It runs in your browser. | Code, simulate, install: fm1studio.com |

## Keep it true

- **Speed.** A part that plays faster shows its speed (×3.3, ×6, ×7.3).
- **The AI.** Show a real answer. If NIGHT DRIVE took more than one round, add a short scene "Tell the AI
  what to change" (the follow-up message, or the LOG copied to the AI), or say "after a few rounds".
- **The gallery.** PROG-1 is by Hans Dam, and AURORA is the work of its authors (github.com/twochannels/AURORA,
  GPL-3.0-only): the app shows their names and licences on its cards and in ABOUT. They stay in the video
  (decided 2026-11-21). The page's own demos (SPACE-1, GAME-2) can take those scenes if that changes.
- **The real FM-1.** The program in F1, F2 and F3 must be the one of the take, built and installed by FM-1 Studio.
- **The install.** The install in the take is simulated. A video that says "on a real FM-1" is best with a real
  install (see the shot list).
- **Claims.** "Free and open source" (GPL-3.0), "in your browser" and "over USB" are true. The end card
  says that an install is at your own risk and that FM-1 Studio is not affiliated with M-VAVE or JieLi, as
  the README does.
- **The FM-1 itself** carries M-VAVE's name: that is the product. Use no logo of M-VAVE in the cards.

## Variants

Each variant is another timeline file that uses the same recordings: `python video/render.py --timeline
video/teaser.json`.

- **A 30-second cut:** scenes 1, 7, 8, 10, 12, 13, 17 and 19, shorter, and the end card.
- **Vertical (9:16) for shorts and reels:** `"width": 1080, "height": 1920`, views of the simulator and of
  the FM-1 only, the captions higher (this cut needs close views, which the main video does not use).
- **A tutorial (2 to 3 minutes):** the same shots at normal speed, with the VIBE prompt and the INSTALL
  dialog readable, and the safety steps of "On a real FM-1" (M-Flasher, m-bootloader).
