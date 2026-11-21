# The promotional video of FM-1 Studio: storyboard

`timeline.json` is this storyboard as data: the scenes, their times, the words on the screen and the
sound. When one changes, change the other. `python video/render.py --list` prints the times of the timeline.

## The video

| | |
| --- | --- |
| Length | 75 seconds (a 30-second cut and a vertical cut are variants, see the end) |
| Format | MP4, H.264 High, 1920 x 1080, 30 frames a second, AAC stereo, -16 LUFS |
| Picture | Mostly screen recordings of FM-1 Studio, with videos of a real FM-1 as picture-in-picture (PiP) |
| Words | Short captions on the screen, no voice: most people watch a video on a social site without sound |
| Sound | The FM-1 itself: the sound of the program in the video, from the simulator and from the real FM-1 |
| Who for | People with an FM-1, and people who make synths, music and games with code or with an AI |

**The story in one line:** you describe a synth, an AI writes it, you play it in the browser, and it runs
on your real FM-1.

The four steps, with the colours of the four stripes of the app:

1. **Describe your idea** (VIBE)
2. **An AI chat writes the code**
3. **Paste it: it compiles and plays** (the simulator)
4. **Install it on your FM-1** (INSTALL, over USB)

The video starts with the end: the real FM-1 plays a synth that did not exist before. Then it shows how.

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

**Before you record:** do the vibe session one time without a camera. Install the result on the FM-1 and
play it. If the road moves too slowly on the real FM-1, ask the AI to make the road smaller or to move only
its lines. Keep the final `main.c` (for example in `video/night-drive/main.c`): the recordings of the
simulator and of the FM-1 need the same program.

## Storyboard

| # | Time | Scene (`id`) | Picture | PiP | Words on the screen | Sound |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0:00 | `cold-open`, 5 s | F1: the real FM-1 plays NIGHT DRIVE, close, a slow move. Fade in from black | | "What if you could just describe a synth?" | The FM-1 |
| 2 | 0:05 | `title`, 4 s | Title card: FM-1 STUDIO, the four stripes | | CODE · SIMULATE · INSTALL / for the M-VAVE FM-1, in your browser | Music |
| 3 | 0:09 | `app`, 6 s | S1: FM-1 Studio, the demo plays; a slow push in | | "Code, compile and play. All in the browser." | The demo |
| 4 | 0:15 | `idea`, 8 s | S2: VIBE, CLEAR, the idea is typed (2x); close on YOUR IDEA | | **1** Describe your idea | Music |
| 5 | 0:23 | `prompt`, 3 s | S2: close on the PROMPT, COPY, the check mark | | **1** Copy the prompt: your idea and the rules of the FM-1 | Music |
| 6 | 0:26 | `ai`, 8 s | S3: an AI chat: the prompt goes in, the answer comes (6x), copy | | **2** An AI chat writes the code; **×6** | Music |
| 7 | 0:34 | `paste`, 5 s | S4: Ctrl+V on FM-1 Studio, NAME: NIGHT DRIVE, the compile, it runs | | **3** Paste it: it compiles and plays | The simulator, music under it |
| 8 | 0:39 | `play`, 10 s | S4: the view moves close to the simulator; keys, knobs, PLAY | | **3** Play it in the FM-1 simulator | The simulator |
| 9 | 0:49 | `install-steps`, 5 s | S5: INSTALL: READ, CONNECT, the FM-1 is found (1.5x) | F2: the FM-1, in sync | **4** Install it on your FM-1 | Music |
| 10 | 0:54 | `install-write`, 6 s | S5: INSTALL NOW, the bar, done (8x) | F2: UPDATING, the restart, NIGHT DRIVE starts, in sync | **4** Install it on your FM-1; **×8** | Music |
| 11 | 1:00 | `payoff`, 9 s | F3: hands play NIGHT DRIVE on the real FM-1 | | "Your idea. Your synth. On a real FM-1." | The FM-1 |
| 12 | 1:09 | `end`, 6 s | End card: fm1studio.com, the stripes. Fade to black | | Free and open source. It runs in your browser. / own risk, not affiliated | Music |

Picture-in-picture is used where it tells the story: the install on the screen and on the real FM-1 at
the same moment. The PiP has the label REAL FM-1, so that nobody takes it for the simulator.

## Shot list

Record each shot longer than the scene: the timeline picks the part. File names are those of
`timeline.json` ("sources"); put the files in `video/footage/`.

### Screen recordings (OBS, 1920 x 1080; see README.md, "Record")

