#!/usr/bin/env python3
"""Create a conservative, build-specific compiled-source inventory.

This is a review aid, not an SPDX SBOM or a substitute for license analysis.
Compilation does not prove that an object was retained in the final ELF.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


SPDX = re.compile(r"SPDX-License-Identifier:\s*([^\s*]+)")


def relative_name(path: Path, project: Path) -> str:
    try:
        return "project/" + str(path.relative_to(project))
    except ValueError:
        parts = path.parts
        if "zephyr" in parts:
            index = parts.index("zephyr")
            return "workspace/" + "/".join(parts[index:])
        return "external/" + path.name


def collect(build_dir: Path, project: Path) -> dict:
    entries = json.loads((build_dir / "compile_commands.json").read_text())
    sources = []
    for name in sorted({entry["file"] for entry in entries}):
        path = Path(name).resolve()
        data = path.read_bytes()
        match = SPDX.search(data[:16384].decode("utf-8", errors="replace"))
        sources.append({
            "path": relative_name(path, project),
            "sha256": hashlib.sha256(data).hexdigest(),
            "spdx": match.group(1) if match else None,
        })
    counts = Counter(item["spdx"] or "manual-review" for item in sources)
    return {
        "description": "Compiled-source inventory; not a complete linked SBOM",
        "build": build_dir.name,
        "source_count": len(sources),
        "spdx_counts": dict(sorted(counts.items())),
        "sources": sources,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    output = build_dir / "source_inventory.json"
    inventory = collect(build_dir, Path(__file__).resolve().parents[1])
    output.write_text(json.dumps(inventory, indent=2) + "\n")
    print(f"{inventory['source_count']} compiled sources; "
          f"{inventory['spdx_counts'].get('manual-review', 0)} need manual "
          f"classification: {output}")


if __name__ == "__main__":
    main()
