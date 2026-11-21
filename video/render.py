#!/usr/bin/env python3
# render.py - makes the promotional video of FM-1 Studio from timeline.json, with ffmpeg.
# README.md in this folder tells how to work with it.
#
#   python video/render.py                  preview of the whole timeline: out/preview.mp4 (960 x 540)
#   python video/render.py --final          the video: out/fm1studio-promo.mp4 (1920 x 1080, H.264, AAC)
#   python video/render.py --scene install  one scene: out/scene-install.mp4 (add --final for full size)
#   python video/render.py --frame 41.5     one picture of the timeline: out/frame-41.50.png
#   python video/render.py --grid screen-vibe 12.0   a picture of a recording with a grid of its pixels
#   python video/render.py --list           the scenes with their times, and the recordings that are missing
#
# Each scene is rendered to a segment of its own in out/cache, which is used again until the scene, its
# recordings or the styles change. A recording that is not there yet is shown as a placeholder.
"""Renders the promotional video of FM-1 Studio from timeline.json."""

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

HERE = Path(__file__).resolve().parent
CODE = hashlib.sha1(Path(__file__).read_bytes()).hexdigest()  # a change of this script renders all segments again
RATE = 48000          # audio sample rate
RAMP = 0.25           # half the time of a change of the music level at a cut, in seconds
BG = '#0c0b0a'        # around a picture that does not fill the frame (--room of the app)
IMAGE = {'.png', '.jpg', '.jpeg', '.webp', '.bmp'}
PLACEHOLDER_BG, PLACEHOLDER_PIP = '#2a2420', '#3a3029'

MODES = {
    'preview': {'scale': 0.5, 'x264': ['-preset', 'veryfast', '-crf', '26']},
    'final': {'scale': 1.0, 'x264': ['-preset', 'slow', '-crf', '18']},
}

STYLE = {  # the style that each text style of the timeline starts from
    'font': 'text', 'size': 48, 'color': '#ede4cc', 'x': 'center', 'y': 'center', 'fade': 0.25,
    'box': None, 'boxPad': 16, 'shadow': False, 'align': 'left', 'lineSpacing': 0,
}
PIP = {  # the PiP settings that each PiP starts from
    'corner': 'bottom-right', 'width': 576, 'margin': 48, 'border': 4, 'borderColor': '#f2801f',
    'label': None, 'labelStyle': 'pipLabel', 'fade': 0.3,
}

sys.stdout.reconfigure(errors='replace')


class Stop(Exception):
    """An error in the timeline or a failed ffmpeg run: its text is for the user."""


# ---- Tools ----

def tool(name):
    """The path of ffmpeg or ffprobe: $FFMPEG / $FFPROBE, the PATH, or the folder where winget puts it."""
    env = os.environ.get(name.upper())
    if env:
        return env
    found = shutil.which(name)
    if found:
        return found
    pattern = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'WinGet', 'Packages', '*FFmpeg*', '*', 'bin', name + '.exe')
    hits = sorted(glob.glob(pattern))
    if hits:
        return hits[-1]
    raise Stop(f'{name} is not found. Install ffmpeg (Windows: winget install --id Gyan.FFmpeg.Essentials -e), '
               f'or set {name.upper()} to its path.')


def run(args, what):
    """Runs ffmpeg or ffprobe in this folder. Returns its output; stops with its error text."""
    done = subprocess.run([str(a) for a in args], cwd=HERE, capture_output=True)
    if done.returncode:
        text = done.stderr.decode('utf-8', 'replace').strip()
        raise Stop(f'{what} failed:\n{text[-3000:]}')
    return done.stdout


# ---- Small helpers ----

def opt(value):
    """A value of a filter option: quoted for the filter graph, with ':' escaped for the option."""
    text = str(value).replace('\\', '/')
    if "'" in text:
        raise Stop(f"a ' cannot be in a filter value: {text}")
    return "'" + text.replace(':', '\\:') + "'"


def colour(text):
    """'#rrggbb' or '#rrggbbaa' -> an ffmpeg colour."""
    hexa = text.lstrip('#')
    if len(hexa) == 8:
        return f'0x{hexa[:6]}@{int(hexa[6:], 16) / 255:.3f}'
    return '0x' + hexa


def even(value):
    return max(2, int(round(value / 2)) * 2)


def clock(seconds):
    return f'{int(seconds // 60)}:{seconds % 60:04.1f}'


def dump(value, indent='', prefix=0):
    """JSON as timeline.json has it: a value on one line when it fits in 120 characters."""
    flat = json.dumps(value, ensure_ascii=False)
    if not isinstance(value, (dict, list)) or not value or len(indent) + prefix + len(flat) <= 120:
        return flat
    inner = indent + '  '
    if isinstance(value, dict):
        items = []
        for key, part in value.items():
            name = json.dumps(key, ensure_ascii=False) + ': '
            items.append(inner + name + dump(part, inner, len(name)))
        return '{\n' + ',\n'.join(items) + '\n' + indent + '}'
    return '[\n' + ',\n'.join(inner + dump(part, inner) for part in value) + '\n' + indent + ']'


def save_timeline(path, timeline):
    Path(path).write_text(dump(timeline) + '\n', encoding='utf-8')


# ---- The timeline ----

