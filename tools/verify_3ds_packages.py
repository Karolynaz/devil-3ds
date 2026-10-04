#!/usr/bin/env python3
"""Fail packaging if required core or built-in Hellfire assets are absent."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build"
mods_cmake = (root / "CMake/Mods.cmake").read_text()
match = re.search(r"set\(hellfire_mod\s+(.*?)\)", mods_cmake, re.S)
if not match:
    raise SystemExit("Cannot read the declared Hellfire asset list")
hellfire_files = match.group(1).split() + ["data/inv/objcurs2-widths.txt"]
assets = [root / "mods/hf" / name for name in hellfire_files]
assets += [root / "assets/cacert.pem", root / "assets/licenses/jsmn.txt"]
for name in ("devil-3ds.3dsx", "devil-3ds.cia"):
    package = (build / name).read_bytes()
    if name.endswith(".3dsx") and package[:4] != b"3DSX":
        raise SystemExit(f"Invalid 3DSX header: {name}")
    if len(package) < 1024 * 1024:
        raise SystemExit(f"Unexpected package size: {name}")
    for asset in assets:
        content = asset.read_bytes()
        if not content or content not in package:
            raise SystemExit(f"{name}: missing bundled asset {asset.relative_to(root)}")
    print(f"{name}: verified {len(assets)} bundled assets ({len(package)} bytes)")
