#!/usr/bin/env python3
"""Run the texture comparison fixture sequentially in private Azahar profiles."""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics
import struct

import emulator_smoke


MAPS = ('ui', 'a10')
CAPTURE_PIXELS = 400 * 240
FIXTURE_LOOPS = 240
ARCHIVE_MODES = {'raw': 1, 'lz4': 2, 'packed': 3}
ARCHIVE_COUNTERS = ('raw', 'lz4', 'rgba8', 'rgb8', 'la8', 'stored_bytes', 'resident_bytes')
TIMING_GROUPS = ('pre_campaign', 'campaign', 'steady_campaign')


def capture_manifest(sd):
    captures = {}
    for stage, name in enumerate(('menu', 'campaign')):
        path = sd / f'halo-texture-compare-{stage}-target.rgba'
        size = path.stat().st_size
        if size != CAPTURE_PIXELS * 4:
            raise ValueError(f'Wrong target size for {path.name}: {size}')
        captures[name] = {'path': str(path), 'sha256': emulator_smoke.sha(path), 'bytes': size}
    if captures['menu']['sha256'] == captures['campaign']['sha256']:
        raise ValueError('Menu and campaign targets are identical')
    return captures


def compare_captures(reference, candidate):
    result = {}
    for name, expected in reference.items():
        actual = candidate[name]
        if expected['sha256'] == actual['sha256']:
            result[name] = {'different_pixels': 0, 'different_bytes': 0, 'maximum_byte_error': 0}
            continue
        before = Path(expected['path']).read_bytes()
        after = Path(actual['path']).read_bytes()
        different_pixels = sum(a != b for a, b in zip(struct.iter_unpack('<I', before),
                                                       struct.iter_unpack('<I', after)))
        different_bytes = sum(a != b for a, b in zip(before, after))
        result[name] = {'different_pixels': different_pixels, 'different_bytes': different_bytes,
                        'maximum_byte_error': max(abs(a-b) for a, b in zip(before, after))}
    return result


def archive_evidence(text, expected_mode):
    records = {}
    for line in text.splitlines():
        marker = 'TEXTURE COMPARE: '
        if marker not in line:
            continue
        fields = dict(word.split('=', 1) for word in line.split(marker, 1)[1].split())
        name = fields.pop('map')
        if name not in MAPS or name in records or set(fields) != {'mode', *ARCHIVE_COUNTERS}:
            raise ValueError(f'Unexpected texture archive counters for {name}')
        records[name] = {key: int(value) for key, value in fields.items()}
    if expected_mode is None:
        if records:
            raise ValueError('Legacy case reported candidate archive publications')
        summaries = re.findall(r'TEXTURE ARCHIVE: hits=\d+ legacy_hits=(\d+)', text)
        if len([count for count in summaries if int(count) > 0]) < len(MAPS):
            raise ValueError('Legacy archive did not supply both fixture stages')
        return {'legacy_publications': [int(count) for count in summaries]}
    if 'TEXTURE CANDIDATE: rejected;' in text or 'TEXTURE CANDIDATE: payload rejected;' in text:
        raise ValueError('Candidate archive rejected during fixture')
    for name in MAPS:
        row = records.get(name)
        if not row or row['mode'] != expected_mode:
            raise ValueError(f'Expected candidate mode {expected_mode} on {name}')
        publications = row['raw'] + row['lz4']
        if any(value < 0 for value in row.values()) or not publications or \
           row['rgba8'] + row['rgb8'] + row['la8'] != publications or \
           not 0 < row['stored_bytes'] <= row['resident_bytes']:
            raise ValueError(f'Invalid or empty candidate publications on {name}')
        if expected_mode != ARCHIVE_MODES['lz4'] and row['lz4']:
            raise ValueError(f'Unexpected compressed candidate publications on {name}')
        if expected_mode != ARCHIVE_MODES['packed'] and (row['rgb8'] or row['la8']):
            raise ValueError(f'Unexpected compact candidate publications on {name}')
    if expected_mode == ARCHIVE_MODES['lz4'] and not any(row['lz4'] for row in records.values()):
        raise ValueError('LZ4 mode never published a decoded texture')
    if expected_mode == ARCHIVE_MODES['packed'] and not (records['a10']['rgb8'] or records['a10']['la8']):
        raise ValueError('Packed mode never published a compact campaign texture')
    return records


def percentiles(samples):
    samples = sorted(samples)
    return {'p50': statistics.median(samples), 'p95': samples[math.ceil(.95*len(samples))-1],
            'p99': samples[math.ceil(.99*len(samples))-1], 'maximum': samples[-1]}