class Source:
    """A recording or a picture of the timeline's "sources"."""

    def __init__(self, name, spec, project):
        self.name, self.spec = name, spec
        self.path = (HERE / spec['file']).resolve()
        self.exists = self.path.is_file()
        self.kind = 'image' if self.path.suffix.lower() in IMAGE else 'video'
        self.note = spec.get('note', '')
        self.width = self.height = self.audio = 0
        self.duration = 0.0
        if self.exists:
            self.probe(project)

    def probe(self, project):
        out = run([project.ffprobe, '-v', 'error', '-show_entries',
                   'format=duration:stream=codec_type,width,height:stream_side_data=rotation', '-of', 'json', self.path],
                  f'ffprobe of {self.path.name}')
        info = json.loads(out)
        video = [s for s in info.get('streams', []) if s.get('codec_type') == 'video']
        self.audio = sum(1 for s in info.get('streams', []) if s.get('codec_type') == 'audio')
        self.duration = float(info.get('format', {}).get('duration') or 0)
        if video:
            self.width, self.height = video[0]['width'], video[0]['height']
            rotation = [d.get('rotation', 0) for d in video[0].get('side_data_list', []) if 'rotation' in d]
            if rotation and abs(int(rotation[0])) % 180 == 90:  # a phone video upright: ffmpeg turns it
                self.width, self.height = self.height, self.width

    def stamp(self):
        """What changes the segments that use this source."""
        if not self.exists:
            return [str(self.path), None]
        stat = self.path.stat()
        return [str(self.path), stat.st_size, stat.st_mtime_ns, self.marks()]

    def marks(self):
        """The marks of the recording: <file>.marks.json next to it (capture.mjs makes it), or {}."""
        path = self.path.with_suffix('.marks.json')
        return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}

    def time(self, value):
        """A time in the recording: a number of seconds, or a mark with seconds to add, as "typing-1.0" or "copy"."""
        if isinstance(value, (int, float)):
            return float(value)
        match = re.fullmatch(r'([a-z][a-z0-9_]*)([+-]\d+(?:\.\d+)?)?', str(value))
        if not match:
            raise Stop(f'{self.name}: "in" is seconds or a mark, as "typing-1.0": {value!r}')
        if not self.exists:
            return 0.0  # a placeholder
        marks = self.marks()
        if match.group(1) not in marks:
            raise Stop(f'{self.name} has no mark {match.group(1)!r}: ' + (', '.join(marks) or 'it has no marks file'))
        return marks[match.group(1)] + float(match.group(2) or 0)


