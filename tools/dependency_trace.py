#!/usr/bin/env python3
"""Read one bounded Halo dependency trace and summarize retained observations."""

import argparse
from bisect import bisect_left, bisect_right
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
VIEW_CONTEXT = ('loop', 'epoch', 'tick', 'generation', 'bsp', 'attempt', 'view', 'kind')
ATTEMPT_CONTEXT = ('loop', 'epoch', 'tick', 'generation', 'bsp', 'attempt', 'kind')
JOB_CONTEXT = VIEW_CONTEXT + ('submission', 'job', 'arg0')
QUEUE_CONTEXT = VIEW_CONTEXT + ('submission', 'job')
STAGE_SPANS = {
    'LOOP': ('loop',),
    'TICK': ('loop', 'epoch', 'tick'),
    'FRAME_UPDATE': ('loop', 'epoch', 'tick'),
    'FRAME_SERVICES': ('loop',),
    'ATTEMPT': ATTEMPT_CONTEXT,
    'VIEW': VIEW_CONTEXT,
    'THREAD_COLLECT': VIEW_CONTEXT,
    'TEXTURE_SERVICE': VIEW_CONTEXT,
    'FRUSTUM': VIEW_CONTEXT,
    'VISIBILITY': VIEW_CONTEXT,
    'OBJECT_DISCOVERY': VIEW_CONTEXT,
    'OBJECT_RENDER': VIEW_CONTEXT + ('arg0',),
    'GPU_ACQUIRE': VIEW_CONTEXT + ('arg0',),
    'GPU_FRAME_END': VIEW_CONTEXT + ('arg0',),
    'GPU_TEXTURE_FENCE': VIEW_CONTEXT,
    'GPU_SWAP_WAIT': VIEW_CONTEXT + ('arg0',),
}
JOB_SPANS = {
    'worker_wake': ('WORKER_DISPATCH', 'WORKER_START', JOB_CONTEXT),
    'worker': ('WORKER_START', 'WORKER_END', JOB_CONTEXT + ('arg1',)),
    'owner_work': ('MAIN_WORK_BEGIN', 'MAIN_WORK_END', JOB_CONTEXT + ('arg1',)),
    'join': ('JOIN_BEGIN', 'JOIN_END', JOB_CONTEXT + ('arg1',)),
    'whole_batch': ('WORKER_DISPATCH', 'JOIN_END', JOB_CONTEXT),
    'serial': ('SERIAL_BEGIN', 'SERIAL_END', JOB_CONTEXT + ('arg1',)),
}
KIND_NAMES = ('frontend', 'regular', 'midpoint')


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
        if not header['capture_start_tick'] <= record['timestamp'] <= header['capture_stop_tick']:
            raise ValueError('event timestamp lies outside the capture interval')
        records.append(record)
    return data, header, records, checksum


def limited_ids(values):
    values = sorted(values)
    return {'count': len(values), 'first_ids': values[:EXAMPLE_LIMIT],
            'examples_truncated': len(values) > EXAMPLE_LIMIT}


