#!/usr/bin/env python3
# sync.py - puts two recordings of timeline.json in sync: a screen recording and a video of the real FM-1.
# README.md in this folder tells how to work with it.
#
#   python video/sync.py find screen-install fm1-install            the offset, from the sound of both
#   python video/sync.py find screen-install fm1-install --write    ... and put it in timeline.json
#   python video/sync.py check screen-install fm1-install --at 12   a clip of both side by side, to see and hear it
#
# The offset is in "sync" of the second source: {"to": "screen-install", "offset": -6.8} means that a moment at
# 10.0 s in screen-install is at 3.2 s in fm1-install. render.py then gives each PiP of fm1-install the time that
# goes with the main picture, also when a scene starts later or plays faster.
"""Finds and checks the offset between two recordings of the timeline."""

import argparse
import json
import subprocess
import sys

import render as R

try:
    import numpy as np
except ImportError:  # only "find" needs it
    np = None

HOP = 0.001  # the step of the sound envelopes, in seconds: the offset is found to 1 ms


def samples(project, src, track, start, length, rate=8000):
    """The sound of a source as mono float samples."""
    if src.audio <= track:
        raise R.Stop(f'{src.name} has no audio track {track} (it has {src.audio})')
    out = subprocess.run([project.ffmpeg, '-v', 'error', '-ss', str(start), '-t', str(length), '-i', str(src.path),
                          '-map', f'0:a:{track}', '-ac', '1', '-ar', str(rate), '-f', 'f32le', '-'], capture_output=True)
    if out.returncode:
        raise R.Stop(f'ffmpeg could not read the sound of {src.name}:\n{out.stderr.decode("utf-8", "replace")[-2000:]}')
    return np.frombuffer(out.stdout, dtype='<f4'), rate


def onsets(x, rate):
    """Where the sound gets louder (a clap, a note, a beat), each HOP: the rises of a 10 ms loudness."""
    hop, window = int(rate * HOP), int(rate * 0.010)
    power = np.convolve(x.astype(np.float64) ** 2, np.ones(window) / window, mode='same')[::hop]
    level = np.sqrt(power + 1e-12)
    rise = np.maximum(0.0, np.diff(level, prepend=level[0]))
    rise -= rise.mean()
    norm = np.linalg.norm(rise)
    return rise / norm if norm else rise


