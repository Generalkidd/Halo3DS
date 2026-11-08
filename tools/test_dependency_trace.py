"""Synthetic schema-1 records for the offline reader."""

from pathlib import Path
import struct
import tempfile
import unittest

import dependency_trace as trace


def record(name, timestamp, sequence, **fields):
    event = dict.fromkeys(trace.RECORD_FIELDS, 0)
    event.update(event=trace.EVENT_NAMES.index(name) + 1, timestamp=timestamp, sequence=sequence)
    event.update(fields)
    return event


def encode(records, drops=(0, 0, 0)):
    header = bytearray(trace.HEADER_BYTES)
    header[:8] = b'H2DSTRC1'
    struct.pack_into('<6I4Q16I', header, 8, 1, trace.HEADER_BYTES, trace.RECORD_BYTES,
                     trace.CAPACITY, len(records), 120, 1, 1000, 1, 1000, *drops,
                     0, 0, 0, 0, 2, 1, 10, 11, trace.BUFFER_BYTES, 1, 1, 8, 9)
    body = header + b''.join(trace.RECORD.pack(*(r[key] for key in trace.RECORD_FIELDS))
                            for r in records)
    return body + struct.pack('<8sII', b'H2DEND01', len(records), trace.fnv1a32(body))


def dependency_fixture(*, early=True, midpoint_callback=True, same_tick=True, visibility_end=True):
    base = dict(loop=9, tick=44, epoch=2, generation=9, bsp=3)
    midpoint = dict(base, kind=2, attempt=7, view=4, submission=13)
    regular = dict(base, tick=44 if same_tick else 45, kind=1, attempt=8, view=5, submission=14)
    early_view = dict(regular, submission=0)
    events = []

    def owner(name, tick, fields, **args):
        events.append(record(name, tick, len(events) + 1, **dict(fields, **args)))

    owner('ATTEMPT_BEGIN', 90, midpoint, view=0, submission=0)
    owner('QUEUE_RUN', 100, midpoint)
    owner('ATTEMPT_END', 102, midpoint, view=0)
    owner('ATTEMPT_BEGIN', 104, regular, view=0, submission=0)
    if not early:
        owner('GPU_ACQUIRE_BEGIN', 107, regular, view=0, submission=0)
        owner('GPU_ACQUIRE_END', 109, regular, view=0, submission=0)
    shift = 0 if early else 10
    owner('FRUSTUM_BEGIN', 108 + shift, early_view)
    owner('FRUSTUM_END', 111 + shift, early_view)
    owner('VISIBILITY_BEGIN', 112 + shift, early_view)
    if visibility_end:
        owner('VISIBILITY_END', 125 + shift, early_view)
    owner('OBJECT_DISCOVERY_BEGIN', 126 + shift, early_view)
    owner('OBJECT_DISCOVERY_END', 132 + shift, early_view)
    if early:
        owner('GPU_TEXTURE_FENCE_BEGIN', 136, early_view)
        owner('GPU_TEXTURE_FENCE_END', 141, early_view, arg0=1)
        owner('GPU_ACQUIRE_BEGIN', 143, early_view)
        owner('GPU_ACQUIRE_END', 150, early_view)
    owner('QUEUE_RUN', 175, regular)
    owner('ATTEMPT_END', 177, regular, view=0)
    owner('TOP_PENDING_CLEAR', 195, regular, arg0=9)
    if midpoint_callback:
        events.append(record('QUEUE_COMPLETE', 117, 1, producer=1, **midpoint))
    events.extend([
        record('QUEUE_COMPLETE', 190, 2, producer=1, arg0=1, **regular),
        record('TOP_SWAP_POSTED', 191, 3, producer=1, arg0=9, **regular),
    ])
    return events


