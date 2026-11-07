#!/usr/bin/env python3
"""Run the production BSP packet comparator and queue against a host fixture."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='halo-bsp-batch-') as directory:
        executable = Path(directory) / 'verify-bsp-batch'
        for mode in (0, 1):
            subprocess.run([
                args.clang, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                f'-DHALO_N3DS_ALGO_BSP_RESOLVED_BATCH={mode}', '-I', str(tests.parent / 'include'),
                str(tests / 'host/bsp_resolved_batch_test.c'), '-o', str(executable),
            ], check=True, timeout=45)
            subprocess.run([str(executable)], check=True, timeout=45)


if __name__ == '__main__':
    main()