| Shot | File | What to do | Record |
| --- | --- | --- | --- |
| S1 | `screen/s1-app.mkv` | Open fm1studio.com in a fresh tab; the compiler loads and the demo plays. Click the page, play some notes with the computer keys, turn KNOB 1 | 30 s |
| S2 | `screen/s2-vibe.mkv` | Click VIBE. Click CLEAR. Type the idea (at a calm speed; the video plays it at 2x). Wait 2 s, then click COPY and wait for the check mark | 40 s |
| S3 | `screen/s3-ai.mkv` | A new chat in your AI. Paste the prompt, send it, let the answer come to its end, and click the copy button of the code block | The full answer |
| S4 | `screen/s4-paste.mkv` | Back in FM-1 Studio, close the VIBE dialog. Press Ctrl+V outside the editor: the project "no name" compiles and runs. Type NIGHT DRIVE in NAME. Hold a chord with the computer keys, turn the knobs, press PLAY | 60 s |
| S5 | `screen/s5-install.mkv` | With F2 running and the microphone on: clap one time. Click INSTALL, tick the box, NEXT, CONNECT (allow MIDI), NEXT, INSTALL NOW. Wait until the dialog says that it is done | The full install |

- In S3, hide what is private: the list of your other chats, your name and picture. A real answer of a
  real AI is the honest choice. If the code needed more than one round, show that too (see "Keep it true").
- Allow MIDI for fm1studio.com in the browser before S5 if you do not want its permission prompt in the
  video.

### Videos of the real FM-1 (a phone or a camera on a stand)

| Shot | File | What | Record |
| --- | --- | --- | --- |
| F1 | `fm1/f1-hero.mp4` | NIGHT DRIVE plays (PLAY on, a held chord). Close, from the front and a bit above; a slow move or a slow push in. The screen and the LEDs must be clear | 30 s |
| F2 | `fm1/f2-install.mp4` | At the same time as S5: the FM-1 from above, its screen well in the picture. The same clap at the start. Keep it running from the clap to NIGHT DRIVE on the screen after the restart | The full install |
| F3 | `fm1/f3-play.mp4` | Hands play NIGHT DRIVE: a chord, the knobs, PLAY. The best take of the shoot | 60 s |

- Lock the focus and the exposure on the FM-1's screen: it is bright, and the phone makes it white.
- Light from the side, not from the front: the screen and the keys shine.
- Landscape, 1080p or 4K, 30 or 60 frames a second. A phone video with a changing frame rate is fine.
- Sound: the phone's microphone is enough for the clap. For F1 and F3, a recording of the FM-1's audio
  output is better than the microphone: see `sound` in README.md.

### Sound

| Shot | File | What |
| --- | --- | --- |
| A1 | `audio/a1-night-drive.wav` | About 80 s of NIGHT DRIVE as music: a jam from the FM-1's audio output (or from the simulator). It plays under the scenes with captions |

## Words on the screen

All words are in `timeline.json`. Some other choices:

| Place | In the timeline | Other choices |
| --- | --- | --- |
| Hook (scene 1) | What if you could just describe a synth? | Describe a synth. Play it on your FM-1. / This synth started as one sentence. |
| Payoff (scene 11) | Your idea. Your synth. On a real FM-1. | From an idea to your FM-1. / Made with words. Played with hands. |
| End line (scene 12) | Free and open source. It runs in your browser. | Code, simulate, install: fm1studio.com |

## Keep it true

- **Speed.** A part that plays faster shows its speed (×6, ×8). Typing at 2x is not marked.
- **The AI.** Show a real answer. If NIGHT DRIVE took more than one round, add a short scene "Tell the AI
  what to change" (the follow-up message, or the LOG copied to the AI), or say "after a few rounds".
- **The real FM-1.** The program in F1, F2 and F3 must be the one of S4, built and installed by FM-1 Studio.
- **Claims.** "Free and open source" (GPL-3.0), "in your browser" and "over USB" are true. The end card
  says that an install is at your own risk and that FM-1 Studio is not affiliated with M-VAVE or JieLi, as
  the README does.
- **The FM-1 itself** carries M-VAVE's name: that is the product. Use no logo of M-VAVE in the cards.

## Variants

Each variant is another timeline file that uses the same recordings: `python video/render.py --timeline
video/teaser.json`.

- **A 30-second cut:** scenes 1, 4, 6, 7, 10 and 11, shorter, and the end card.
- **Vertical (9:16) for shorts and reels:** `"width": 1080, "height": 1920`, views of the simulator and of
  the FM-1 only, the captions higher.
- **A tutorial (2 to 3 minutes):** the same shots at normal speed, with the VIBE prompt and the INSTALL
  dialog readable, and the safety steps of "On a real FM-1" (M-Flasher, m-bootloader).
