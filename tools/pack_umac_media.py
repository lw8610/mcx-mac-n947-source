#!/usr/bin/env python3
"""Create a PRIVATE, separately flashed media package from user-owned files.

Never upload the output, ROM, disk, or a firmware image embedding them.
This tool only creates a file; it does not write to the board.
"""

import argparse
from pathlib import Path
import struct
import zlib

from patch_umac_rom_independent import SONY_OFFSET, make_driver

SLOT_BYTES = 0xF6000
ROM_OFFSET = 0x2000
DISK_OFFSET = 0x22000
HEADER_BYTES = 64


def build_package(rom: bytes, disk: bytes) -> bytes:
    if len(rom) != 0x20000 or rom[:4] != bytes.fromhex("4d1f8172"):
        raise ValueError("expected a 128 KiB Mac Plus V3 ROM")
    valid_driver = any(
        rom[SONY_OFFSET:SONY_OFFSET + len(driver)] == driver
        for driver in (make_driver(), make_driver(True))
    )
    if rom[0xD92:0xD94] != bytes.fromhex("b381") or not valid_driver:
        raise ValueError("ROM must first be patched with the independent driver")
    if len(disk) not in (409600, 819200) or disk[:2] != b"LK":
        raise ValueError("expected a 400/800 KiB raw Macintosh disk image")
    image = bytearray(b"\xff" * SLOT_BYTES)
    image[ROM_OFFSET:ROM_OFFSET + len(rom)] = rom
    image[DISK_OFFSET:DISK_OFFSET + len(disk)] = disk
    struct.pack_into("<8sIIIIIIII", image, 0, b"MCXMAC01", 1, HEADER_BYTES,
                     ROM_OFFSET, len(rom), zlib.crc32(rom), DISK_OFFSET,
                     len(disk), zlib.crc32(disk))
    struct.pack_into("<I", image, 40, zlib.crc32(image[:40]))
    return bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("disk", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists; refusing to overwrite")
    package = build_package(args.rom.read_bytes(), args.disk.read_bytes())
    with args.output.open("xb") as target:
        target.write(package)
    print(f"Private media package created ({len(package)} bytes); do not distribute")


if __name__ == "__main__":
    main()