def find(project, a_name, b_name, args):
    if np is None:
        raise R.Stop('"find" needs numpy: pip install numpy')
    a, b = project.source(a_name), project.source(b_name)
    for src in (a, b):
        if not src.exists:
            raise R.Stop(f'{src.name}: {src.path} is not there')
    track_a = args.track_a if args.track_a is not None else int(a.spec.get('syncTrack', a.spec.get('audioTrack', 0)))
    track_b = args.track_b if args.track_b is not None else int(b.spec.get('syncTrack', b.spec.get('audioTrack', 0)))
    xa, rate = samples(project, a, track_a, args.from_a, args.length)
    xb, _ = samples(project, b, track_b, args.from_b, args.length)
    ea, eb = onsets(xa, rate), onsets(xb, rate)
    size = 1 << (len(ea) + len(eb)).bit_length()
    corr = np.fft.irfft(np.fft.rfft(eb, size) * np.conj(np.fft.rfft(ea, size)), size)  # corr[k] = sum a[n] b[n + k]
    lags = np.arange(size)
    lags[lags > size // 2] -= size
    allowed = np.abs(lags) <= args.max / HOP
    corr = np.where(allowed, corr, -np.inf)
    best = int(np.argmax(corr))
    near = np.abs(lags - lags[best]) <= 0.05 / HOP  # the second best peak: at least 50 ms from the best one
    second = np.max(np.where(near | ~allowed, -np.inf, corr))
    offset = lags[best] * HOP + args.from_b - args.from_a
    ratio = corr[best] / second if second > 0 else float('inf')
    print(f'{b_name} = {a_name} {offset:+.3f} s   (a moment at 10.000 s in {a_name} is at {10 + offset:.3f} s in {b_name})')
    print(f'match {corr[best]:.3f}, next best {second:.3f}: ' +
          ('a clear match' if ratio >= 1.5 else 'NOT clear: check it, or record a clap at the start of both'))
    if args.write:
        data = json.loads(project.timeline_path.read_text(encoding='utf-8'))
        data['sources'][b_name]['sync'] = {'to': a_name, 'offset': round(offset, 3)}
        R.save_timeline(project.timeline_path, data)
        print(f'written to {project.timeline_path.name}: sources.{b_name}.sync')


def check(project, a_name, b_name, args):
    """A clip of both recordings side by side around --at (a time in A), each with its time, A's sound on the
    left and B's on the right: out/sync-A-B.mp4."""
    a, b = project.source(a_name), project.source(b_name)
    for src in (a, b):
        if not src.exists:
            raise R.Stop(f'{src.name}: {src.path} is not there')
    tb = project.synced_time(a_name, args.at, b_name)
    if tb is None:
        raise R.Stop(f'{b_name} has no sync to {a_name}: run "find" first, or set its "sync" by hand')
    span = args.span
    starts = [args.at - span / 2, tb - span / 2]
    mono = project.font(project.tl.get('placeholderFont', 'mono'))
    inputs, lines, sounds = [], [], []
    for n, (src, start) in enumerate(zip((a, b), starts)):
        lead = max(0.0, -start)
        inputs += ['-ss', f'{max(0.0, start):.4f}', '-t', f'{span:.4f}', '-i', str(src.path)]
        label = project.textfile(f'{src.name}  %{{pts:hms:{start:.3f}}}')
        lines.append(f'[{n}:v]setpts=PTS-STARTPTS,fps=30,tpad=start_duration={lead:.4f}:stop_mode=clone:stop_duration={span},'
                     f'scale=960:540:force_original_aspect_ratio=decrease,pad=960:540:-1:-1,'
                     f'drawtext=fontfile={R.opt(mono)}:textfile={R.opt(label)}:expansion=normal:fontsize=30:'
                     f'fontcolor=yellow:box=1:boxcolor=black@0.7:boxborderw=8:x=12:y=12[v{n}]')
        track = int(src.spec.get('syncTrack', src.spec.get('audioTrack', 0)))
        if src.audio > track:
            lines.append(f'[{n}:a:{track}]asetpts=PTS-STARTPTS,adelay={round(lead * 1000)}:all=1,aresample=48000,'
                         f'aformat=channel_layouts=mono,apad,atrim=0:{span}[s{n}]')
        else:
            lines.append(f'anullsrc=r=48000:cl=mono,atrim=0:{span}[s{n}]')
        sounds.append(f'[s{n}]')
    lines.append('[v0][v1]hstack,pad=1920:1080:0:270[v]')
    lines.append(''.join(sounds) + 'amerge=inputs=2[a]')
    graph = R.HERE / 'out' / 'cache' / 'sync.graph.txt'
    graph.write_text(';\n'.join(lines) + '\n', encoding='utf-8')
    out = R.HERE / 'out' / f'sync-{a_name}-{b_name}.mp4'
    R.run([project.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', *inputs, '-/filter_complex',
           graph.relative_to(R.HERE).as_posix(), '-map', '[v]', '-map', '[a]', '-c:v', 'libx264', '-preset', 'veryfast',
           '-crf', '23', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-t', f'{span:.4f}', out], 'the sync clip')
    group, offset = project.offsets[b_name]
    print(f'{out.relative_to(R.HERE).as_posix()}: {a_name} from {starts[0]:.3f} s (left), {b_name} from {starts[1]:.3f} s (right)')
    print(f'the same moment must show at the same time. To move {b_name} later in the picture, make its "offset" smaller.')


def main():
    parser = argparse.ArgumentParser(description='Puts two recordings of timeline.json in sync.')
    sub = parser.add_subparsers(dest='command', required=True)
    f = sub.add_parser('find', help='the offset of B to A, from the sound of both')
    f.add_argument('a')
    f.add_argument('b')
    f.add_argument('--from-a', type=float, default=0.0, help='start of the part of A to compare (s)')
    f.add_argument('--from-b', type=float, default=0.0, help='start of the part of B to compare (s)')
    f.add_argument('--length', type=float, default=120.0, help='length of the parts to compare (s)')
    f.add_argument('--max', type=float, default=60.0, help='the largest offset to look for (s)')
    f.add_argument('--track-a', type=int, help='audio track of A (default: its "syncTrack", else "audioTrack", else 0)')
    f.add_argument('--track-b', type=int, help='audio track of B')
    f.add_argument('--write', action='store_true', help='put the offset in timeline.json')
    c = sub.add_parser('check', help='a clip of A and B side by side')
    c.add_argument('a')
    c.add_argument('b')
    c.add_argument('--at', type=float, required=True, help='the time in A to look at (s)')
    c.add_argument('--span', type=float, default=6.0, help='length of the clip (s)')
    parser.add_argument('--timeline', default=str(R.HERE / 'timeline.json'))
    args = parser.parse_args()
    project = R.Project(args.timeline, 'preview')
    (find if args.command == 'find' else check)(project, args.a, args.b, args)


if __name__ == '__main__':
    try:
        main()
    except R.Stop as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
