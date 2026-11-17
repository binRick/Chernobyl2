#!/usr/bin/env python3
"""Narration clips from ElevenLabs can end with a stray garbled fragment: after the last word, a short gap, then a quiet
blip (heard as a "glitch" after "liquidators" in the first cut). This finds where the speech really ends and cuts there.
   vo-trim.py check <clip>...     exit 1 if any clip has such a tail (prints them)
   vo-trim.py trim <in> <out.wav> write the clip cut 0.12 s after its last word, with a 60 ms fade"""
import array, subprocess, sys

def runs_of(path):
    raw = subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', path, '-ac', '1', '-ar', '8000', '-f', 's16le', '-'],
                         capture_output=True, check=True).stdout
    a = array.array('h'); a.frombytes(raw); w = 80                       # 10 ms windows
    e = [sum(abs(x) for x in a[i:i + w]) / w for i in range(0, len(a) - w, w)]
    runs, st = [], None
    for i, x in enumerate(e + [0]):
        if x > 150 and st is None: st = i
        if x <= 150 and st is not None: runs.append((st / 100, i / 100, max(e[st:i]))); st = None
    return runs

def speech_end(path):
    """(end of the last real word in s, [stray fragments]) -- a fragment is quiet (<15% of the line's peak) and comes
    after a gap of 0.3 s or more, at the very end."""
    runs = runs_of(path); pk = max(p for _, _, p in runs); tail = []
    for j in range(len(runs) - 1, 0, -1):
        s, t, p = runs[j]
        if p < 0.15 * pk and s - runs[j - 1][1] >= 0.3: tail.insert(0, (s, t))
        else: break
    return runs[len(runs) - 1 - len(tail)][1], tail

if sys.argv[1] == 'check':
    bad = 0
    for f in sys.argv[2:]:
        end, tail = speech_end(f)
        if tail: print(f"FAIL {f}: stray sound after the last word at {', '.join(f'{s:.2f}-{t:.2f}s' for s, t in tail)}"); bad = 1
    sys.exit(bad)
elif sys.argv[1] == 'trim':
    src, out = sys.argv[2], sys.argv[3]
    end, _ = speech_end(src); cut = end + 0.12
    subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-i', src, '-af',
                    f'atrim=0:{cut:.3f},afade=t=out:st={cut - 0.06:.3f}:d=0.06,aresample=48000', '-c:a', 'pcm_f32le', out], check=True)