class Project:
    def __init__(self, timeline_path, mode):
        self.timeline_path = Path(timeline_path).resolve()
        self.tl = json.loads(self.timeline_path.read_text(encoding='utf-8'))
        self.mode = mode
        self.ffmpeg, self.ffprobe = tool('ffmpeg'), tool('ffprobe')
        self.fps = self.tl.get('fps', 30)
        self.k = MODES[mode]['scale']
        self.width = even(self.tl.get('width', 1920) * self.k)
        self.height = even(self.tl.get('height', 1080) * self.k)
        self.cache = HERE / 'out' / 'cache' / mode
        self.cache.mkdir(parents=True, exist_ok=True)
        (HERE / 'out' / 'cache' / 'text').mkdir(parents=True, exist_ok=True)
        self.sources = {name: Source(name, spec, self) for name, spec in self.tl.get('sources', {}).items()}
        self.offsets = self.sync_offsets()
        self.scenes = self.tl['scenes']
        # A scene has "duration" (seconds) or "beats" (beats of the timeline's "bpm"). Each cut is at the frame
        # nearest to its time, so that a row of scenes in beats stays on the beat of the music.
        seen, time, frame = set(), 0.0, 0
        beat = 60 / float(self.tl['bpm']) if self.tl.get('bpm') else None
        for scene in self.scenes:
            if scene.get('id') in seen or not scene.get('id'):
                raise Stop(f'each scene needs an id of its own: {scene.get("id")!r}')
            seen.add(scene['id'])
            if 'beats' in scene:
                if not beat:
                    raise Stop(f'scene {scene["id"]} has "beats": the timeline needs "bpm"')
                time += scene['beats'] * beat
            elif 'duration' in scene:
                time += scene['duration']
            else:
                raise Stop(f'scene {scene["id"]} needs "duration" or "beats"')
            end = max(frame + 1, round(time * self.fps))
            scene['_frames'] = end - frame
            scene['_start'] = frame / self.fps
            scene['_dur'] = scene['_frames'] / self.fps
            frame = end
        self.total = frame / self.fps

    # -- sync --

    def sync_offsets(self):
        """For each source, its sync group and offset: the time in the source is the group's clock + offset.
        A source with "sync": {"to": B, "offset": o} has the time of B + o."""
        found = {}

        def resolve(name, path=()):
            if name in found:
                return found[name]
            if name in path:
                raise Stop(f'the sync of {name} goes round in a circle')
            if name not in self.tl.get('sources', {}):
                raise Stop(f'unknown source in a sync: {name}')
            sync = self.tl['sources'][name].get('sync')
            if not sync:
                found[name] = (name, 0.0)
            else:
                group, offset = resolve(sync['to'], path + (name,))
                found[name] = (group, offset + float(sync.get('offset', 0)))
            return found[name]

        for name in self.tl.get('sources', {}):
            resolve(name)
        return found

    def synced_time(self, from_name, time, to_name):
        """The time in `to_name` that shows the same moment as `time` in `from_name`, or None."""
        group_a, offset_a = self.offsets[from_name]
        group_b, offset_b = self.offsets[to_name]
        if group_a != group_b:
            return None
        return time - offset_a + offset_b

    def source(self, name):
        if name not in self.sources:
            raise Stop(f'unknown source: {name}')
        return self.sources[name]

    # -- styles --

    def style(self, name, overrides=None):
        styles = self.tl.get('styles', {})
        if name not in styles:
            raise Stop(f'unknown text style: {name}')
        return {**STYLE, **styles[name], **(overrides or {})}

    def font(self, name):
        path = self.tl.get('fonts', {}).get(name, name)
        if not Path(path).is_file():
            raise Stop(f'font {name}: {path} is not there. Change "fonts" in the timeline.')
        return path

    def view(self, value, source):
        """A rectangle [x, y, w, h] in the pixels of the source: a name of "views", a list, or the full frame."""
        if value is None:
            return [0, 0, source.width, source.height]
        rect = self.tl.get('views', {}).get(value) if isinstance(value, str) else value
        if not rect or len(rect) != 4:
            raise Stop(f'unknown view: {value!r}')
        x, y, w, h = rect
        if source.exists and (x < 0 or y < 0 or x + w > source.width or y + h > source.height):
            raise Stop(f'view {value!r} {rect} is not inside {source.name} ({source.width} x {source.height})')
        return rect

    def textfile(self, text):
        """Writes a text for drawtext to a file (no escaping needed) and gives its path from this folder."""
        name = hashlib.sha1(text.encode('utf-8')).hexdigest()[:12]
        path = HERE / 'out' / 'cache' / 'text' / f'{name}.txt'
        if not path.exists():
            path.write_text(text, encoding='utf-8', newline='\n')  # drawtext takes a \r for one more line
        return f'out/cache/text/{name}.txt'

    def pos(self, value, size, text_size):
        """A position of a text: 'center', a number of pixels, or a negative number from the right or bottom."""
        if value == 'center':
            return f'({size}-{text_size})/2'
        if value < 0:
            return f'{size}-{text_size}-{round(-value * self.k)}'
        return str(round(value * self.k))

    def drawtext(self, text, style, start, end, dur, expansion='none'):
        """A drawtext filter. The text fades in and out, but not at the start and end of its scene: there it cuts
        with the picture, so that a text that goes on in the next scene does not blink.
        Lines of a text with "align": "center" are drawn one by one, each in the middle (drawtext's own
        text_align cuts off the lower lines). Lines of another text are one drawtext, with one box."""
        k = self.k
        lines = text.split('\n')
        if len(lines) > 1 and style.get('align') == 'center':
            step = style['size'] * 1.15 + style.get('lineSpacing', 0)  # the line height of drawtext, about
            top = style['y']
            if top == 'center':
                top = (self.tl.get('height', 1080) - step * len(lines)) / 2
            elif top < 0:
                top = self.tl.get('height', 1080) + top - step * len(lines)
            return ','.join(self.drawtext(line, {**style, 'y': top + n * step, 'align': 'left'}, start, end, dur, expansion)
                            for n, line in enumerate(lines))
        parts = [
            f'fontfile={opt(self.font(style["font"]))}', f'textfile={opt(self.textfile(text))}', f'expansion={expansion}',
            f'fontsize={max(6, round(style["size"] * k))}', f'fontcolor={colour(style["color"])}',
            f'x={opt(self.pos(style["x"], "w", "tw"))}', f'y={opt(self.pos(style["y"], "h", "th"))}',
            'y_align=font',  # y is the top of the font, and each box has the height of the font: "4" as "your"
        ]
        if style.get('box'):
            parts += ['box=1', f'boxcolor={colour(style["box"])}', f'boxborderw={max(1, round(style["boxPad"] * k))}']
        if style.get('shadow'):
            parts += ['shadowcolor=0x000000@0.65', f'shadowx={max(1, round(3 * k))}', f'shadowy={max(1, round(3 * k))}']
        if style.get('lineSpacing'):
            parts.append(f'line_spacing={round(style["lineSpacing"] * k)}')
        fade = min(float(style.get('fade', 0.25)), (end - start) / 2)
        a, b = round(start, 4), round(end, 4)
        fade_in = fade if a > 0.001 else 0
        fade_out = fade if b < dur - 0.001 else 0
        alpha = '1'
        if fade_out:
            alpha = f'if(lt(t,{b - fade_out:.4f}),1,({b}-t)/{fade_out})'
        if fade_in:
            alpha = f'if(lt(t,{a + fade_in:.4f}),(t-{a})/{fade_in},{alpha})'
        if alpha != '1':
            parts.append(f'alpha={opt(alpha)}')
        parts.append(f'enable={opt(f"between(t,{a},{b})")}')
        return 'drawtext=' + ':'.join(parts)

    # -- one scene --

    def segment(self, scene):
        """Renders the scene to a segment in out/cache (or uses the one that is there). Gives its path."""
        used = {name: self.sources[name].stamp() + [self.offsets[name][1]] for name in self.scene_sources(scene)}
        clean = {key: value for key, value in scene.items() if not key.startswith('_')}
        key = hashlib.sha1(json.dumps({
            'code': CODE, 'scene': clean, 'frames': scene['_frames'], 'mode': self.mode, 'size': [self.width, self.height], 'fps': self.fps,
            'styles': self.tl.get('styles'), 'fonts': self.tl.get('fonts'), 'views': self.tl.get('views'),
            'pip': self.tl.get('pip'), 'sources': used,
        }, sort_keys=True).encode('utf-8')).hexdigest()[:10]
        index = self.scenes.index(scene) + 1
        path = self.cache / f'{index:02d}-{scene["id"]}-{key}.mkv'
        if path.exists():
            return path
        # The older segments of this scene go. (Not those of a scene whose id starts with this id: "ai", "ai-prompt".)
        for old in self.cache.glob(f'*-{scene["id"]}-*.mkv'):
            if re.fullmatch(rf'\d+-{re.escape(scene["id"])}-[0-9a-f]+\.mkv', old.name):
                old.unlink()
        print(f'  render {scene["id"]} ({scene["_dur"]:.2f} s)', flush=True)
        args, graph = self.scene_graph(scene)
        graph_path = self.cache / f'{scene["id"]}.graph.txt'
        graph_path.write_text(graph, encoding='utf-8')
        level = '4.1' if self.fps <= 30 else '4.2'
        work = path.with_suffix('.part.mkv')
        run([self.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', *args,
             '-/filter_complex', graph_path.relative_to(HERE).as_posix(), '-map', '[vout]', '-map', '[aout]',
             '-frames:v', scene['_frames'], '-r', self.fps,
             '-c:v', 'libx264', *MODES[self.mode]['x264'], '-pix_fmt', 'yuv420p', '-profile:v', 'high', '-level:v', level,
             '-g', self.fps * 2, '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709',
             '-color_range', 'tv', '-c:a', 'pcm_s16le', '-ar', RATE, '-ac', 2, work], f'the render of scene {scene["id"]}')
        work.replace(path)
        return path

    def scene_sources(self, scene):
        parts = (scene.get('main') or {}, scene.get('pip') or {}, scene.get('sound') or {})
        return [part['source'] for part in parts if 'source' in part]

    def synced_start(self, scene, main, part, what):
        """The time in the source of `part` (the PiP or the sound) at the start of the scene: its "in", or the time
        that its sync gives for the main picture's "in"."""
        if 'in' in part:
            return self.source(part['source']).time(part['in'])
        if 'source' not in main:
            raise Stop(f'scene {scene["id"]}: the {what} needs "in" (the main picture is no recording)')
        start = self.synced_time(main['source'], self.source(main['source']).time(main.get('in', 0)), part['source'])
        if start is None:
            raise Stop(f'scene {scene["id"]}: give the {what} "in", or give {part["source"]} a "sync" to {main["source"]} '
                       f'(python video/sync.py find {main["source"]} {part["source"]})')
        return start

    def scene_graph(self, scene):
        """The ffmpeg inputs and the filter graph of one scene."""
        fps, k, W, H = self.fps, self.k, self.width, self.height
        dur = scene['_frames'] / fps
        inputs, lines, audio = [], [], []

        def add_input(args):
            inputs.extend(args)
            return sum(1 for a in inputs if a == '-i') - 1

        # The picture: a colour, a picture file, a recording, or a placeholder
        main = scene.get('main') or {'color': BG}
        speed = float(main.get('speed', 1))
        levels = {'main': 1.0 if speed == 1 else 0.0, 'pip': 0.0, **scene.get('audio', {})}
        if 'color' in main:
            lines.append(f'color=c={colour(main["color"])}:s={W}x{H}:r={fps}:d={dur:.4f},format=gbrp[base]')
        else:
            src = self.source(main['source'])
            if not src.exists:
                lines.append(self.placeholder(f'{src.name}  (main)', src.note or scene.get('note', ''), W, H, dur,
                                              PLACEHOLDER_BG, scene['id']) + '[base]')
            else:
                if src.kind == 'image':
                    i = add_input(['-loop', '1', '-framerate', fps, '-t', f'{dur:.4f}', '-i', src.path])
                    chain = f'[{i}:v]fps={fps}'
                else:
                    start = src.time(main.get('in', 0))
                    if start + dur * speed > src.duration + 0.05:
                        print(f'  note: {scene["id"]} needs {src.name} to {start + dur * speed:.1f} s, '
                              f'it has {src.duration:.1f} s: the last picture stays', flush=True)
                    i = add_input(['-ss', f'{start:.4f}', '-t', f'{dur * speed + 1:.4f}', '-i', src.path])
                    chain = f'[{i}:v]setpts=(PTS-STARTPTS)/{speed},fps={fps},tpad=stop_mode=clone:stop_duration={dur:.4f}'
                    track = int(src.spec.get('audioTrack', 0))
                    if levels['main'] > 0 and src.audio > track:
                        tempo = f',atempo={speed}' if speed != 1 else ''
                        audio.append(f'[{i}:a:{track}]asetpts=PTS-STARTPTS{tempo},volume={levels["main"]}')
                view, view_to = main.get('view'), main.get('viewTo')
                rect = self.view(view, src)
                if view_to is not None:
                    # The crop moves by whole pixels. The recording is made larger first (3 times), so that a step
                    # is a third of a pixel: a slow zoom then does not shake.
                    fine = 3 if src.width <= 1920 else 1
                    first = [value * fine for value in rect]
                    if fine > 1:
                        chain += f',scale={src.width * fine}:{src.height * fine}:flags=bicubic'
                    chain += f',sendcmd=f={opt(self.move_commands(scene, first, [v * fine for v in self.view(view_to, src)], dur))}'
                    chain += f',crop={first[2]}:{first[3]}:{first[0]}:{first[1]}:exact=1,scale={W}:{H}:flags=lanczos,format=gbrp'
                else:
                    if rect != [0, 0, src.width, src.height]:
                        chain += f',crop={rect[2]}:{rect[3]}:{rect[0]}:{rect[1]}:exact=1'
                    chain += (f',scale={W}:{H}:force_original_aspect_ratio=decrease:flags=lanczos,format=gbrp,'
                              f'pad={W}:{H}:-1:-1:color={colour(BG)}')
                lines.append(chain + '[base]')

        # The PiP
        top = 'base'
        if scene.get('pip'):
            top = self.pip_graph(scene, main, speed, levels, dur, add_input, lines, audio)

        # More sound: a recording of sound only (for example the FM-1's own output), in sync with the main picture
        sound = scene.get('sound')
        if sound:
            src = self.source(sound['source'])
            level = float(sound.get('level', 1))
            track = int(src.spec.get('audioTrack', 0))
            if src.exists and level > 0 and src.audio > track:
                start = self.synced_start(scene, main, sound, 'sound')
                sound_speed = float(sound.get('speed', 1)) if 'in' in sound else speed
                lead = max(0.0, -start)
                i = add_input(['-ss', f'{max(0.0, start):.4f}', '-t', f'{dur * sound_speed + 1:.4f}', '-i', src.path])
                tempo = f',atempo={sound_speed}' if sound_speed != 1 else ''
                delay = f',adelay={round(lead / sound_speed * 1000)}:all=1' if lead else ''
                audio.append(f'[{i}:a:{track}]asetpts=PTS-STARTPTS{tempo}{delay},volume={level}')

        # Boxes, texts, fades, and the colours of HD video
        chain = f'[{top}]null'
        for x, y, w, h, fill in scene.get('boxes', []):
            chain += (f',drawbox=x={round(x * k)}:y={round(y * k)}:w={max(1, round(w * k))}:h={max(1, round(h * k))}'
                      f':color={colour(fill)}:t=fill')
        for item in scene.get('texts', []):
            overrides = {key: value for key, value in item.items() if key not in ('text', 'style', 'from', 'to')}
            style = self.style(item.get('style', 'caption'), overrides)
            chain += ',' + self.drawtext(item['text'], style, float(item.get('from', 0)), float(item.get('to', dur)), dur)
        fade_in, fade_out = float(scene.get('fadeIn', 0)), float(scene.get('fadeOut', 0))
        if fade_in:
            chain += f',fade=t=in:st=0:d={fade_in}'
        if fade_out:
            chain += f',fade=t=out:st={dur - fade_out:.4f}:d={fade_out}'
        chain += (',scale=out_color_matrix=bt709:out_range=tv,format=yuv420p,setsar=1,'
                  'setparams=range=tv:color_primaries=bt709:color_trc=bt709:colorspace=bt709[vout]')
        lines.append(chain)

        # The sound of the scene (the music is mixed in later, over all scenes)
        samples = round(dur * RATE)
        tails = []
        for n, part in enumerate(audio):
            lines.append(f'{part},aresample={RATE},aformat=sample_fmts=fltp:channel_layouts=stereo[a{n}]')
            tails.append(f'[a{n}]')
        if len(tails) > 1:
            mix = ''.join(tails) + f'amix=inputs={len(tails)}:normalize=0:duration=longest'
        elif tails:
            mix = tails[0] + 'anull'
        else:
            mix = f'anullsrc=r={RATE}:cl=stereo'
        mix += f',apad,atrim=end_sample={samples}'
        # A short fade at each end of the scene's own sound, so that a cut does not click ("audioFade": [in, out])
        edge_in, edge_out = scene.get('audioFade', [0.03, 0.03])
        if tails and edge_in:
            mix += f',afade=t=in:st=0:d={edge_in}'
        if tails and edge_out:
            mix += f',afade=t=out:st={dur - edge_out:.4f}:d={edge_out}'
        if fade_in:
            mix += f',afade=t=in:st=0:d={fade_in}'
        if fade_out:
            mix += f',afade=t=out:st={dur - fade_out:.4f}:d={fade_out}'
        lines.append(mix + '[aout]')
        return inputs, ';\n'.join(lines) + '\n'

    def pip_graph(self, scene, main, speed, levels, dur, add_input, lines, audio):
        """Adds the PiP of the scene to the graph. Gives the label of the picture with the PiP on it."""
        fps, k, W, H = self.fps, self.k, self.width, self.height
        p = {**PIP, **self.tl.get('pip', {}), **scene['pip']}
        src = self.source(p['source'])
        width = even(p['width'] * k)
        rect = self.view(p.get('view'), src) if src.exists else [0, 0, 16, 9]
        height = even(width * rect[3] / rect[2])
        border = round(p['border'] * k)
        box_w, box_h = width + 2 * border, height + 2 * border
        margin = round(p['margin'] * k)
        label_h = 0  # the label is a tab on top of the PiP's border
        if p.get('label'):
            label_style = self.style(p['labelStyle'])
            label_h = round((label_style['size'] + 2 * label_style['boxPad']) * k)
        corner = p['corner']
        x = margin if 'left' in corner else W - margin - box_w
        y = margin + label_h if 'top' in corner else H - margin - box_h
        if 'x' in p:
            x = round(p['x'] * k)
        if 'y' in p:
            y = round(p['y'] * k)
        show_from, show_to = float(p.get('from', 0)), float(p.get('to', dur))
        fade = float(p['fade'])

        if not src.exists:
            chain = self.placeholder(f'{src.name}  (PiP)', src.note, width, height, dur, PLACEHOLDER_PIP, None)
        else:
            start = self.synced_start(scene, main, p, 'PiP')
            pip_speed = float(p.get('speed', 1)) if 'in' in p else speed  # in sync: as fast as the main picture
            lead = max(0.0, -start)  # the scene starts before the recording: black until it starts
            i = add_input(['-ss', f'{max(0.0, start):.4f}', '-t', f'{dur * pip_speed + 1:.4f}', '-i', src.path])
            chain = f'[{i}:v]setpts=(PTS-STARTPTS)/{pip_speed},fps={fps}'
            if lead:
                chain += f',tpad=start_duration={lead / pip_speed:.4f}:color=black'
            chain += f',tpad=stop_mode=clone:stop_duration={dur:.4f}'
            if rect != [0, 0, src.width, src.height]:
                chain += f',crop={rect[2]}:{rect[3]}:{rect[0]}:{rect[1]}:exact=1'
            chain += f',scale={width}:{height}:flags=lanczos'
            level = float(levels.get('pip', 0))
            track = int(src.spec.get('audioTrack', 0))
            if level > 0 and src.audio > track:
                tempo = f',atempo={pip_speed}' if pip_speed != 1 else ''
                delay = f',adelay={round(lead / pip_speed * 1000)}:all=1' if lead else ''
                audio.append(f'[{i}:a:{track}]asetpts=PTS-STARTPTS{tempo}{delay},volume={level}')
        chain += f',format=gbrap,pad={box_w}:{box_h}:{border}:{border}:color={colour(p["borderColor"])}'
        # As the texts: a fade where the PiP comes and goes in the scene, a cut at the start and end of the scene
        if show_from > 0.001:
            chain += f',fade=t=in:st={show_from}:d={fade}:alpha=1'
        if show_to < dur - 0.001:
            chain += f',fade=t=out:st={show_to - fade:.4f}:d={fade}:alpha=1'
        lines.append(chain + '[pip]')
        on = opt(f'between(t,{show_from},{show_to})')
        top = f'[base][pip]overlay=x={x}:y={y}:format=gbrp:eof_action=repeat:enable={on}'
        if p.get('label'):
            pad = label_style['boxPad'] * k
            style = self.style(p['labelStyle'], {'x': (x + pad) / k, 'y': (y - label_h + pad) / k})
            top += ',' + self.drawtext(p['label'], style, show_from, show_to, dur)
        lines.append(top + '[withpip]')
        return 'withpip'

    def placeholder(self, title, note, w, h, dur, fill, scene_id):
        """A chain that makes a placeholder picture: the name of the recording, its note, and the time."""
        mono = self.font(self.tl.get('placeholderFont', 'mono'))
        big, small = max(8, round(h / 14)), max(6, round(h / 24))
        lines = textwrap.fill(note, max(20, int(w / (small * 0.6)) - 4)) if note else ''
        clock_text = (f'{scene_id}   ' if scene_id else '') + '%{pts:hms}'
        chain = (f'color=c={colour(fill)}:s={w}x{h}:r={self.fps}:d={dur:.4f},format=gbrp,'
                 f'drawbox=x=0:y=0:w={w}:h={h}:color=0xf2801f@0.8:t={max(2, round(h / 180))},'
                 f'drawtext=fontfile={opt(mono)}:textfile={opt(self.textfile(title.upper()))}:expansion=none:'
                 f'fontsize={big}:fontcolor=0xf6b31b:x={round(w * 0.05)}:y={round(h * 0.08)}')
        if lines:
            chain += (f',drawtext=fontfile={opt(mono)}:textfile={opt(self.textfile(lines))}:expansion=none:'
                      f'fontsize={small}:fontcolor=0xcdf2ff:line_spacing={round(small * 0.4)}:'
                      f'x={round(w * 0.05)}:y={round(h * 0.08) + big * 2}')
        chain += (f',drawtext=fontfile={opt(mono)}:textfile={opt(self.textfile(clock_text))}:expansion=normal:'
                  f'fontsize={small}:fontcolor=0x6fb9d2:x={opt(f"w-tw-{round(w * 0.05)}")}:y={round(h * 0.08) + big * 2 - small * 2}')
        return chain

    def move_commands(self, scene, rect_from, rect_to, dur):
        """The sendcmd file of a view that moves: a crop for each frame, eased (smoothstep) over "move", or at one
        speed with "ease": "linear"."""
        t0, t1 = scene['main'].get('move', [0, dur])
        lines = []
        for n in range(scene['_frames']):
            t = n / self.fps
            u = 0.0 if t <= t0 else 1.0 if t >= t1 else (t - t0) / (t1 - t0)
            # "ease": "linear" for a move that goes on in the next scene (no stop at the cut)
            e = u if scene['main'].get('ease') == 'linear' else u * u * (3 - 2 * u)
            x, y, w, h = (a + (b - a) * e for a, b in zip(rect_from, rect_to))
            lines.append(f'{t:.4f} crop w {w:.2f}, crop h {h:.2f}, crop x {x:.2f}, crop y {y:.2f};')
        path = self.cache / f'{scene["id"]}.move.txt'
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        return path.relative_to(HERE).as_posix()

    # -- the whole video --

    def music_level_expression(self):
        """The level of the music over the timeline: the "music" level of each scene, with a ramp at a cut.

        The ramp ends RAMP after the cut. It takes 2 * RAMP, or the "musicFade" of the scene after the cut, in
        seconds: a longer time lets the music fade away before the scene, or come up to it."""
        levels = [float(scene.get('audio', {}).get('music', 0)) for scene in self.scenes]
        expr = f'{levels[0]}'
        for scene, before, after in zip(self.scenes[1:], levels, levels[1:]):
            if after != before:
                ramp = float(scene.get('musicFade', 2 * RAMP))
                expr += f'+({after - before})*clip((t-{scene["_start"] + RAMP - ramp:.4f})/{ramp},0,1)'
        return expr

    def music_start(self):
        """The time in the timeline where the music starts: "start" of "music", in seconds or the id of a scene."""
        start = (self.tl.get('music') or {}).get('start', 0)
        if isinstance(start, str):
            scene = next((s for s in self.scenes if s['id'] == start), None)
            if not scene:
                raise Stop(f'the music starts at scene {start!r}: there is no such scene')
            return scene['_start']
        return float(start)

    def assemble(self, segments, out_path):
        listing = self.cache / 'list.txt'
        listing.write_text(''.join(f"file '{path.name}'\n" for path in segments), encoding='utf-8')
        args = ['-f', 'concat', '-safe', '0', '-i', listing.relative_to(HERE).as_posix()]
        music = self.tl.get('music')
        music_path = (HERE / music['file']).resolve() if music else None
        lines = []
        if music and music_path.is_file():
            args += ['-ss', float(music.get('in', 0)), '-i', music_path]
            fade = float(music.get('fadeOut', 2))
            start = self.music_start()
            delay = f'adelay={round(start * 1000)}:all=1,' if start > 0 else ''
            lines.append(f'[1:a]atrim=0:{self.total - start:.4f},asetpts=PTS-STARTPTS,aresample={RATE},'
                         f'aformat=sample_fmts=fltp:channel_layouts=stereo,{delay}'
                         f'volume=eval=frame:volume={opt(str(music.get("volume", 1)) + "*(" + self.music_level_expression() + ")")},'
                         f'afade=t=out:st={self.total - fade:.4f}:d={fade}[bed]')
            lines.append('[0:a][bed]amix=inputs=2:normalize=0:duration=first[mix]')
        else:
            if music:
                print(f'  note: no music yet ({music["file"]})', flush=True)
            lines.append('[0:a]anull[mix]')
        loudness = self.tl.get('loudness', -16)
        if loudness is not None:
            lines.append(f'[mix]loudnorm=I={loudness}:TP=-1.5:LRA=11,aresample={RATE}[a]')
        else:
            lines.append('[mix]anull[a]')
        graph = self.cache / 'assemble.graph.txt'
        graph.write_text(';\n'.join(lines) + '\n', encoding='utf-8')
        out_path.parent.mkdir(parents=True, exist_ok=True)
        run([self.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', *args, '-/filter_complex',
             graph.relative_to(HERE).as_posix(), '-map', '0:v', '-c:v', 'copy', '-map', '[a]', '-c:a', 'aac', '-b:a', '192k',
             '-ar', RATE, '-movflags', '+faststart', '-t', f'{self.total:.4f}', out_path], 'the assembly of the video')
        # ffmpeg joins the segments that it can read and stops at one that it cannot, without an error
        length = float(run([self.ffprobe, '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', out_path],
                           'ffprobe of the result'))
        if length < self.total - 0.5:
            raise Stop(f'the video stops at {length:.2f} s of {self.total:.2f} s: a segment in {self.cache.name}/ is not there or not good')

    def missing(self):
        used = {name for scene in self.scenes for name in self.scene_sources(scene)}
        names = [name for name in self.sources if name in used and not self.sources[name].exists]
        music = self.tl.get('music')
        if music and not (HERE / music['file']).is_file():
            names.append('music')
        return names


