#!/usr/bin/env python3
"""Compare complete midpoint capture pairs at identical camera inputs."""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


VIEWS = ('midpoint', 'regular')
SIZES = {'rgba': 384000, 'd24s8': 384000, 'camera': 260}
MANIFEST = re.compile(r'halo-pair-([0-9a-f]{16})-([0-9]+)-complete\.json')


def load_pair(path):
    match = MANIFEST.fullmatch(path.name)
    if not match:
        raise ValueError('Supply a complete-pair manifest')
    capture_id, tick_text = match.groups()
    tick = int(tick_text)
    expected = dict(schema=1, capture_id=capture_id, tick=tick, width=240, height=400,
                    rgba_bytes=SIZES['rgba'], d24s8_bytes=SIZES['d24s8'],
                    camera_bytes=SIZES['camera'], views=list(VIEWS))
    with path.open('rb') as stream:
        receipt = stream.read(1025)
    if len(receipt) > 1024 or json.loads(receipt) != expected:
        raise ValueError('Invalid complete-pair manifest')
    payloads = {}
    for view in VIEWS:
        files = {}
        for extension, size in SIZES.items():
            name = f'halo-pair-{capture_id}-{tick_text}-{view}.{extension}'
            with path.with_name(name).open('rb') as stream:
                data = stream.read(size+1)
            if len(data) != size:
                raise ValueError(f'Unexpected payload size for {name}')
            files[extension] = data
        camera = files['camera']
        if camera[:8] != b'H2DSCAM1' or struct.unpack_from('<4I', camera, 8) != (tick, view == 'midpoint', 240, 400):
            raise ValueError(f'Camera record does not identify the {view} target')
        payloads[view] = files
    return tick, payloads


def compare(reference, candidate):
    ref_tick, before = load_pair(reference)
    cand_tick, after = load_pair(candidate)
    if ref_tick != cand_tick:
        raise ValueError('The pairs must have the same simulation tick')
    if any(before[view]['camera'] != after[view]['camera'] for view in VIEWS):
        raise ValueError('The pairs differ in camera, presentation time or target layout')
    report = {}
    for view in VIEWS:
        report[view] = {}
        for extension in ('rgba', 'd24s8'):
            first, second = before[view][extension], after[view][extension]
            report[view][extension] = {
                'reference_sha256': hashlib.sha256(first).hexdigest(),
                'candidate_sha256': hashlib.sha256(second).hexdigest(),
                'different_words': sum(a != b for a, b in zip(memoryview(first).cast('I'),
                                                              memoryview(second).cast('I'))),
            }
    return {'tick': ref_tick, 'same_camera_records': True, 'views': report,
            'exact_match': all(data['different_words'] == 0 for view in report.values() for data in view.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    try:
        result = compare(args.reference, args.candidate)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))
    return 0 if result['exact_match'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
