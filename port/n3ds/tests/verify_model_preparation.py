#!/usr/bin/env python3
"""Run the production model layout, compaction and midpoint scratch code with host sanitizers."""

import argparse
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='halo-model-preparation-') as directory:
        executable = Path(directory) / 'verify-model-preparation'
        for mode, layout, geometry in ((1, 0, 0), (1, 1, 0), (2, 2, 0), (0, 1, 1), (2, 2, 1)):
            subprocess.run([
                args.clang, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                f'-DHALO_N3DS_ALGO_MODEL_COMPACT={mode}', f'-DHALO_N3DS_VERTEX_LAYOUT={layout}',
                f'-DHALO_N3DS_GEOMETRY_SOURCE={geometry}',
                '-I', str(tests.parent / 'include'),
                str(tests / 'host_model_preparation.c'), '-o', str(executable),
            ], check=True, timeout=45)
            subprocess.run([str(executable)], cwd=directory, check=True, timeout=45)


if __name__ == '__main__':
    main()
