# New3DS integration additions by Kevin Boggs.
"""Fail validation if attribution comments on the integration code disappear."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
MANIFEST = Path(__file__).with_name("attribution-files.txt")
SYMBOLS = Path(__file__).with_name("attribution-symbols.txt")
MARKER = "New3DS integration additions by Kevin Boggs."


# Kevin Boggs: main - New3DS integration.
def main() -> int:
    missing: list[str] = []
    for rel in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not rel or rel.startswith("#"):
            continue
        source = ROOT / rel
        if not source.is_file() or MARKER not in "\n".join(source.read_text(encoding="utf-8-sig").splitlines()[:5]):
            missing.append(rel)
    for entry in SYMBOLS.read_text(encoding="utf-8").splitlines():
        if not entry or entry.startswith("#"):
            continue
        rel, name = entry.split("|", 1)
        source = ROOT / rel
        if not source.is_file():
            missing.append(f"{rel}: {name}")
            continue
        lines = source.read_text(encoding="utf-8-sig").splitlines()
        marker = f"Kevin Boggs: {name}"
        places = [i for i, line in enumerate(lines) if marker in line]
        if not any(re.search(rf"\b{re.escape(name)}\s*(?:\(|\b)", "\n".join(lines[i+1:i+16]))
                   for i in places):
            missing.append(f"{rel}: {name}")
    if missing:
        print("Kevin Boggs integration attribution missing from:", file=sys.stderr)
        for rel in missing:
            print(f"  {rel}", file=sys.stderr)
        return 1
    print(f"Attribution check passed: {len(MANIFEST.read_text(encoding='utf-8').splitlines())} files, "
          f"{len(SYMBOLS.read_text(encoding='utf-8').splitlines())} symbols")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
