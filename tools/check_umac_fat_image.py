#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only preflight for a raw HFS/MFS image to be copied as umac0.img."""

import argparse
import hashlib
from pathlib import Path
from typing import Optional, Tuple

SECTOR_BYTES = 512
VOLUME_SIGNATURE_OFFSET = 2 * SECTOR_BYTES


def inspect_image(path: Path, max_bytes: Optional[int] = None) -> Tuple[int, str, str]:
    size = path.stat().st_size
    if size < VOLUME_SIGNATURE_OFFSET + 2 or size % SECTOR_BYTES:
        raise ValueError("image must contain whole 512-byte sectors and a volume header")
    if max_bytes is not None and size > max_bytes:
        raise ValueError(f"image is {size} bytes; volume allows only {max_bytes}")
    digest = hashlib.sha256()
    with path.open("rb") as image:
        image.seek(VOLUME_SIGNATURE_OFFSET)
        signature = image.read(2)
        if signature == b"BD":
            media_type = "HFS (writable)"
        elif signature == b"\xd2\xd7":
            media_type = "MFS (read-only)"
        else:
            raise ValueError("no raw HFS or MFS signature at sector 2")
        image.seek(0)
        for chunk in iter(lambda: image.read(1024 * 1024), b""):
            digest.update(chunk)
    return size // SECTOR_BYTES, digest.hexdigest(), media_type


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--max-bytes", type=int,
                        help="optional exact free-space limit of the FAT volume")
    args = parser.parse_args()
    try:
        sectors, checksum, media_type = inspect_image(args.image, args.max_bytes)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(f"Raw {media_type} image: {sectors} sectors "
          f"({sectors * SECTOR_BYTES} bytes)")
    print(f"SHA-256: {checksum}")


if __name__ == "__main__":
    main()
