#!/usr/bin/env python3
"""Exercise production texture mirrors with explicit asynchronous-DMA stubs."""
from pathlib import Path
import argparse
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='halo-texture-vram-') as directory:
        for name in ('texture_vram_test', 'texture_vram_probe_test'):
            binary = Path(directory) / name
            subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-I', str(root / 'port/n3ds/include'),
                            str(root / f'port/n3ds/tests/host/{name}.c'),
                            '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=30)

if __name__ == '__main__':
    main()
