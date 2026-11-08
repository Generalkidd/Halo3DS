#!/usr/bin/env python3
"""Compile the production texture-unit adapter with host command stubs."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='halo-texture-units-') as temporary:
        work = Path(temporary)
        # The fixture supplies the narrow platform surface before production
        # source inclusion. Never rewrite the real SDK or adapter for a test.
        (work / '3ds.h').write_text('/* host platform supplied by fixture */\n')
        (work / 'internal.h').write_text('/* host context supplied by fixture */\n')
        binary = work / 'texture-units'
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I', str(work),
                        str(root / 'port/n3ds/tests/host/texture_units_test.c'),
                        '-o', str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
