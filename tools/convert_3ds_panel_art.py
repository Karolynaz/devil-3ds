#!/usr/bin/env python3
"""Convert 400x240 PNG art to 3DS paletted assets using a Diablo town palette.

Usage: python3 tools/convert_3ds_panel_art.py town.pal
Requires ffmpeg; town.pal is extracted from the user's own DIABDAT.MPQ.
"""

import argparse
from pathlib import Path
import subprocess


ART = {
    'new_character_bg.png': 'ctr_character_background.pal8',
    'new_quests_bg.png': 'ctr_quest_background.pal8',
    'new_spells_bg.png': 'ctr_spells_background.pal8',
    'inventory_warrior.png': 'ctr_inventory_warrior.pal8',
    'inventory_rogue.png': 'ctr_inventory_rogue.pal8',
    'inventory_sorc.png': 'ctr_inventory_sorcerer.pal8',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('palette', type=Path)
    args = parser.parse_args()
    palette = args.palette.read_bytes()
    if len(palette) != 256 * 3:
        raise ValueError('expected 768-byte Diablo RGB palette')
    colors = [tuple(palette[3 * i:3 * i + 3]) for i in range(128, 256)]
    cache = {(0, 0, 0): 0}
    root = Path(__file__).resolve().parents[1]
    for source_name, output_name in ART.items():
        source = root / 'art/3ds' / source_name
        result = subprocess.run(
            ['ffmpeg', '-loglevel', 'error', '-i', str(source), '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
            capture_output=True, check=True,
        )
        rgb = result.stdout
        if len(rgb) != 400 * 240 * 3:
            raise ValueError(f'{source} is not 400x240 RGB')
        pixels = bytearray(400 * 240)
        for offset in range(0, len(rgb), 3):
            color = tuple(rgb[offset:offset + 3])
            index = cache.get(color)
            if index is None:
                r, g, b = color
                index = 128 + min(range(128), key=lambda i: (
                    (r - colors[i][0]) ** 2 + (g - colors[i][1]) ** 2 + (b - colors[i][2]) ** 2
                ))
                cache[color] = index
            pixels[offset // 3] = index
        target = root / 'assets/data' / output_name
        target.write_bytes(pixels)
        print(target, len(pixels))


if __name__ == '__main__':
    main()
