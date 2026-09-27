#!/usr/bin/env python3
"""Convert the user-supplied demon image into 3DS HOME Menu art.

Only Packaging/ctr/icon.png and banner.png are generated. In-game Diablo
artwork is loaded from the original game assets.
"""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "art/3ds/home_menu_source.png"


def render(output: str, width: int, height: int, crop: str) -> None:
    target = ROOT / "Packaging/ctr" / output
    filt = (
        f"{crop},scale={width}:{height}:"
        "force_original_aspect_ratio=decrease:flags=bicubic,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black"
    )
    subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-i", str(SOURCE),
         "-vf", filt, "-frames:v", "1", str(target)],
        check=True,
    )


if __name__ == "__main__":
    if not SOURCE.is_file():
        raise SystemExit(f"Missing source artwork: {SOURCE}")
    render("icon.png", 48, 48, "crop=1000:1000:127:0")
    render("banner.png", 256, 128, "crop=1254:820:0:0")
