#!/usr/bin/env python3
"""Conservative release gate for mcx_mac build artifacts.

This checks known blockers, not every copyright or license obligation. A pass
is not legal clearance. Keep the gate current when the build graph changes.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys


BLOCKED_OBJECTS = {
    "disc.c.obj": "uMac's Basilisk II-derived disc driver (GPLv2)",
    "rom.c.obj": "uMac ROM patcher contains GPLv2-derived driver bytes",
    "umac_private_images.S.obj": "embedded private ROM/System disk",
    "softfloat.c.obj": "SoftFloat 2b terms need review or removal",
}

BLOCKED_SOURCE_NAMES = {
    "disc.c": "uMac's Basilisk II-derived floppy driver",
    "rom.c": "legacy ROM patching path",
    "sonydrv.h": "Basilisk II-derived ROM driver",
    "sonydrv.S": "Basilisk II-derived ROM driver",
    "b2_macos_util.h": "Basilisk II-derived helper",
    "keymap.h": "Mini vMac-derived keymap",
    "keymap_sdl.h": "Mini vMac-derived keymap",
    "m68kmmu.h": "MAME-derived PMMU implementation",
    "m68kfpu.c": "unused FPU implementation requiring SoftFloat 2b",
}

BLOCKED_ASSET_SUFFIXES = {".rom", ".dsk", ".img", ".image", ".hex", ".bin", ".uf2"}


def inspect_source(source_dir: Path) -> list[str]:
    """Check that the proposed source tree has no known private/GPL payloads."""
    blockers = []
    for path in source_dir.rglob("*"):
        if not path.is_file() or any(
            part.startswith("build") or part in {".git", ".cache", "__pycache__"}
            for part in path.relative_to(source_dir).parts[:-1]
        ):
            continue
        reason = BLOCKED_SOURCE_NAMES.get(path.name)
        if reason:
            blockers.append(f"source contains {path.relative_to(source_dir)}: {reason}")
        if "softfloat" in (part.lower() for part in path.relative_to(source_dir).parts):
            blockers.append(f"source contains SoftFloat 2b material: {path.relative_to(source_dir)}")
        if path.suffix.lower() in BLOCKED_ASSET_SUFFIXES:
            blockers.append(f"source contains release asset: {path.relative_to(source_dir)}")
    return blockers


def inspect_history(source_dir: Path) -> list[str]:
    """Catch deleted blockers still reachable through a local Git ref."""
    if not (source_dir / ".git").exists():
        return []
    result = subprocess.run(
        ["git", "-C", str(source_dir), "rev-list", "--objects", "--all"],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        return ["cannot inspect Git history for blocked files"]
    blockers = []
    for line in result.stdout.splitlines():
        _, separator, name = line.partition(" ")
        if not separator:
            continue
        path = Path(name)
        if (path.name in BLOCKED_SOURCE_NAMES or
                path.suffix.lower() in BLOCKED_ASSET_SUFFIXES or
                "softfloat" in (part.lower() for part in path.parts)):
            blockers.append(f"Git history still exposes blocked file: {name}")
    return sorted(set(blockers))


def inspect_build(build_dir: Path) -> list[str]:
    blockers = []
    config = build_dir / "zephyr" / ".config"
    cache = build_dir / "CMakeCache.txt"
    link_map = build_dir / "zephyr" / "zephyr.map"

    for path in (config, cache, link_map):
        if not path.is_file():
            blockers.append(f"missing build evidence: {path}")
    if blockers:
        return blockers

    config_text = config.read_text(errors="replace")
    cache_text = cache.read_text(errors="replace")
    map_text = link_map.read_text(errors="replace")

    if "CONFIG_MCX_MAC_UMAC_BOOT=y" not in config_text:
        blockers.append("uMac boot is not enabled in this build")
    elif "CONFIG_MCX_MAC_UMAC_MEDIA_FROM_SLOT1=y" not in config_text:
        blockers.append("uMac boot still requires ROM/System disk at build time")
    if "CONFIG_MCX_MAC_INDEPENDENT_FLOPPY=y" not in config_text:
        blockers.append("independent floppy driver is not enabled")
    if "MCX_MAC_PUBLIC_RELEASE:BOOL=ON" not in cache_text:
        blockers.append("CMake public-release guard is not enabled")

    for variable in ("MCX_MAC_PRIVATE_ROM", "MCX_MAC_PRIVATE_DISK"):
        if re.search(rf"^{variable}:[^=]*=.+$", cache_text, re.MULTILINE):
            blockers.append(f"{variable} is set in CMake cache (value withheld)")

    for object_name, reason in BLOCKED_OBJECTS.items():
        if re.search(rf"(?:^|[/(:]){re.escape(object_name)}(?:\)|\s|$)", map_text,
                     re.MULTILINE):
            blockers.append(f"linked {object_name}: {reason}")

    binary = build_dir / "zephyr" / "zephyr.bin"
    if not binary.is_file():
        blockers.append("missing binary for image-1 boundary check")
    elif binary.stat().st_size > 0x10A000:
        blockers.append("firmware overlaps the separate image-1 media slot")

    return blockers


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="Zephyr build directory")
    args = parser.parse_args()
    blockers = inspect_source(Path(__file__).resolve().parents[1])
    blockers.extend(inspect_history(Path(__file__).resolve().parents[1]))
    blockers.extend(inspect_build(args.build_dir))
    if blockers:
        print("NOT RELEASE-READY; do not publish this build or its HEX/BIN:")
        for blocker in blockers:
            print(f"- {blocker}")
        return 1
    print("Known build blockers not found. Manual provenance and rights review still required.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