def distribution(values, frequency):
    """Nearest-rank quantiles on available pairs, keeping the entire tail."""
    ordered = sorted(values)
    count = len(ordered)
    if not count:
        return {'samples': 0}
    ticks = {'min': ordered[0], 'max': ordered[-1]}
    for percent in (50, 95, 99):
        ticks[f'p{percent}'] = ordered[(count * percent + 99) // 100 - 1]
    return {'samples': count, 'ticks': ticks,
            'ms': {label: value * 1000 / frequency for label, value in ticks.items()}}


def context(record, fields):
    return tuple(record[name] for name in fields)


def paired_spans(by_name, begin, end_names, fields, producer, owner_sequence, owner_duplicates):
    """Pair repeated scopes in owner sequence, with timestamps used only for duration."""
    family = [(r, False) for r in by_name[begin]]
    family.extend((r, True) for name in end_names for r in by_name[name])
    family.sort(key=lambda item: item[0]['sequence'])
    gaps = Counter()
    starts = defaultdict(list)
    pairs = []
    for record, is_end in family:
        if record['producer'] != producer or (producer == 3 and not record['job']):
            gaps['unexpected_producer_or_missing_job'] += 1
            continue
        key = context(record, fields)
        if not is_end:
            starts[key].append(record)
            continue
        pending = starts.pop(key, [])
        if not pending:
            gaps['ends_without_begin'] += 1
            continue
        if len(pending) != 1:
            gaps['ambiguous_endpoints'] += len(pending) + 1
            continue
        first = pending[0]
        lo, hi = first['sequence'], record['sequence']
        observed = bisect_right(owner_sequence, hi) - bisect_left(owner_sequence, lo)
        duplicate = bisect_right(owner_duplicates, hi) != bisect_left(owner_duplicates, lo)
        if lo == hi or duplicate or observed != hi - lo + 1:
            gaps['sequence_gap_or_duplicate'] += 1
        elif record['timestamp'] < first['timestamp']:
            gaps['end_before_begin'] += 1
        else:
            pairs.append((first, record))
    missing = sum(map(len, starts.values()))
    if missing:
        gaps['begins_without_end'] = missing
    return pairs, dict(gaps)


def paired_identities(first, last, identity, fields, producers, invalid=()):
    """Pair only unique endpoints carrying the same captured context."""
    begins, ends = defaultdict(list), defaultdict(list)
    for record in first:
        begins[identity(record)].append(record)
    for record in last:
        ends[identity(record)].append(record)
    pairs, excluded = [], Counter()
    for key in begins.keys() & ends.keys():
        a, b = begins[key], ends[key]
        if len(a) != 1 or len(b) != 1 or key in invalid:
            excluded['ambiguous_or_conflicting_identity'] += 1
            continue
        first_record, last_record = a[0], b[0]
        if (not first_record['submission'] or context(first_record, fields) != context(last_record, fields)
                or (first_record['producer'], last_record['producer']) != producers):
            excluded['identity_context_mismatch'] += 1
        elif first_record['timestamp'] > last_record['timestamp']:
            excluded['end_before_begin'] += 1
        else:
            pairs.append((first_record, last_record))
    return pairs, dict(excluded)


def pair_distributions(pairs, frequency, group):
    times = defaultdict(list)
    for first, last in pairs:
        times[group(first, last)].append(last['timestamp'] - first['timestamp'])
    return {name: distribution(values, frequency) for name, values in sorted(times.items())}


def stage_timings(by_name, owner_records, frequency):
    counts = Counter(owner_records)
    owner_sequence = sorted(counts)
    owner_duplicates = sorted(sequence for sequence, count in counts.items() if count > 1)
    stages, workers, stage_pairs = {}, {}, {}
    for name, fields in STAGE_SPANS.items():
        ends = (f'{name}_END',)
        if name == 'GPU_ACQUIRE':
            ends += ('GPU_ACQUIRE_REJECT',)
        pairs, gaps = paired_spans(by_name, f'{name}_BEGIN', ends, fields,
                                   0, owner_sequence, owner_duplicates)
        stage_pairs[name] = pairs
        stages[name] = {
            'by_kind': pair_distributions(pairs, frequency, lambda a, b: KIND_NAMES[a['kind']]
                                         + ('_rejected' if EVENT_NAMES[b['event'] - 1] == 'GPU_ACQUIRE_REJECT' else '')),
            'unpaired': gaps,
        }
    for name, (begin, end, fields) in JOB_SPANS.items():
        pairs, gaps = paired_spans(by_name, begin, (end,), fields, 3, owner_sequence, owner_duplicates)
        workers[name] = {
            'by_kind_and_category': pair_distributions(
                pairs, frequency, lambda a, b: f"{KIND_NAMES[a['kind']]}:{a['arg0']}"),
            'unpaired': gaps,
        }
    return stages, workers, stage_pairs


def presentation_observations(by_name, frequency):
    posts = by_name['TOP_SWAP_POSTED']
    clears = by_name['TOP_PENDING_CLEAR']
    post_sequences = Counter(record['arg0'] for record in posts)
    clear_sequences = Counter(record['arg0'] for record in clears)
    swap_key = lambda record: (record['submission'], record['arg0'])
    conflicts = {swap_key(record) for record in by_name['TOP_ACK_UNOBSERVED']}
    conflicts.update(swap_key(record) for record in posts
                     if post_sequences[record['arg0']] > 1 or clear_sequences[record['arg0']] > 1)
    acknowledgements, pairing_gaps = paired_identities(
        posts, clears, swap_key, QUEUE_CONTEXT, (1, 0), conflicts)
    acknowledgements.sort(key=lambda pair: pair[1]['sequence'])

    # Require consecutive swap sequences and clear observations before
    # describing a gap. Any additional VBlank statement requires the locally
    # retained callback timestamps and uninterrupted observed labels as well.
    blanks = defaultdict(list)
    for record in by_name['VBLANK']:
        if record['producer'] == 2:
            blanks[record['arg0']].append(record['timestamp'])
    ack_gaps = []
    bracketed_runs = []
    gap_exclusions = Counter()
    for (prev_post, prev_clear), (post, clear) in zip(acknowledgements, acknowledgements[1:]):
        if ((post['arg0'] - prev_post['arg0']) & 0xffffffff) != 1:
            gap_exclusions['nonconsecutive_swap_sequence'] += 1
            continue
        if not prev_clear['timestamp'] <= post['timestamp'] <= clear['timestamp']:
            gap_exclusions['nonmonotonic_post_or_clear'] += 1
            continue
        gap = clear['timestamp'] - prev_clear['timestamp']
        ack_gaps.append(gap)
        first_blank, last_blank = prev_clear['arg1'], clear['arg1']
        distance = (last_blank - first_blank) & 0xffffffff
        if distance >= len(blanks):
            gap_exclusions['vblank_bracket_missing'] += 1
            continue
        times = []
        for offset in range(distance + 1):
            observed = blanks.get((first_blank + offset) & 0xffffffff, ())
            if len(observed) != 1:
                break
            times.append(observed[0])
        if len(times) != distance + 1:
            gap_exclusions['vblank_bracket_missing'] += 1
            continue
        if (times[0] > prev_clear['timestamp'] or times[-1] > clear['timestamp']
                or any(a >= b for a, b in zip(times, times[1:]))
                or any(t <= prev_clear['timestamp'] or t >= clear['timestamp'] for t in times[1:-1])):
            gap_exclusions['vblank_bracket_timestamp_ambiguous'] += 1
            continue
        bracketed_runs.append(max(0, distance - 1))
    return {
        'observed_distinct_matched_pending_clears': dict(Counter(
            KIND_NAMES[first['kind']] for first, _ in acknowledgements)),
        'post_to_sampled_clear_by_kind': pair_distributions(
            acknowledgements, frequency, lambda a, b: KIND_NAMES[a['kind']]),
        'excluded_post_clear_pairs': pairing_gaps,
        'consecutive_sampled_clear_gaps': distribution(ack_gaps, frequency),
        'excluded_gap_or_vblank_brackets': dict(gap_exclusions),
        'witnessed_interior_vblank_runs': {
            'bracketed_gap_samples': len(bracketed_runs),
            'longest_consecutive_callback_count': max(bracketed_runs, default=None),
            'scope': 'Callback observations strictly inside retained consecutive-clear brackets. '
                     'A witnessed lower bound on the longest such observation gap in this capture.',
        },
        'interpretation': 'Queue/clear observations only. Actual distinct scanout, '
                          'missed or repeated refreshes, and longest physical stall are undetermined.',
    }, acknowledgements


def attempt_dependencies(by_name, stage_pairs, queue_pairs, acknowledgements, frequency):
    """Associate observed regular paths with the adjacent midpoint and top clear."""
    prep_stages = ('FRUSTUM', 'VISIBILITY', 'OBJECT_DISCOVERY')
    attempt_of = lambda r: context(r, ATTEMPT_CONTEXT)
    attempt_counts = Counter(attempt_of(a) for a, _ in stage_pairs['ATTEMPT'])
    attempts = {attempt_of(a): (a, b) for a, b in stage_pairs['ATTEMPT'] if attempt_counts[attempt_of(a)] == 1}
    spans = defaultdict(list)
    for name in (*prep_stages, 'GPU_ACQUIRE', 'GPU_TEXTURE_FENCE'):
        for a, b in stage_pairs[name]:
            spans[(attempt_of(a), name)].append((a, b))
    prep_begins = defaultdict(list)
    for name in (*prep_stages, 'GPU_TEXTURE_FENCE'):
        for event in by_name[f'{name}_BEGIN']:
            prep_begins[(attempt_of(event), name)].append(event['sequence'])
    queue_runs = defaultdict(list)
    for run in by_name['QUEUE_RUN']:
        queue_runs[attempt_of(run)].append(run)
    queue_counts = Counter(run['submission'] for run in by_name['QUEUE_RUN'])
    completions = {a['submission']: b for a, b in queue_pairs}
    clears = defaultdict(list)
    for post, clear in acknowledgements:
        clears[attempt_of(post)].append((post, clear))

    rows = []
    for first, last in sorted(attempts.values(), key=lambda pair: pair[0]['sequence']):
        if first['kind'] != 1:
            continue
        key = attempt_of(first)
        row = dict(zip(ATTEMPT_CONTEXT, key))
        row.update(begin_tick=first['timestamp'], end_tick=last['timestamp'], result=last['arg0'],
                   early_preparation=None, gpu_acquire_span=None, pre_acquire_texture_fences=None,
                   preceding_midpoint_queue=None, early_preparation_queue_overlap_ticks=None,
                   sampled_top_clear=None)

        midpoint = key[:-2] + (first['attempt'] - 1, 2)
        midpoint_attempt = attempts.get(midpoint)
        if midpoint_attempt and midpoint_attempt[1]['arg0'] == 0 and queue_runs[midpoint]:
            midpoint_run = max(queue_runs[midpoint], key=lambda r: r['sequence'])
            submission = midpoint_run['submission']
            if (submission and queue_counts[submission] == 1
                    and midpoint_attempt[0]['sequence'] < midpoint_run['sequence'] < midpoint_attempt[1]['sequence']
                    < first['sequence']
                    and midpoint_attempt[0]['timestamp'] <= midpoint_run['timestamp'] <= midpoint_attempt[1]['timestamp']
                    <= first['timestamp']):
                callback = completions.get(submission)
                row['preceding_midpoint_queue'] = {
                    'attempt': midpoint_run['attempt'], 'submission': submission,
                    'run_tick': midpoint_run['timestamp'],
                    'completion_callback_tick': callback['timestamp'] if callback else None,
                }

        acquire = spans[(key, 'GPU_ACQUIRE')]
        if len(acquire) == 1 and EVENT_NAMES[acquire[0][1]['event'] - 1] == 'GPU_ACQUIRE_END':
            acquire_begin, acquire_end = acquire[0]
            row['gpu_acquire_span'] = {
                'begin_tick': acquire_begin['timestamp'], 'end_tick': acquire_end['timestamp'],
                'observed_ticks': acquire_end['timestamp'] - acquire_begin['timestamp'],
            }
            early = sorted(
                (a['timestamp'], b['timestamp'], name, a['sequence'])
                for name in prep_stages for a, b in spans[(key, name)]
                if first['sequence'] < a['sequence'] < b['sequence'] < acquire_begin['sequence']
            )
            expected = {seq for name in prep_stages for seq in prep_begins[(key, name)]
                        if first['sequence'] < seq < acquire_begin['sequence']}
            if ({seq for _, _, _, seq in early} == expected
                    and all(a[1] <= b[0] for a, b in zip(early, early[1:]))
                    and all(first['timestamp'] <= a <= b <= acquire_begin['timestamp'] for a, b, _, _ in early)):
                row['early_preparation'] = {
                    'spans': [{'stage': stage, 'begin_tick': a, 'end_tick': b} for a, b, stage, _ in early],
                    'observed_ticks': sum(b - a for a, b, _, _ in early),
                }
                midpoint_queue = row['preceding_midpoint_queue']
                if midpoint_queue and midpoint_queue['completion_callback_tick'] is not None:
                    run, callback = midpoint_queue['run_tick'], midpoint_queue['completion_callback_tick']
                    row['early_preparation_queue_overlap_ticks'] = sum(
                        max(0, min(b, callback) - max(a, run)) for a, b, _, _ in early)
            fences = [(a, b) for a, b in spans[(key, 'GPU_TEXTURE_FENCE')]
                if first['sequence'] < a['sequence'] < b['sequence'] < acquire_begin['sequence']
                and first['timestamp'] <= a['timestamp'] <= b['timestamp'] <= acquire_begin['timestamp']]
            expected_fences = {seq for seq in prep_begins[(key, 'GPU_TEXTURE_FENCE')]
                               if first['sequence'] < seq < acquire_begin['sequence']}
            if {a['sequence'] for a, _ in fences} == expected_fences:
                row['pre_acquire_texture_fences'] = [
                    {'begin_tick': a['timestamp'], 'end_tick': b['timestamp'], 'ready': bool(b['arg0'])}
                    for a, b in fences
                ]

        if len(clears[key]) == 1:
            post, clear = clears[key][0]
            if post['timestamp'] >= first['timestamp']:
                callback = completions.get(post['submission'])
                if callback and context(callback, QUEUE_CONTEXT) != context(post, QUEUE_CONTEXT):
                    callback = None
                row['sampled_top_clear'] = {
                    'submission': post['submission'], 'swap_sequence': post['arg0'],
                    'post_tick': post['timestamp'], 'clear_tick': clear['timestamp'],
                    'completion_callback_tick': callback['timestamp'] if callback else None,
                    'attempt_to_clear_ticks': clear['timestamp'] - first['timestamp'],
                }
        rows.append(row)

    cohort = [row for row in rows if row['preceding_midpoint_queue'] is not None]
    return {
        'regular_attempts': rows,
        'after_observed_midpoint_queue': {
            'attempts': len(cohort),
            'early_preparation': distribution(
                (r['early_preparation']['observed_ticks'] for r in cohort if r['early_preparation'] is not None),
                frequency),
            'gpu_acquire_call': distribution(
                (r['gpu_acquire_span']['observed_ticks'] for r in cohort if r['gpu_acquire_span'] is not None),
                frequency),
            'pre_acquire_texture_fence_span': distribution(
                (sum(f['end_tick'] - f['begin_tick'] for f in r['pre_acquire_texture_fences'])
                 for r in cohort if r['pre_acquire_texture_fences'] is not None), frequency),
            'early_preparation_over_queue_observation': distribution(
                (r['early_preparation_queue_overlap_ticks'] for r in cohort
                 if r['early_preparation_queue_overlap_ticks'] is not None), frequency),
            'attempt_to_sampled_top_clear': distribution(
                (r['sampled_top_clear']['attempt_to_clear_ticks'] for r in cohort if r['sampled_top_clear'] is not None),
                frequency),
        },
        'interpretation': 'Early spans can be discarded later. Overlap uses the run-to-completion-callback '
                          'observation interval. Acquisition includes call overhead. A sampled clear reports '
                          'the owner poll timestamp. Null indicates insufficient retained observations.',
    }


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
    frequency = header['timer_frequency_hz']
    stages, workers, stage_pairs = stage_timings(by_name, sequences['owner_and_joined_job'], frequency)
    queues, excluded_queue_pairs = paired_identities(
        by_name['QUEUE_RUN'], by_name['QUEUE_COMPLETE'], lambda r: r['submission'], QUEUE_CONTEXT, (0, 1))
    presentation, acknowledgements = presentation_observations(by_name, frequency)
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
        'stage_timings': stages,
        'worker_timings': workers,
        'queue_observations': {
            'run_to_completion_callback_by_kind': pair_distributions(
                queues, frequency, lambda a, b: KIND_NAMES[a['kind']]),
            'excluded_run_completion_pairs': excluded_queue_pairs,
        },
        'presentation_observations': presentation,
        'attempt_dependencies': attempt_dependencies(by_name, stage_pairs, queues, acknowledgements, frequency),
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
            'pairing_scope': 'raw queue and top-swap identifiers; timing also requires unique matching context',
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
