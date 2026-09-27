#!/usr/bin/env python3
"""Build Devil-3Ds artwork from art/3ds/devil3ds_emblem_source.png.

Requires ffmpeg and the game's 768-byte RGB palette at assets/ui_art/diablo.pal.
The .pal8 outputs are raw 8-bit palette indices; index 0 is reserved for black.
"""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "art/3ds/devil3ds_emblem_source.png"
PALETTE = ROOT / "assets/ui_art/diablo.pal"
CROP = "crop=900:900:359:35"


def render_rgb(width: int, height: int, content_height: int | None = None) -> bytes:
    if content_height is None:
        content_height = height
    filt = (
        f"{CROP},scale={width}:{content_height}:force_original_aspect_ratio=decrease:flags=lanczos,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black,format=rgb24"
    )
    result = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(SOURCE), "-vf", filt,
         "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        check=True, capture_output=True,
    )
    expected = width * height * 3
    if len(result.stdout) != expected:
        raise RuntimeError(f"ffmpeg returned {len(result.stdout)} bytes, expected {expected}")
    return result.stdout


def write_pal8(width: int, height: int, output: Path, palette: bytes, content_height: int | None = None) -> None:
    # Index zero is black; quantize every other visible pixel against entries 1..255.
    colors = [tuple(palette[i:i + 3]) for i in range(3, 768, 3)]
    cache = {(0, 0, 0): 0}
    rgb = render_rgb(width, height, content_height)
    indices = bytearray(width * height)
    for pos in range(0, len(rgb), 3):
        color = tuple(rgb[pos:pos + 3])
        if max(color) < 8:
            continue
        index = cache.get(color)
        if index is None:
            r, g, b = color
            index = 1 + min(range(255), key=lambda n: (
                (r - colors[n][0]) ** 2 + (g - colors[n][1]) ** 2 + (b - colors[n][2]) ** 2
            ))
            cache[color] = index
        indices[pos // 3] = index
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(indices)
    print(f"{output.relative_to(ROOT)}: {width}x{height}, {len(indices)} bytes")


def write_png(width: int, height: int, output: Path) -> None:
    filt = (
        f"{CROP},scale={width}:{height}:force_original_aspect_ratio=decrease:flags=lanczos,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-i", str(SOURCE), "-vf", filt,
         "-frames:v", "1", str(output)], check=True,
    )
    print(f"{output.relative_to(ROOT)}: {width}x{height}")


def main() -> None:
    if not SOURCE.is_file():
        raise SystemExit(f"Missing source artwork: {SOURCE}")
    palette = PALETTE.read_bytes()
    if len(palette) != 256 * 3:
        raise SystemExit(f"Expected 768-byte RGB palette, got {len(palette)} bytes")
    if palette[:3] != b"\0\0\0":
        raise SystemExit("Palette entry 0 must be black")

    write_pal8(400, 240, ROOT / "assets/data/ctr_brand_title.pal8", palette, content_height=195)
    write_pal8(180, 100, ROOT / "assets/data/ctr_brand_mark.pal8", palette)
    write_png(48, 48, ROOT / "Packaging/ctr/icon.png")
    write_png(256, 128, ROOT / "Packaging/ctr/banner.png")


if __name__ == "__main__":
    main()
