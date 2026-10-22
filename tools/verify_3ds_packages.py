#!/usr/bin/env python3
"""Fail packaging if required core or built-in Hellfire assets are absent."""
import re
import sys
import struct
from pathlib import Path

NETWORK_SERVICES = {"ac:u", "ndm:u", "soc:U", "ssl:C", "ps:ps"}


def verify_network_services(package):
    header, _, _, certificates, ticket, tmd = struct.unpack_from("<IHHIII", package)
    align = lambda n: (n + 63) & ~63
    content = align(header) + align(certificates) + align(ticket) + align(tmd)
    if package[content + 0x100:content + 0x104] != b"NCCH":
        raise ValueError("CIA has no readable NCCH header")
    exheader = content + 0x200
    for name, position in (("requested", 0x250), ("allowed", 0x650)):
        table = package[exheader + position:exheader + position + 34 * 8]
        services = {table[i:i+8].rstrip(b"\0").decode("ascii") for i in range(0, len(table), 8)}
        missing = NETWORK_SERVICES - services
        if missing:
            raise ValueError(f"CIA {name} service permissions missing: {', '.join(sorted(missing))}")


def verify_cia_version(package, version):
    components = [int(part) for part in version.strip().split(".")]
    if len(components) == 2:
        components.append(0)
    major, minor, micro = components
    if not (0 <= major <= 63 and 0 <= minor <= 63 and 0 <= micro <= 15):
        raise ValueError("CIA version exceeds its 6/6/4-bit fields")
    expected = (major << 10) | (minor << 4) | micro
    header, _, _, certificates, ticket, _ = struct.unpack_from("<IHHIII", package)
    align = lambda n: (n + 63) & ~63
    tmd = align(header) + align(certificates) + align(ticket)
    signature = struct.unpack_from(">I", package, tmd)[0]
    body_offset = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
                   0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}.get(signature)
    if body_offset is None:
        raise ValueError("Unsupported CIA TMD signature")
    actual = struct.unpack_from(">H", package, tmd + body_offset + 0x9C)[0]
    if actual != expected:
        raise ValueError(f"CIA version {actual} differs from port {version.strip()} ({expected})")
    print(f"CIA installer version verified: {major}.{minor}.{micro} ({actual})")


def verify_cia_identity(package, expected):
    header, _, _, certificates, ticket_size, tmd_size = struct.unpack_from("<IHHIII", package)
    align = lambda n: (n + 63) & ~63
    ticket = align(header) + align(certificates)
    tmd = ticket + align(ticket_size)
    content = tmd + align(tmd_size)
    def signed_body(offset):
        signature = struct.unpack_from(">I", package, offset)[0]
        size = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
                0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}.get(signature)
        if size is None:
            raise ValueError("Unsupported CIA signature")
        return offset + size
    fields = [("ticket", ">Q", signed_body(ticket) + 0x9C),
              ("TMD", ">Q", signed_body(tmd) + 0x4C),
              ("NCCH partition", "<Q", content + 0x108),
              ("NCCH program", "<Q", content + 0x118),
              ("requested program", "<Q", content + 0x400),
              ("allowed program", "<Q", content + 0x800)]
    for name, fmt, offset in fields:
        actual = struct.unpack_from(fmt, package, offset)[0]
        if actual != expected:
            raise ValueError(f"CIA {name} ID {actual:016X} differs from Devil-3Ds {expected:016X}")
    if expected == 0x0004000003F39500:
        raise ValueError("Devil-3Ds must not reuse the legacy DevilutionX Title ID")
    print(f"Independent CIA identity verified: {expected:016X}")


def main():
    root = Path(__file__).resolve().parents[1]
    build = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build"
    rsf = (root / "Packaging/ctr/template.rsf").read_text()
    unique_id = int(re.search(r"UniqueId\s*:\s*(0x[0-9A-Fa-f]+)", rsf).group(1), 16)
    title_id = 0x0004000000000000 | (unique_id << 8)
    mods_cmake = (root / "CMake/Mods.cmake").read_text()
    match = re.search(r"set\(hellfire_mod\s+(.*?)\)", mods_cmake, re.S)
    if not match:
        raise SystemExit("Cannot read the declared Hellfire asset list")
    hellfire_files = match.group(1).split() + ["data/inv/objcurs2-widths.txt"]
    assets = [root / "mods/hf" / name for name in hellfire_files]
    assets += [root / "assets/cacert.pem", root / "assets/licenses/jsmn.txt"]
    assets += [root / "assets/data/ctr_bottom_ui.pal8"]
    assets += [root / f"assets/data/hud/button_{button}.rgba" for button in "xylr"]
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
        if name.endswith(".cia"):
            verify_cia_identity(package, title_id)
            verify_network_services(package)
            verify_cia_version(package, (root / "PORT_VERSION").read_text())
        print(f"{name}: verified {len(assets)} bundled assets ({len(package)} bytes)")


if __name__ == "__main__":
    main()
