#!/usr/bin/env python3
"""Extract Diablo UI CEL artwork from a locally owned DIABDAT.MPQ as PNGs.

No external Python packages are required. The output is for local art reference;
the game itself continues to read CEL files from its MPQ at runtime.
"""

import argparse
import ctypes
import math
from pathlib import Path
import shutil
import struct
import zlib


class Mpq:
    def __init__(self, path, library):
        self.lib = ctypes.CDLL(str(library))
        self.lib.mpqfs_open.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self.lib.mpqfs_open.restype = ctypes.c_int
        self.lib.mpqfs_close.argtypes = [ctypes.c_void_p]
        self.lib.mpqfs_read_file.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                             ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t)]
        self.lib.mpqfs_read_file.restype = ctypes.c_int
        self.handle = ctypes.c_void_p()
        code = self.lib.mpqfs_open(str(path).encode(), ctypes.byref(self.handle))
        if code:
            raise ValueError(f'MPQ open failed (error {code}): {path}')
        self.libc = ctypes.CDLL('/usr/lib/libSystem.B.dylib')
        self.libc.free.argtypes = [ctypes.c_void_p]

    def read(self, name):
        data = ctypes.c_void_p()
        size = ctypes.c_size_t()
        code = self.lib.mpqfs_read_file(self.handle, name.encode('ascii'),
                                        ctypes.byref(data), ctypes.byref(size))
        if code == 7:
            raise FileNotFoundError(name)
        if code:
            raise ValueError(f'MPQ read failed (error {code}): {name}')
        try:
            return ctypes.string_at(data, size.value)
        finally:
            self.libc.free(data)

    def __del__(self):
        if getattr(self, 'handle', None):
            self.lib.mpqfs_close(self.handle)


def cel_frames(data, widths):
    count = struct.unpack_from('<I', data)[0]
    if count > 2000 or 4 * (count + 2) > len(data):
        raise ValueError('CEL frame header is invalid')
    positions = struct.unpack_from('<' + 'I' * (count + 2), data)
    result = []
    for i in range(count):
        frame = data[positions[i + 1]:positions[i + 2]]
        if len(frame) >= 10 and struct.unpack_from('<H', frame)[0] == 10:
            frame = frame[10:]
        width = widths[i] if isinstance(widths, list) else widths
        pixels = bytearray()
        cursor = 0
        while cursor < len(frame):
            control = frame[cursor]
            cursor += 1
            if control >= 128:
                pixels.extend(b'\x00' * (256 - control))
            else:
                pixels.extend(frame[cursor:cursor + control])
                cursor += control
        if len(pixels) % width:
            raise ValueError(f'CEL frame {i + 1} has partial row')
        height = len(pixels) // width
        rows = [pixels[y * width:(y + 1) * width] for y in range(height)]
        result.append((width, height, b''.join(reversed(rows))))
    return result


def clx_frames(data):
    count = struct.unpack_from('<I', data)[0]
    if count > 2000 or 4 * (count + 2) > len(data):
        raise ValueError('CLX frame header is invalid')
    positions = struct.unpack_from('<' + 'I' * (count + 2), data)
    result = []
    for i in range(count):
        frame = data[positions[i + 1]:positions[i + 2]]
        header_size, width, height = struct.unpack_from('<HHH', frame)
        if header_size < 6:
            raise ValueError(f'CLX frame {i + 1} has invalid header size')
        pixels = bytearray()
        cursor = header_size
        while cursor < len(frame):
            control = frame[cursor]
            cursor += 1
            if control < 128:
                pixels.extend(b'\x00' * control)
            elif control <= 190:
                pixels.extend(frame[cursor:cursor + 1] * (191 - control))
                cursor += 1
            else:
                length = 256 - control
                pixels.extend(frame[cursor:cursor + length])
                cursor += length
        if len(pixels) != width * height:
            raise ValueError(f'CLX frame {i + 1} is {len(pixels)} pixels, expected {width * height}')
        rows = [pixels[y * width:(y + 1) * width] for y in range(height)]
        result.append((width, height, b''.join(reversed(rows))))
    return result


def png_chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))


def write_png(path, width, height, pixels, palette, transparent=True):
    raw = b''.join(b'\x00' + pixels[y * width:(y + 1) * width] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n'
    png += png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 3, 0, 0, 0))
    png += png_chunk(b'PLTE', palette)
    if transparent:
        png += png_chunk(b'tRNS', b'\x00')
    png += png_chunk(b'IDAT', zlib.compress(raw, 9))
    png += png_chunk(b'IEND', b'')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


