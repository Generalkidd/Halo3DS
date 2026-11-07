#!/usr/bin/env python3
"""Read one bounded Halo dependency trace and emit a JSON integrity summary.

Source-only groundwork: this reader has not been executed or tested. It does
not replay gameplay, infer scanout/completion instants or calculate FPS.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import stat
import struct


SCHEMA = 1
HEADER_BYTES = 128
RECORD_BYTES = 64
FOOTER_BYTES = 16
CAPACITY = 8192
BUFFER_BYTES = 557792
MAX_FILE_BYTES = HEADER_BYTES + CAPACITY * RECORD_BYTES + FOOTER_BYTES
EXAMPLE_LIMIT = 32
EVENT_NAMES = (
    'LOOP_BEGIN', 'LOOP_END', 'TICK_BEGIN', 'TICK_END',
    'FRAME_UPDATE_BEGIN', 'FRAME_UPDATE_END', 'RESTORE', 'LOCATION',
    'ATTEMPT_BEGIN', 'ATTEMPT_END', 'VIEW_BEGIN', 'VIEW_END',
    'FRAME_SERVICES_BEGIN', 'FRAME_SERVICES_END',
    'THREAD_COLLECT_BEGIN', 'THREAD_COLLECT_END',
    'TEXTURE_SERVICE_BEGIN', 'TEXTURE_SERVICE_END',
    'FRUSTUM_BEGIN', 'FRUSTUM_END', 'VISIBILITY_BEGIN', 'VISIBILITY_END',
    'OBJECT_DISCOVERY_BEGIN', 'OBJECT_DISCOVERY_END',
    'OBJECT_RENDER_BEGIN', 'OBJECT_RENDER_END',
    'GPU_ACQUIRE_BEGIN', 'GPU_ACQUIRE_END', 'GPU_ACQUIRE_REJECT',
    'GPU_FRAME_END_BEGIN', 'GPU_FRAME_END_END',
    'GPU_TEXTURE_FENCE_BEGIN', 'GPU_TEXTURE_FENCE_END',
    'GPU_SWAP_WAIT_BEGIN', 'GPU_SWAP_WAIT_END', 'MIDPOINT_CANCEL',
    'QUEUE_RUN', 'QUEUE_COMPLETE', 'TOP_SWAP_POSTED', 'TOP_PENDING_CLEAR',
    'TOP_ACK_UNOBSERVED', 'VBLANK', 'WORKER_DISPATCH', 'WORKER_START',
    'WORKER_END', 'MAIN_WORK_BEGIN', 'MAIN_WORK_END', 'JOIN_BEGIN', 'JOIN_END',
    'SERIAL_BEGIN', 'SERIAL_END',
)
RECORD_FIELDS = (
    'timestamp', 'sequence', 'loop', 'epoch', 'tick', 'generation', 'bsp',
    'attempt', 'view', 'submission', 'job', 'event', 'kind', 'producer',
    'flags', 'arg0', 'arg1',
)
RECORD = struct.Struct('<QIIIiIiIIIIHBBIII')


def fnv1a32(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def read_trace(path):
    if not path.is_file():
        raise ValueError('trace must be a regular file')
    with path.open('rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ValueError('trace must be a regular file')
        if not HEADER_BYTES + FOOTER_BYTES <= info.st_size <= MAX_FILE_BYTES:
            raise ValueError('file size is outside the bounded schema-1 range')
        data = stream.read(MAX_FILE_BYTES + 1)
    if not HEADER_BYTES + FOOTER_BYTES <= len(data) <= MAX_FILE_BYTES:
        raise ValueError('truncated or oversized trace')

    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]

    def u64(offset):
        return struct.unpack_from('<Q', data, offset)[0]

    if data[:8] != b'H2DSTRC1' or u32(8) != SCHEMA:
        raise ValueError('unsupported trace magic or schema')
    if u32(12) != HEADER_BYTES or u32(16) != RECORD_BYTES or u32(20) != CAPACITY:
        raise ValueError('unsupported header/record size or event capacity')
    count = u32(24)
    if count > CAPACITY or len(data) != HEADER_BYTES + count * RECORD_BYTES + FOOTER_BYTES:
        raise ValueError('record count does not match bounded file length')
    footer = len(data) - FOOTER_BYTES
    if data[footer:footer + 8] != b'H2DEND01' or u32(footer + 8) != count:
        raise ValueError('missing or inconsistent final trailer')
    checksum = u32(footer + 12)
    if fnv1a32(memoryview(data)[:footer]) != checksum:
        raise ValueError('header/record FNV-1a checksum mismatch')
    header = {
        'schema': SCHEMA, 'record_count': count, 'event_capacity': CAPACITY,
        'capture_loop_limit': u32(28), 'run_id': f'{u64(32):016x}',
        'timer_frequency_hz': u64(40), 'capture_start_tick': u64(48),
        'capture_stop_tick': u64(56), 'owner_dropped': u32(64),
        'callback_dropped': u32(68), 'vblank_dropped': u32(72),
        'queue_identity_lost': u32(76), 'pending_queues_at_start': u32(80),
        'pending_queues_at_stop': u32(84), 'pending_top_sequence': u32(88),
        'stop_reason': u32(92), 'completed_capture_loops': u32(96),
        'posted_sequence_at_stop': u32(100), 'vblank_sequence_at_stop': u32(104),
        'buffer_bytes': u32(108), 'tail_unconfirmed': u32(112),
        'midpoint_build': u32(116), 'last_queue_id_at_export': u32(120),
        'last_job_id_at_export': u32(124),
    }
    if not 1 <= header['capture_loop_limit'] <= 18000:
        raise ValueError('invalid capture loop limit')
    if (header['timer_frequency_hz'] == 0 or u64(32) != header['capture_start_tick']
            or header['capture_stop_tick'] < header['capture_start_tick']):
        raise ValueError('invalid run clock metadata')
    if (header['buffer_bytes'] != BUFFER_BYTES or header['tail_unconfirmed'] != 1
            or header['queue_identity_lost'] not in (0, 1)
            or header['midpoint_build'] not in (0, 1)
            or header['stop_reason'] not in (1, 2)):
        raise ValueError('unsupported schema-1 storage or capture flags')
    if (header['completed_capture_loops'] > header['capture_loop_limit']
            or (header['stop_reason'] == 1
                and header['completed_capture_loops'] != header['capture_loop_limit'])):
        raise ValueError('inconsistent completed-loop count')
    records = []
    for offset in range(HEADER_BYTES, footer, RECORD_BYTES):
        record = dict(zip(RECORD_FIELDS, RECORD.unpack_from(data, offset)))
        if not 1 <= record['event'] <= len(EVENT_NAMES):
            raise ValueError('unsupported schema-1 event ID')
        if record['producer'] > 3 or record['kind'] > 2 or record['flags']:
            raise ValueError('unsupported producer, render kind or event flags')
        records.append(record)
    return data, header, records, checksum


def limited_ids(values):
    values = sorted(values)
    return {'count': len(values), 'first_ids': values[:EXAMPLE_LIMIT],
            'examples_truncated': len(values) > EXAMPLE_LIMIT}


def summarize(path, data, header, records, checksum):
    events = defaultdict(list)
    sequences = defaultdict(list)
    by_name = defaultdict(list)
    for record in records:
        name = EVENT_NAMES[record['event'] - 1]
        events[name].append(record['timestamp'])
        by_name[name].append(record)
        # Joined-job events share the owner's sequence allocator. GPU and
        # VBlank callbacks each have an independent sequence allocator.
        stream = {0: 'owner_and_joined_job', 1: 'gpu_callback',
                  2: 'vblank_callback', 3: 'owner_and_joined_job'}[record['producer']]
        sequences[stream].append(record['sequence'])
    sequence_summary = {}
    for stream, values in sequences.items():
        unique = set(values)
        sequence_summary[stream] = {
            'first_observed': min(unique), 'last_observed': max(unique),
            'missing_within_observed_range': max(unique) - min(unique) + 1 - len(unique),
            'duplicate_observations': len(values) - len(unique),
        }
    runs = Counter(r['submission'] for r in by_name['QUEUE_RUN'])
    completions = Counter(r['submission'] for r in by_name['QUEUE_COMPLETE'])
    posts = Counter((r['submission'], r['arg0']) for r in by_name['TOP_SWAP_POSTED'])
    clears = Counter((r['submission'], r['arg0']) for r in by_name['TOP_PENDING_CLEAR'])
    unobserved = Counter((r['submission'], r['arg0']) for r in by_name['TOP_ACK_UNOBSERVED'])
    missing_complete = runs.keys() - completions.keys()
    missing_run = completions.keys() - runs.keys()
    missing_ack = posts.keys() - clears.keys() - unobserved.keys()
    warnings = [
        'GPU/scanout tail is always unconfirmed in schema 1, even with zero pending counters.',
        'Timestamps describe observations; callback execution and sampled acknowledgement are not exact hardware completion or scanout instants.',
        'Journal order is retention order, not timestamp order; joined jobs and callback drains can append earlier timestamps.',
        'Capture boundaries can leave unmatched events; no missing observation is treated as zero work or zero stalls.',
        'Successful integrity checks do not validate instrumentation, rendering, gameplay or timing accuracy.',
    ]
    if any(header[name] for name in ('owner_dropped', 'callback_dropped', 'vblank_dropped')):
        warnings.append('Dropped observations make retained coverage incomplete.')
    if header['queue_identity_lost']:
        warnings.append('Queue identity was lost; subsequent GPU association is disabled rather than guessed.')
    if header['pending_queues_at_start']:
        warnings.append('Pre-window queues were pending when capture started; their missing run records are not a zero-cost baseline.')
    if len(records) == CAPACITY:
        warnings.append('The retained journal reached capacity; a longer capture limit cannot extend this retained event history.')
    return {
        'file': str(path), 'file_bytes': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
        'integrity': {'status': 'valid_schema1_structure_and_checksum',
                      'fnv1a32_header_and_records': f'{checksum:08x}'},
        'header': header,
        'timing': {'unit': 'raw svcGetSystemTick ticks',
                   'capture_interval_ticks': header['capture_stop_tick'] - header['capture_start_tick'],
                   'interpretation': 'collection interval only; no FPS or completion/scanout instant inferred'},
        'events': {name: {'count': len(values), 'first_observed_tick': min(values),
                          'last_observed_tick': max(values)}
                   for name, values in sorted(events.items())},
        'sequences': sequence_summary,
        'association_gaps': {
            'queue_runs_without_retained_completion': limited_ids(missing_complete),
            'completions_without_retained_queue_run': limited_ids(missing_run),
            'duplicate_queue_run_records': sum(max(0, n - 1) for n in runs.values()),
            'duplicate_queue_completion_records': sum(max(0, n - 1) for n in completions.values()),
            'posts_without_retained_clear_or_unobserved_marker': limited_ids(missing_ack),
            'clears_without_retained_post': limited_ids(clears.keys() - posts.keys()),
            'superseded_unobserved_swap_records': sum(unobserved.values()),
            'pairing_scope': 'queue and top-swap identity only; CPU stage/job pairing is not implemented',
        },
        'warnings': warnings,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='one local halo-trace-*.bin file')
    args = parser.parse_args()
    try:
        data, header, records, checksum = read_trace(args.trace)
        summary = summarize(args.trace, data, header, records, checksum)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(2, f'dependency trace rejected: {error}\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