class DependencyTraceTest(unittest.TestCase):
    def summarize(self, records, drops=(0, 0, 0)):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / 'test.bin'
            path.write_bytes(encode(records, drops))
            data, header, parsed, checksum = trace.read_trace(path)
            return trace.summarize(path, data, header, parsed, checksum)

    def test_context_pairing_uses_sequence_and_keeps_missing_and_rejected_spans(self):
        common = dict(loop=7, tick=50, epoch=2, generation=1, bsp=4, attempt=3, view=2, kind=1)
        start = dict(common, view=0, attempt=0, kind=0)
        result = self.summarize([
            record('FRAME_SERVICES_BEGIN', 10, 1, **start),
            record('FRAME_SERVICES_END', 12, 2, **dict(start, epoch=3, generation=2)),
            record('FRUSTUM_BEGIN', 20, 3, **common),
            record('FRUSTUM_END', 28, 4, **common),
            record('VISIBILITY_BEGIN', 29, 5, **common),
            record('VISIBILITY_END', 30, 6, **dict(common, bsp=5)),
            record('GPU_ACQUIRE_BEGIN', 31, 7, **common),
            record('GPU_ACQUIRE_REJECT', 33, 8, **common),
            record('THREAD_COLLECT_BEGIN', 35, 9, **common),
            record('THREAD_COLLECT_END', 36, 11, **common),
        ], drops=(1, 0, 0))['stage_timings']
        self.assertEqual(result['FRAME_SERVICES']['by_kind']['frontend']['ticks']['p99'], 2)
        self.assertEqual(result['FRUSTUM']['by_kind']['regular']['ticks']['p50'], 8)
        self.assertEqual(result['VISIBILITY']['unpaired'], {'ends_without_begin': 1, 'begins_without_end': 1})
        self.assertEqual(result['GPU_ACQUIRE']['by_kind']['regular_rejected']['samples'], 1)
        self.assertEqual(result['THREAD_COLLECT']['unpaired'], {'sequence_gap_or_duplicate': 1})

    def test_worker_observations_are_paired_despite_nonchronological_timestamps(self):
        job = dict(loop=1, job=9, kind=2, producer=3, arg0=2)
        result = self.summarize([
            record('WORKER_DISPATCH', 10, 1, arg1=8, **job),
            record('WORKER_START', 13, 2, arg1=3, **job),
            record('WORKER_END', 18, 3, arg1=3, **job),
            record('MAIN_WORK_BEGIN', 11, 4, arg1=5, **job),
            record('MAIN_WORK_END', 16, 5, arg1=5, **job),
            record('JOIN_BEGIN', 16, 6, arg1=3, **job),
            record('JOIN_END', 20, 7, arg1=3, **job),
        ])['worker_timings']
        expected = {'worker_wake': 3, 'worker': 5, 'owner_work': 5, 'join': 4, 'whole_batch': 10}
        for name, ticks in expected.items():
            self.assertEqual(result[name]['by_kind_and_category']['midpoint:2']['ticks']['max'], ticks)

    def test_queue_and_swap_gaps_count_only_bracketed_evidence(self):
        records = []
        for submission, swap, blank, post, clear in [
                (1, 7, 5, 10, 12), (2, 8, 9, 39, 52), (3, 10, 11, 55, 61)]:
            context = dict(loop=submission, attempt=submission, view=submission, submission=submission, kind=1)
            owner_sequence = 2 * submission - 1
            callback_sequence = 2 * submission - 1
            records.extend([
                record('QUEUE_RUN', post - 4, owner_sequence, **context),
                record('QUEUE_COMPLETE', post - 1, callback_sequence, producer=1, arg0=1, **context),
                record('TOP_SWAP_POSTED', post, callback_sequence + 1, producer=1, arg0=swap, **context),
                record('TOP_PENDING_CLEAR', clear, owner_sequence + 1, arg0=swap, arg1=blank, **context),
            ])
        records += [
            record('QUEUE_RUN', 68, 7, submission=4, kind=2),
            record('QUEUE_RUN', 70, 8, submission=5, kind=2, generation=2),
            record('QUEUE_COMPLETE', 71, 7, submission=5, kind=2, generation=3, producer=1),
        ]
        blank_records = [record('VBLANK', stamp, i, producer=2, arg0=i + 4)
                         for i, stamp in enumerate((8, 19, 28, 36, 47, 53, 57), 1)]
        complete = self.summarize(records + blank_records)
        presentation = complete['presentation_observations']
        self.assertEqual(complete['queue_observations']['run_to_completion_callback_by_kind']
                         ['regular']['ticks']['p95'], 3)
        self.assertEqual(complete['queue_observations']['excluded_run_completion_pairs'],
                         {'identity_context_mismatch': 1})
        self.assertEqual(complete['association_gaps']['queue_runs_without_retained_completion']['first_ids'], [4])
        self.assertEqual(presentation['observed_distinct_matched_pending_clears'], {'regular': 3})
        self.assertEqual(presentation['consecutive_sampled_clear_gaps']['ticks']['max'], 40)
        self.assertEqual(presentation['witnessed_interior_vblank_runs']['longest_consecutive_callback_count'], 3)
        self.assertEqual(presentation['excluded_gap_or_vblank_brackets'], {'nonconsecutive_swap_sequence': 1})

        dropped = self.summarize(records + blank_records[:2] + blank_records[3:], drops=(0, 0, 1))
        presentation = dropped['presentation_observations']
        self.assertEqual(presentation['consecutive_sampled_clear_gaps']['samples'], 1)
        self.assertIsNone(presentation['witnessed_interior_vblank_runs']['longest_consecutive_callback_count'])
        self.assertEqual(presentation['excluded_gap_or_vblank_brackets']['vblank_bracket_missing'], 1)

    def test_out_of_window_record_is_rejected_even_with_valid_checksum(self):
        with self.assertRaisesRegex(ValueError, 'outside the capture interval'):
            self.summarize([record('LOOP_BEGIN', 1001, 1)])

    def test_attempt_dependencies_join_preparation_to_observed_midpoint_and_regular_swap(self):
        report = self.summarize(dependency_fixture())['attempt_dependencies']
        sample, = report['regular_attempts']
        self.assertEqual(sample['attempt'], 8)
        self.assertEqual(sample['preceding_midpoint_queue'],
                         {'attempt': 7, 'submission': 13, 'run_tick': 100, 'completion_callback_tick': 117})
        self.assertEqual(sample['early_preparation']['observed_ticks'], 22)
        self.assertEqual(sample['early_preparation_queue_overlap_ticks'], 8)
        self.assertEqual(sample['gpu_acquire_span']['observed_ticks'], 7)
        self.assertEqual(sample['pre_acquire_texture_fences'], [{'begin_tick': 136, 'end_tick': 141, 'ready': True}])
        self.assertEqual(sample['sampled_top_clear']['attempt_to_clear_ticks'], 91)
        self.assertEqual(sample['sampled_top_clear']['completion_callback_tick'], 190)
        self.assertEqual(report['after_observed_midpoint_queue']['attempts'], 1)

    def test_attempt_dependencies_distinguish_reference_zero_from_missing_overlap(self):
        cases = [
            (dict(early=False), 0, 0, True),
            (dict(midpoint_callback=False), 22, None, True),
            (dict(same_tick=False), 22, None, False),
            (dict(visibility_end=False), None, None, True),
        ]
        for options, early_ticks, overlap, linked in cases:
            with self.subTest(options=options):
                report = self.summarize(dependency_fixture(**options))['attempt_dependencies']
                sample, = report['regular_attempts']
                self.assertEqual(sample['preceding_midpoint_queue'] is not None, linked)
                early = sample['early_preparation']
                self.assertEqual(early['observed_ticks'] if early is not None else None, early_ticks)
                self.assertEqual(sample['early_preparation_queue_overlap_ticks'], overlap)


if __name__ == '__main__':
    unittest.main()