DIGITS = {
    '0': ('111', '101', '101', '101', '111'),
    '1': ('010', '110', '010', '010', '111'),
    '2': ('111', '001', '111', '100', '111'),
    '3': ('111', '001', '111', '001', '111'),
    '4': ('101', '101', '111', '001', '001'),
    '5': ('111', '100', '111', '001', '111'),
    '6': ('111', '100', '111', '101', '111'),
    '7': ('111', '001', '010', '010', '010'),
    '8': ('111', '101', '111', '101', '111'),
    '9': ('111', '101', '111', '001', '111'),
}


def write_atlas(path, frames, palette, columns=10):
    cell_width = max(w for w, _, _ in frames) + 6
    cell_height = max(h for _, h, _ in frames) + 10
    width = columns * cell_width
    height = math.ceil(len(frames) / columns) * cell_height
    output = bytearray(width * height)
    for i, (sprite_width, sprite_height, sprite) in enumerate(frames):
        x0 = (i % columns) * cell_width + 3
        y0 = (i // columns) * cell_height + 2
        for y in range(sprite_height):
            for x in range(sprite_width):
                value = sprite[y * sprite_width + x]
                if value:
                    output[(y0 + y) * width + x0 + x] = value
        for d, digit in enumerate(f'{i + 1:03d}'):
            for y, row in enumerate(DIGITS[digit]):
                for x, bit in enumerate(row):
                    if bit == '1':
                        output[(y0 + cell_height - 8 + y) * width + x0 + d * 4 + x] = 255
    write_png(path, width, height, output, palette, transparent=False)


def scale_region(dst, dst_width, source, src_width, src_rect, dst_rect):
    sx, sy, sw, sh = src_rect
    dx, dy, dw, dh = dst_rect
    for y in range(dh):
        source_y = sy + y * sh // dh
        for x in range(dw):
            source_x = sx + x * sw // dw
            dst[(dy + y) * dst_width + dx + x] = source[source_y * src_width + source_x]


def write_layout_previews(mpq, inv, bottom, palette):
    _, _, inv_pixels = cel_frames(mpq.read('data\\inv\\inv.cel'), 320)[0]
    top = bytearray(400 * 240)
    scale_region(top, 400, inv_pixels, 320, (0, 0, 128, 6), (0, 0, 400, 4))
    scale_region(top, 400, inv_pixels, 320, (0, 346, 320, 6), (0, 236, 400, 4))
    scale_region(top, 400, inv_pixels, 320, (0, 6, 6, 340), (0, 4, 4, 232))
    scale_region(top, 400, inv_pixels, 320, (314, 6, 6, 340), (396, 4, 4, 232))
    # Full 218x240 content is drawn in the centre; the innermost four pixels
    # on each side are clipped so its original border does not reappear.
    for y in range(240):
        for x in range(95, 305):
            source_x = (x - 91) * 320 // 218
            source_y = y * 352 // 240
            top[y * 400 + x] = inv_pixels[source_y * 320 + source_x]
    write_png(inv / 'inventory_3ds_400x240_layout.png', 400, 240, top, palette, transparent=False)

    _, _, panel = cel_frames(mpq.read('ctrlpan\\panel8.cel'), 640)[0]
    screen = bytearray(320 * 240)
    scale_region(screen, 320, panel, 640, (0, 16, 640, 128), (0, 176, 320, 64))
    write_png(bottom / 'bottom_screen_320x240_base_layout.png', 320, 240, screen, palette, transparent=False)


def save_cel(mpq, name, width, destination, palette, all_frames=False):
    frames = cel_frames(mpq.read(name), width)
    if all_frames:
        destination.mkdir(parents=True, exist_ok=True)
        for i, (w, h, pixels) in enumerate(frames, 1):
            write_png(destination / f'{i:03d}.png', w, h, pixels, palette)
        if len(frames) > 12:
            write_atlas(destination.parent / f'{destination.name}_atlas.png', frames, palette)
    else:
        w, h, pixels = frames[0]
        write_png(destination, w, h, pixels, palette)
    return len(frames), [(w, h) for w, h, _ in frames]


def save_clx(source, destination, palette):
    frames = clx_frames(source.read_bytes())
    destination.mkdir(parents=True, exist_ok=True)
    for i, (width, height, pixels) in enumerate(frames, 1):
        write_png(destination / f'{i:03d}.png', width, height, pixels, palette)
    if len(frames) > 12:
        write_atlas(destination.parent / f'{destination.name}_atlas.png', frames, palette)
    return len(frames), [(w, h) for w, h, _ in frames]


def write_globe_demo(mpq, destination, palette):
    _, _, panel = cel_frames(mpq.read('ctrlpan\\panel8.cel'), 640)[0]
    bulbs = cel_frames(mpq.read('ctrlpan\\p8bulbs.cel'), 88)
    width, height = 88 * 3, 82 * 2
    output = bytearray(width * height)
    for row, xoffset in enumerate((96, 464)):
        _, _, bulb = bulbs[row]
        for column, fill in enumerate((0, 40, 81)):
            x0, y0 = column * 88, row * 82
            # Empty globe sits on top of the painted full globe.
            for y in range(13):
                for x in range(11, 73):
                    output[(y0 + y) * width + x0 + x] = bulb[(y + 3) * 88 + x]
            for y in range(69):
                for x in range(88):
                    output[(y0 + 13 + y) * width + x0 + x] = bulb[(y + 16) * 88 + x]
            lower_filled = min(fill, 69)
            for y in range(69 - lower_filled, 69):
                for x in range(88):
                    output[(y0 + 13 + y) * width + x0 + x] = panel[(y + 16) * 640 + xoffset + x]
            upper_filled = max(0, min(fill - 68, 13))
            for y in range(13 - upper_filled, 13):
                for x in range(11, 73):
                    value = panel[(y + 3) * 640 + xoffset + x]
                    if value:
                        output[(y0 + y) * width + x0 + x] = value
    write_png(destination, width, height, output, palette, transparent=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--mpqfs-library', type=Path, default=Path('/private/tmp/libmpqfs.dylib'))
    args = parser.parse_args()
    mpq = Mpq(args.mpq, args.mpqfs_library)
    # Town palette is used as a stable reference for the shared UI colors.
    palette = mpq.read('levels\\towndata\\town.pal')[:768]
    if len(palette) != 768:
        raise ValueError('town palette must contain 256 RGB entries')
    inv = args.output / 'Inventory'
    bottom = args.output / 'Bottom_UI'
    for filename in ['inv', 'inv_rog', 'inv_sor']:
        try:
            n, dims = save_cel(mpq, f'data\\inv\\{filename}.cel', 320, inv / f'{filename}.png', palette)
            print(filename, n, dims[:2])
        except FileNotFoundError:
            print(filename, 'not present in MPQ')
    widths = [int(x) for x in (Path(__file__).resolve().parents[1] / 'assets/data/inv/objcurs-widths.txt').read_text().split()]
    for name, path, width, target in [
        ('item icons', 'data\\inv\\objcurs.cel', widths, inv / 'objcurs_frames'),
        ('main panel', 'ctrlpan\\panel8.cel', 640, bottom / 'panel8.png'),
        ('empty globes', 'ctrlpan\\p8bulbs.cel', 88, bottom / 'p8bulbs_frames'),
        ('pressed panel buttons', 'ctrlpan\\panel8bu.cel', 71, bottom / 'panel8bu_frames'),
        ('multiplayer buttons', 'ctrlpan\\p8but2.cel', 33, bottom / 'p8but2_frames'),
        ('talk buttons', 'ctrlpan\\talkbutt.cel', 61, bottom / 'talkbutt_frames'),
        ('talk panel', 'ctrlpan\\talkpanl.cel', 640, bottom / 'talkpanl.png'),
        ('spell button icons', 'ctrlpan\\spelicon.cel', 56, bottom / 'spelicon_frames'),
        ('level button', 'data\\charbut.cel', [95] + [41] * 8, bottom / 'charbut_frames'),
        ('durability icons', 'items\\duricons.cel', 32, bottom / 'duricons_frames'),
    ]:
        try:
            count, dims = save_cel(mpq, path, width, target, palette, target.suffix != '.png')
            print(name, count, dims[:3])
        except FileNotFoundError:
            print(name, 'not present in MPQ')
    for name in ['panel8buc', 'panel8bucp', 'dirtybuc', 'dirtybucp', 'xpbar', 'talkbutton']:
        source = Path(__file__).resolve().parents[1] / 'assets/data' / f'{name}.clx'
        if source.exists():
            count, dims = save_clx(source, bottom / f'{name}_frames', palette)
            print(name, count, dims[:3])
    # Belt icons come from the same sprite sheet as inventory items.
    shutil.copytree(inv / 'objcurs_frames', bottom / 'belt_item_frames', dirs_exist_ok=True)
    shutil.copyfile(inv / 'objcurs_frames_atlas.png', bottom / 'belt_item_frames_atlas.png')
    write_globe_demo(mpq, bottom / 'globe_fill_examples.png', palette)
    write_layout_previews(mpq, inv, bottom, palette)


if __name__ == '__main__':
    main()
