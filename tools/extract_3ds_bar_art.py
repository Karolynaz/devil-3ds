#!/usr/bin/env python3
"""Crop and scale the supplied CTR health, mana, and experience bar reference.

Usage: python3 tools/extract_3ds_bar_art.py path/to/reference.png
Requires ffmpeg. Output PNGs retain the source alpha channel.
"""

import argparse
from pathlib import Path
import subprocess


# Reference canvas is 1536x1024. Each crop encloses the complete jeweled bar,
# leaving the surrounding colored glow outside so it can be alpha-composited.
ART = {
    'ctr_mana_bar.png': ('40:134:1456:232', 'transpose=cclock,scale=26:84:flags=lanczos'),
    'ctr_health_bar.png': ('40:390:1456:245', 'transpose=cclock,scale=26:84:flags=lanczos'),
    'ctr_experience_bar.png': ('40:644:1456:235', 'scale=224:14:flags=lanczos'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    destination = root / 'art/3ds'
    for name, (crop, transform) in ART.items():
        x, y, width, height = crop.split(':')
        vf = f'crop={width}:{height}:{x}:{y},{transform},format=rgba'
        subprocess.run([
            'ffmpeg', '-y', '-loglevel', 'error', '-i', str(args.reference),
            '-vf', vf, '-frames:v', '1', str(destination / name),
        ], check=True)
        print(destination / name)


if __name__ == '__main__':
    main()