# ---- Commands ----

def describe(project, path):
    """A short check of a rendered file: codecs, size, frame rate, duration."""
    out = run([project.ffprobe, '-v', 'error', '-show_entries',
               'stream=codec_name,profile,width,height,r_frame_rate,pix_fmt,sample_rate,channels:format=duration,size',
               '-of', 'json', path], 'ffprobe of the result')
    info = json.loads(out)
    parts = []
    for s in info['streams']:
        if 'width' in s:
            parts.append(f'{s["codec_name"]} {s.get("profile", "")} {s["width"]}x{s["height"]} {s["r_frame_rate"]} {s["pix_fmt"]}')
        else:
            parts.append(f'{s["codec_name"]} {s.get("sample_rate")} Hz {s.get("channels")} ch')
    size = int(info['format']['size']) / 1048576
    print(f'{path.relative_to(HERE).as_posix()}: {float(info["format"]["duration"]):.2f} s, {size:.1f} MB; ' + '; '.join(parts))


def list_scenes(project):
    print(f'{len(project.scenes)} scenes, {clock(project.total)} ({project.total:.2f} s) at {project.fps} fps\n')
    print(f'{"":>3} {"start":>6} {"dur":>6}  {"scene":<15} {"main":<36} {"pip":<24} sound')
    for n, scene in enumerate(project.scenes, 1):
        main = scene.get('main') or {}
        if 'source' in main:
            src = project.source(main['source'])
            speed = f' x{main["speed"]}' if main.get('speed', 1) != 1 else ''
            text = f'{src.name} @{src.time(main.get("in", 0)):.1f}{speed}' + ('' if src.exists else ' (missing)')
        else:
            text = 'card'
        pip = scene.get('pip')
        pip_text = '-'
        if pip:
            src = project.source(pip['source'])
            pip_text = src.name + ('' if src.exists else ' (missing)')
        levels = {'main': 1.0 if 'source' in main and float(main.get('speed', 1)) == 1 else 0.0, **scene.get('audio', {})}
        sound = ' '.join(f'{key} {value:g}' for key, value in levels.items() if value)
        if scene.get('sound'):
            sound += f' + {scene["sound"]["source"]}'
        print(f'{n:>3} {clock(scene["_start"]):>6} {scene["_dur"]:>6.2f}  {scene["id"]:<15} {text:<36} {pip_text:<24} {sound}')
    missing = project.missing()
    print('\nmissing: ' + (', '.join(missing) if missing else 'nothing'))
    if project.tl.get('music'):
        print(f'music: starts at {clock(project.music_start())}')
    for name, (group, offset) in project.offsets.items():
        if group != name:
            print(f'sync: {name} = {group} {offset:+.3f} s')


