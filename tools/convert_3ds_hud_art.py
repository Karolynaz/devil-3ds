#!/usr/bin/env python3
"""Convert the supplied native HUD and exported Figma badges without changing game sprites.

Usage: python3 tools/convert_3ds_hud_art.py /path/to/town.pal
"""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def rgba(path, width, height):
    data = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'rawvideo',
                           '-pix_fmt', 'rgba', '-'], capture_output=True, check=True).stdout
    if len(data) != width * height * 4:
        raise ValueError(f'{path} must be {width}x{height}')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('palette', type=Path)
    palette = parser.parse_args().palette.read_bytes()
    if len(palette) != 768:
        raise ValueError('Expected the 256-color town palette')
    colors = [tuple(palette[i*3:i*3+3]) for i in range(128, 256)]
    cache = {(0, 0, 0): 0}
    source = rgba(ROOT/'art/3ds/hud/bottom_screen_ui.png', 320, 240)
    pixels = bytearray(320*240)
    for i in range(len(pixels)):
        r, g, b, alpha = source[i*4:i*4+4]
        color = (r*alpha//255, g*alpha//255, b*alpha//255)
        if color not in cache:
            cache[color] = 128 + min(range(128), key=lambda n: sum(
                (color[c]-colors[n][c])**2 for c in range(3)))
        pixels[i] = cache[color]
    (ROOT/'assets/data/ctr_bottom_ui.pal8').write_bytes(pixels)
    target = ROOT/'assets/data/hud'
    target.mkdir(parents=True, exist_ok=True)
    for label in ('x', 'y', 'l', 'r'):
        # Keep the original badge RGBA, including font antialiasing and shadow.
        data = rgba(ROOT/f'art/3ds/hud/button_{label}.png', 23, 23)
        (target/f'button_{label}.rgba').write_bytes(data)
    print('Converted native 320x240 HUD; preserved four 23x23 RGBA badges')


if __name__ == '__main__':
    main()
