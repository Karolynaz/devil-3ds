#!/usr/bin/env python3
"""Verify main-thread stack budgets in the linked ARM ELF and both packages."""
import struct
import sys
from pathlib import Path

MINIMUM_STACK = 1024 * 1024


def elf_stack(data):
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Expected little-endian ELF32")
    offset = struct.unpack_from("<I", data, 32)[0]
    stride, count = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<10I", data, offset + i * stride) for i in range(count)]
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        table = data[strings[4]:strings[4] + strings[5]]
        for position in range(section[4], section[4] + section[5], section[9]):
            name, address, size, info, other, index = struct.unpack_from("<IIIBBH", data, position)
            if table[name:table.find(b"\0", name)] != b"__stacksize__":
                continue
            if info >> 4 != 1:  # STB_GLOBAL, not libctru's weak default
                raise ValueError("Main stack still uses the SDK weak default")
            owner = sections[index]
            value = struct.unpack_from("<I", data, owner[4] + address - owner[3])[0]
            return value, address
    raise ValueError("ELF has no __stacksize__ symbol")


def package_stacks(elf, cia, three_dsx, address):
    # 3DSX stores the three PT_LOAD segments after its relocation headers.
    header_size, relocation_size = struct.unpack_from("<HH", three_dsx, 4)
    code_size, rodata_size = struct.unpack_from("<II", three_dsx, 16)
    offset = struct.unpack_from("<I", elf, 28)[0]
    stride, count = struct.unpack_from("<HH", elf, 42)
    writable = [struct.unpack_from("<8I", elf, offset + i * stride) for i in range(count)]
    writable = [p for p in writable if p[0] == 1 and p[6] & 2]
    assert len(writable) == 1 and three_dsx[:4] == b"3DSX"
    position = header_size + 3 * relocation_size + code_size + rodata_size + address - writable[0][2]
    three_dsx_stack = struct.unpack_from("<I", three_dsx, position)[0]

    header, _, _, certificates, ticket, tmd = struct.unpack_from("<IHHIII", cia)
    align = lambda n: (n + 63) & ~63
    content = align(header) + align(certificates) + align(ticket) + align(tmd)
    assert cia[content + 0x100:content + 0x104] == b"NCCH"
    cia_stack = struct.unpack_from("<I", cia, content + 0x200 + 0x1C)[0]
    return three_dsx_stack, cia_stack


def verify(elf_path, cia_path, three_dsx_path):
    elf = Path(elf_path).read_bytes()
    stack, address = elf_stack(elf)
    if stack < MINIMUM_STACK:
        raise ValueError(f"ELF main stack too small: {stack} bytes")
    stacks = package_stacks(elf, Path(cia_path).read_bytes(), Path(three_dsx_path).read_bytes(), address)
    for name, value in zip(("3DSX", "CIA"), stacks):
        if value != stack:
            raise ValueError(f"{name} main stack {value} differs from ELF {stack}")
    print(f"ELF, 3DSX and CIA: verified {stack} byte main stacks")


if __name__ == "__main__":
    build = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build")
    verify(build / "devil-3ds.elf.elf", build / "devil-3ds.cia", build / "devil-3ds.3dsx")
