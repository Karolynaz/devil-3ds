#!/usr/bin/env python3
"""Encode the supplied 400x240 startup art with the startup menu palette.

Uses ffmpeg only to decode RGB pixels; no resizing or filtering is performed.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
rgb = subprocess.check_output([
    'ffmpeg', '-v', 'error', '-i', str(ROOT/'art/3ds/ctr_loading.png'),
    '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-',
])
if len(rgb) != 400*240*3:
    raise ValueError('Loading art must be exactly 400x240')
palette = (ROOT/'assets/ui_art/diablo.pal').read_bytes()
if len(palette) != 768:
    raise ValueError('Expected the 256-color startup palette')
colors = [tuple(palette[i:i+3]) for i in range(0, 768, 3)]
cache = {(0, 0, 0): 0}
pixels = bytearray(400*240)
indices = [0] + list(range(3, 256))
for offset in range(0, len(rgb), 3):
    color = tuple(rgb[offset:offset+3])
    if color not in cache:
        cache[color] = min(indices, key=lambda i: sum(
            (a-b)**2 for a, b in zip(color, colors[i])))
    pixels[offset//3] = cache[color]
(ROOT/'assets/data/ctr_loading.pal8').write_bytes(pixels)
print('Encoded 400x240 startup art without resizing')
