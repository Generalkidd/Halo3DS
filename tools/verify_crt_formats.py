#!/usr/bin/env python3
"""Verify production format borrowing and translation with ASan/UBSan."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='halo-crt-formats-') as directory:
        binary = Path(directory) / 'crt-formats'
        subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(root / 'port/n3ds/tests/host/crt_formats_test.c'),
                        '-o', str(binary)], check=True, timeout=45)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