def timing_manifest(sd, text):
    entry = re.findall(r'TEXTURE COMPARE ENTRY: elapsed_us=(\d+)\b', text)
    if len(entry) != 1 or not int(entry[0]):
        raise ValueError('Missing or duplicate campaign-entry timing')
    path = sd / 'halo-texture-compare.csv'
    columns = ['loop', 'campaign', 'tick', 'work_us', 'interval_us']
    with path.open(newline='') as source:
        rows = csv.reader(source)
        if next(rows, None) != columns:
            raise ValueError('Unexpected timing CSV columns')
        samples = [tuple(map(int, row)) for row in rows]
    if len(samples) != FIXTURE_LOOPS or any(len(row) != len(columns) for row in samples):
        raise ValueError(f'Expected {FIXTURE_LOOPS} timing samples')
    start = samples[0][0]
    groups = {'pre_campaign': [], 'campaign': []}
    for index, (loop, campaign, _, work, interval) in enumerate(samples):
        if loop != start + index or campaign not in (0, 1) or not 0 <= work <= interval:
            raise ValueError(f'Invalid timing row at loop {loop}')
        groups['campaign' if campaign else 'pre_campaign'].append((work, interval))
    if not groups['pre_campaign'] or len(groups['campaign']) < 2:
        raise ValueError('Timing run lacks menu/loading or steady campaign samples')
    groups['steady_campaign'] = groups['campaign'][1:]
    result = {'path': str(path), 'sha256': emulator_smoke.sha(path), 'entry_us': int(entry[0])}
    for group, intervals in groups.items():
        result[group] = {'samples': len(intervals),
            'work_us': percentiles(work for work, _ in intervals),
            'interval_us': percentiles(interval for _, interval in intervals),
            'interval_ge_50ms': sum(interval >= 50_000 for _, interval in intervals),
            'interval_ge_100ms': sum(interval >= 100_000 for _, interval in intervals)}
    return result


def compare_timings(reference, candidate):
    result = {'entry_us': candidate['entry_us'] - reference['entry_us']}
    for group in TIMING_GROUPS:
        before, after = reference[group], candidate[group]
        result[group] = {metric: {stat: after[metric][stat] - value
                                  for stat, value in before[metric].items()}
                         for metric in ('work_us', 'interval_us')}
        for count in ('interval_ge_50ms', 'interval_ge_100ms'):
            result[group][count] = after[count] - before[count]
    return result


def timing_rounds(cases):
    """Pair every complete consecutive raw/LZ4/packed round."""
    if len(cases) % len(ARCHIVE_MODES):
        return []
    rounds = []
    for start in range(0, len(cases), len(ARCHIVE_MODES)):
        group = cases[start:start+len(ARCHIVE_MODES)]
        by_mode = {case['archive_mode']: case for case in group}
        if set(by_mode) != set(ARCHIVE_MODES.values()):
            return []
        reference = by_mode[ARCHIVE_MODES['raw']]['timing']
        rounds.append({'order': [case['label'] for case in group],
            'delta_to_raw_us': {label: compare_timings(reference, by_mode[mode]['timing'])
                                for label, mode in ARCHIVE_MODES.items() if label != 'raw'}})
    return rounds


