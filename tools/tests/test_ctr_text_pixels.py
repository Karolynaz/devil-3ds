#!/usr/bin/env python3
"""Verify native 3DS glyph sampling and cursor spacing with shipped CLX glyphs."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
RENDER = (ROOT / 'Source/engine/render/text_render.cpp').read_text()
CLX = (ROOT / 'assets/fonts/12-00.clx').read_bytes()


def glyph(frame):
    start = struct.unpack_from('<I', CLX, 4 + 4 * frame)[0]
    end = struct.unpack_from('<I', CLX, 4 + 4 * (frame + 1))[0]
    data = CLX[start:end]
    header_size, width, height = struct.unpack_from('<HHH', data)
    rows = [[0] * width for _ in range(height)]
    x = y = 0
    pos = header_size
    while pos < len(data) and y < height:
        control = data[pos]
        pos += 1
        if control < 0x80:
            run = control
            values = None
        elif control <= 0xBE:
            run = 0xBF - control
            values = [data[pos]] * run
            pos += 1
        else:
            run = 256 - control
            values = data[pos:pos + run]
            pos += run
        for i in range(run):
            if x == width:
                x = 0
                y += 1
            if y == height:
                break
            if values is not None and values[i] != 0:
                rows[y][x] = 1
            x += 1
    assert pos == len(data)
    return width, rows


def native_origin(canvas_x):
    return (canvas_x * 5 + 7) // 8


def canvas_boundary(native_x):
    return native_x * 8 // 5


def canvas_origin(native_x):
    return ((native_x - 1) * 8 // 5) + 1


def sampled_ink_columns(mask, origin):
    result = set()
    for source_x, ink in enumerate(mask):
        if not ink:
            continue
        left = canvas_boundary(origin + source_x)
        right = canvas_boundary(origin + source_x + 1)
        # The presenter samples logical column floor(native_x * 8 / 5).
        for native_x in range(400):
            sample = canvas_boundary(native_x)
            if left <= sample < right:
                result.add(native_x)
    return result


# `i` is a common narrow glyph in the shipped GameFont12 sheet. Use an actual
# row with ink across every source column so each cursor phase is observable.
width, rows = glyph(ord('i'))
assert width == 3
ink_row = next(row for row in rows if all(row))

# Confirm production draws and advances against the same snapped native origin,
# and measures the accumulated native width once rather than flooring each glyph.
assert 'const int nativeOrigin = NativeOriginFromCanvasX(globalX);' in RENDER
assert 'NativeOriginFromCanvasX(globalX) + glyphWidth + spacing' in RENDER
assert 'lineWidth += glyphW + effectiveSpacing;' in RENDER
assert 'ScaleNativeWidth(lineWidth - effectiveSpacing, scale)' in RENDER

old_failures = []
for canvas_x in range(8):
    first = native_origin(canvas_x)
    for spacing in (0, 1):
        # Old layout independently floored every glyph and converted spacing to
        # canvas pixels before advancing. This creates phase dependent gaps and
        # overlaps for repeated narrow glyphs.
        old_canvas_spacing = (spacing * 8 + 2) // 5
        old_next_canvas_x = canvas_x + width * 8 // 5 + old_canvas_spacing
        old_second = native_origin(old_next_canvas_x)
        old_first_pixels = sampled_ink_columns(ink_row, first)
        old_second_pixels = sampled_ink_columns(ink_row, old_second)
        if old_first_pixels & old_second_pixels or min(old_second_pixels) - max(old_first_pixels) - 1 != spacing:
            old_failures.append((canvas_x, spacing, sorted(old_first_pixels), sorted(old_second_pixels)))

        # New layout retains spacing in native pixels and accumulates glyph
        # widths before mapping each next origin back to the 640-wide canvas.
        second = first + width + spacing
        second_canvas_x = canvas_origin(second)
        assert native_origin(second_canvas_x) == second
        first_pixels = sampled_ink_columns(ink_row, first)
        second_pixels = sampled_ink_columns(ink_row, second)
        assert not first_pixels & second_pixels, (canvas_x, spacing, first_pixels, second_pixels)
        assert min(second_pixels) - max(first_pixels) - 1 == spacing

assert old_failures, 'fixture should demonstrate the old phase dependent spacing'
phase, spacing, before_a, before_b = old_failures[0]
print(f'PASS: actual GameFont12 i pixels stay 1:1 at every 8:5 phase; spacing 0/1 is exact.')
print(f'Old-layout counterexample: canvas phase {phase}, spacing {spacing}, native samples {before_a} and {before_b}.')
