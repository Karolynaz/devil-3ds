#!/usr/bin/env python3
"""Convert the six 400x240 inventory backgrounds to game palette indices.

Usage: python3 tools/convert_3ds_inventory_art.py path/to/town.pal
The palette is read from the user's own Diablo data, like the other 3DS art.
"""

import argparse
from pathlib import Path
import subprocess


ART = {
    'inventory_warrior_single.png': 'ctr_inventory_warrior.pal8',
    'inventory_rogue_single.png': 'ctr_inventory_rogue.pal8',
    'inventory_sorcerer_single.png': 'ctr_inventory_sorcerer.pal8',
    'inventory_warrior_stash.png': 'ctr_inventory_warrior_stash.pal8',
    'inventory_rogue_stash.png': 'ctr_inventory_rogue_stash.pal8',
    'inventory_sorcerer_stash.png': 'ctr_inventory_sorcerer_stash.pal8',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('palette', type=Path)
    args = parser.parse_args()
    palette = args.palette.read_bytes()
    if len(palette) != 768:
        raise ValueError('expected 768-byte Diablo town palette')
    colors = [tuple(palette[3 * i:3 * i + 3]) for i in range(128, 256)]
    root = Path(__file__).resolve().parents[1]
    cache = {}
    for source_name, output_name in ART.items():
        source = root / 'art/3ds' / source_name
        rgba = subprocess.run(
            ['ffmpeg', '-v', 'error', '-i', str(source), '-f', 'rawvideo',
             '-pix_fmt', 'rgba', '-'], capture_output=True, check=True,
        ).stdout
        if len(rgba) != 400 * 240 * 4:
            raise ValueError(f'{source} must be a 400x240 PNG')
        pixels = bytearray(400 * 240)
        for offset in range(0, len(rgba), 4):
            r, g, b, alpha = rgba[offset:offset + 4]
            if alpha < 128:
                continue
            color = (r, g, b)
            index = cache.get(color)
            if index is None:
                index = 128 + min(range(128), key=lambda i: (
                    (r - colors[i][0]) ** 2 + (g - colors[i][1]) ** 2
                    + (b - colors[i][2]) ** 2
                ))
                cache[color] = index
            pixels[offset // 4] = index
        output = root / 'assets/data' / output_name
        output.write_bytes(pixels)
        print(f'{output.relative_to(root)}: {len(pixels)} bytes')


if __name__ == '__main__':
    main()