def grid(project, name, at):
    """A picture of a recording at `at` s with a grid of its pixels: to measure views."""
    src = project.source(name)
    if not src.exists:
        raise Stop(f'{name}: {src.path} is not there')
    mono = project.font(project.tl.get('placeholderFont', 'mono'))
    step = 100
    marks = []
    for x in range(0, src.width, step * 2):
        marks.append(f"drawtext=fontfile={opt(mono)}:text='{x}':fontsize=22:fontcolor=yellow:box=1:boxcolor=black@0.6:x={x + 3}:y=3")
    for y in range(step * 2, src.height, step * 2):
        marks.append(f"drawtext=fontfile={opt(mono)}:text='{y}':fontsize=22:fontcolor=yellow:box=1:boxcolor=black@0.6:x=3:y={y + 3}")
    graph = f'drawgrid=w={step}:h={step}:t=1:c=0xffff00@0.35,' + ','.join(marks)
    out = HERE / 'out' / f'grid-{name}-{at:.2f}.png'
    out.parent.mkdir(parents=True, exist_ok=True)
    run([project.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-ss', at, '-i', src.path, '-frames:v', 1, '-vf', graph, out],
        f'the grid picture of {name}')
    print(f'{out.relative_to(HERE).as_posix()}: {name} at {at} s, {src.width} x {src.height}')


def main():
    parser = argparse.ArgumentParser(description='Renders the promotional video of FM-1 Studio from timeline.json.')
    parser.add_argument('--final', action='store_true', help='full size and quality (default: a fast preview at half size)')
    parser.add_argument('--scene', action='append', metavar='ID', help='render only this scene (more than one: repeat)')
    parser.add_argument('--frame', type=float, metavar='SECONDS', help='one picture of the timeline at this time')
    parser.add_argument('--grid', nargs=2, metavar=('SOURCE', 'SECONDS'), help='a picture of a recording with a pixel grid')
    parser.add_argument('--list', action='store_true', help='list the scenes and the missing recordings')
    parser.add_argument('--strict', action='store_true', help='stop when a recording is missing')
    parser.add_argument('--timeline', default=str(HERE / 'timeline.json'), help='another timeline file')
    args = parser.parse_args()

    project = Project(args.timeline, 'final' if args.final else 'preview')
    if args.list:
        list_scenes(project)
        return
    if args.grid:
        grid(project, args.grid[0], float(args.grid[1]))
        return
    missing = project.missing()
    if missing:
        if args.strict:
            raise Stop('missing: ' + ', '.join(missing))
        print('placeholders for: ' + ', '.join(missing))
    out_dir = HERE / 'out'
    if args.frame is not None:
        scene = next((s for s in project.scenes if s['_start'] <= args.frame < s['_start'] + s['_frames'] / project.fps), None)
        if not scene:
            raise Stop(f'the timeline is {project.total:.2f} s long')
        segment = project.segment(scene)
        out = out_dir / f'frame-{args.frame:.2f}.png'
        run([project.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-ss', f'{args.frame - scene["_start"]:.4f}',
             '-i', segment, '-frames:v', 1, out], 'the picture')
        print(f'{out.relative_to(HERE).as_posix()}: scene {scene["id"]}, {args.frame - scene["_start"]:.2f} s into it')
        return
    if args.scene:
        for scene_id in args.scene:
            scene = next((s for s in project.scenes if s['id'] == scene_id), None)
            if not scene:
                raise Stop(f'no scene {scene_id}: ' + ', '.join(s['id'] for s in project.scenes))
            segment = project.segment(scene)
            out = out_dir / f'scene-{scene_id}.mp4'
            run([project.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', segment, '-c:v', 'copy', '-c:a', 'aac',
                 '-b:a', '192k', '-movflags', '+faststart', out], 'the scene file')
            describe(project, out)
        return
    segments = [project.segment(scene) for scene in project.scenes]
    out = out_dir / ('fm1studio-promo.mp4' if args.final else 'preview.mp4')
    print('  assemble', flush=True)
    project.assemble(segments, out)
    describe(project, out)


if __name__ == '__main__':
    try:
        main()
    except Stop as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