def run_case(label, binary, archive_dir, expected_mode, args, case_timeout):
    out = args.output / label
    command = ['--emulator', str(args.emulator), '--binary', str(binary),
        '--assets', str(args.assets), '--dsp', str(args.dsp), '--output', str(out),
        '--renderer', args.renderer, '--map', 'a10', '--timeout', str(case_timeout),
        '--require-log', 'TEXTURE COMPARE FIXTURE: pass']
    if args.phase == 'capture':
        command.extend(('--require-log', 'PASS: texture comparison native sampling'))
    if args.legacy_dir:
        for name in MAPS:
            command.extend(('--archive', str(args.legacy_dir / f'{name}.ntx')))
    if archive_dir:
        for name in MAPS:
            command.extend(('--archive', str(archive_dir / f'{name}.ntx4')))
    result = {'label': label, 'profile': str(out), 'timeout_seconds': case_timeout,
              'archive_mode': expected_mode}
    try:
        status = emulator_smoke.main(command)
        run = json.loads((out / 'result.json').read_text())
        result.update(run_status=run['status'], renderer=run['renderer_observed'],
            cleanup_complete=run['cleanup_complete'], assets=run['assets'], binary_sha256=run['binary_sha256'])
        if status or run['status'] != 'pass' or not run['cleanup_complete']:
            raise ValueError(f'Azahar run did not complete: {run["status"]}')
        if run['renderer_observed'] != args.renderer:
            raise ValueError(f'Unverified or unexpected Azahar renderer: {run["renderer_observed"]}')
        sd = out / 'data/azahar-emu/sdmc'
        log = sd / 'halo-source-engine.log'
        text = log.read_text(errors='replace')
        result['log'] = str(log)
        result['archive_logs'] = [line for line in text.splitlines() if 'TEXTURE ARCHIVE:' in line or
                                  'TEXTURE CANDIDATE:' in line or 'TEXTURE COMPARE:' in line]
        result['archive_publications'] = archive_evidence(text, expected_mode)
        if args.phase == 'capture':
            ticks = re.findall(r'TEXTURE COMPARE CAPTURE: a10 tick=(\d+)\b', text)
            if len(ticks) != 1:
                raise ValueError('Missing or duplicate campaign capture tick')
            result['campaign_capture_tick'] = int(ticks[0])
            result['captures'] = capture_manifest(sd)
            if (sd / 'halo-texture-compare.csv').exists():
                raise ValueError('Capture fixture produced a timing CSV')
        else:
            if list(sd.glob('halo-texture-compare-*-target.rgba')):
                raise ValueError('Timing fixture produced a readback')
            result['timing'] = timing_manifest(sd, text)
        result['status'] = 'pass'
    except (OSError, KeyError, ValueError, SystemExit) as error:
        result.update(status='fail', error=str(error))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('emulator', 'assets', 'dsp', 'output'):
        parser.add_argument(f'--{name}', type=Path, required=True)
    parser.add_argument('--phase', choices=('capture', 'timing'), required=True)
    parser.add_argument('--renderer', choices=('vulkan', 'opengl', 'software'), default='vulkan')
    parser.add_argument('--legacy-dir', type=Path, help='Private directory with ui.ntx and a10.ntx overrides')
    parser.add_argument('--case', nargs=3, action='append', required=True,
        metavar=('LABEL', 'BINARY', 'ARCHIVE_DIR'),
        help='Repeat in run order. Use - for the legacy-only case.')
    parser.add_argument('--expect-mode', nargs=2, action='append', default=[], metavar=('LABEL', 'MODE'),
        help='Required for each candidate case. MODE is raw, lz4 or packed.')
    parser.add_argument('--timeout', type=emulator_smoke.timeout_seconds, default=180)
    parser.add_argument('--case-timeout', nargs=2, action='append', default=[], metavar=('LABEL', 'SECONDS'))
    args = parser.parse_args()
    args.output = args.output.resolve()
    labels = [case[0] for case in args.case]
    if len(set(labels)) != len(labels) or any(not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_-]*', label)
                                             for label in labels):
        parser.error('--case labels must be unique and contain only letters, digits, _ or -')
    timeouts = {}
    for label, value in args.case_timeout:
        if label not in labels or label in timeouts:
            parser.error(f'Unknown or duplicate --case-timeout label: {label}')
        try:
            timeouts[label] = emulator_smoke.timeout_seconds(value)
        except (ValueError, argparse.ArgumentTypeError) as error:
            parser.error(str(error))
    modes = {}
    for label, mode in args.expect_mode:
        if label not in labels or label in modes or mode not in ARCHIVE_MODES:
            parser.error(f'Invalid or duplicate --expect-mode: {label} {mode}')
        modes[label] = ARCHIVE_MODES[mode]
    if args.output.exists():
        parser.error('Use a fresh output directory')
    if args.legacy_dir:
        for name in MAPS:
            if not (args.legacy_dir / f'{name}.ntx').is_file():
                parser.error(f'Missing legacy archive: {name}.ntx')
    cases = []
    for label, binary, folder in args.case:
        binary = Path(binary)
        directory = None if folder == '-' else Path(folder)
        if not binary.is_file():
            parser.error(f'Missing case binary: {binary}')
        if bool(directory) != (label in modes):
            parser.error(f'--expect-mode is required for candidate cases only: {label}')
        if directory:
            for name in MAPS:
                if not (directory / f'{name}.ntx4').is_file():
                    parser.error(f'Missing candidate archive for {label}: {name}.ntx4')
        cases.append((label, binary, directory))
    args.output.mkdir(parents=True)
    receipt = {'scope': 'Azahar correctness and host-emulator timings only', 'phase': args.phase,
               'status': 'running', 'cases': []}
    receipt_path = args.output / 'comparison.json'
    for label, binary, directory in cases:
        result = run_case(label, binary, directory, modes.get(label), args, timeouts.get(label, args.timeout))
        if result['status'] == 'pass':
            reference = receipt['cases'][0] if receipt['cases'] else result
            if reference is result:
                receipt['reference'] = label
            if args.phase == 'capture':
                result['pixel_diffs'] = compare_captures(reference['captures'], result['captures'])
                if result['campaign_capture_tick'] != reference['campaign_capture_tick']:
                    result.update(status='fail', error='Campaign capture tick differs from reference')
                elif any(image['different_pixels'] for image in result['pixel_diffs'].values()):
                    result.update(status='fail', error='Target RGBA differs from reference')
            else:
                result['timing_delta_us'] = compare_timings(reference['timing'], result['timing'])
        receipt['cases'].append(result)
        receipt_path.write_text(json.dumps(receipt, indent=2)+'\n')
        if result['status'] != 'pass':
            break
    receipt['status'] = ('pass' if len(receipt['cases']) == len(cases) and
        all(case['status'] == 'pass' for case in receipt['cases']) else 'fail')
    if args.phase == 'timing' and receipt['status'] == 'pass':
        receipt['paired_rounds'] = timing_rounds(receipt['cases'])
    receipt_path.write_text(json.dumps(receipt, indent=2)+'\n')
    print(receipt_path)
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
