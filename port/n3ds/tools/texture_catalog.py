#!/usr/bin/env python3
"""Emit the runtime texture archive catalogue header; no builds or validation runs.

From the repository root: python3 port/n3ds/tools/texture_catalog.py
Reads setup/Resources/catalog.json and replaces include/engine_texture_catalog.h.
Run explicitly when the expanded-map catalogue changes; never at runtime.
"""

import json
from pathlib import Path
import re


def main() -> None:
    platform = Path(__file__).resolve().parents[1]
    catalog = json.loads((platform / "setup/Resources/catalog.json").read_text(encoding="utf-8"))
    lines = [
        "#ifndef HALO_N3DS_ENGINE_TEXTURE_CATALOG_H",
        "#define HALO_N3DS_ENGINE_TEXTURE_CATALOG_H",
        "/* Compiled expanded-map identities from setup/Resources/catalog.json.",
        " * Regenerate: python3 port/n3ds/tools/texture_catalog.py (repository root).",
        " * Unknown maps keep legacy archives/original decoding; runtime does not",
        " * hash map files. */",
        "struct texture_archive_catalog_entry { const char *name; unsigned int bytes; unsigned char sha256[32]; };",
        "static const struct texture_archive_catalog_entry texture_archive_catalog[] = {",
    ]
    # Preserve source catalogue ordering. Identical source entries produce
    # identical UTF-8/LF output on every host; no timestamps or host paths.
    for name, entry in catalog.items():
        size, digest = entry["Bytes"], entry["Sha256"]
        if not re.fullmatch(r"[a-z0-9_]{1,31}", name):
            raise ValueError(f"Invalid runtime map name: {name!r}")
        if type(size) is not int or not 2048 <= size <= 0x7FFFFFFF:
            raise ValueError(f"Invalid expanded-map size for {name}")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
            raise ValueError(f"Invalid expanded-map SHA256 for {name}")
        values = ",".join(f"0x{value:02x}" for value in bytes.fromhex(digest))
        lines.append(f'    {{"{name}",{size}U,{{{values}}}}},')
    if not catalog:
        raise ValueError("The runtime map catalogue must not be empty")
    lines.extend(["};", "#endif", ""])
    destination = platform / "include/engine_texture_catalog.h"
    destination.write_bytes("\n".join(lines).encode("utf-8"))


if __name__ == "__main__":
    main()
