import copy
import unittest

from paired_presentations import analyze


class PairedPresentationsTests(unittest.TestCase):
    def cohort(self):
        period = 16714
        count = 5400
        common = dict(device='New Nintendo 2DS XL', observation='physical-refresh-ids',
                      timing_mode='uncaptured', route='synthetic', policy='cold',
                      settings_sha256='0'*64, workload_sha256='1'*64)
        data = {'schema': 1, 'runs': []}
        for pair in range(1, 6):
            variants = ('reference', 'candidate') if pair % 2 else ('candidate', 'reference')
            for variant in variants:
                reference = variant == 'reference'
                # Reference repeats twice near the start and once at the tail.
                refreshes = [[i, i*period, i] for i in range(count+1)]
                if reference:
                    for index in (1, 2, count):
                        refreshes[index][2] = refreshes[index-1][2]
                data['runs'].append(dict(common, pair=pair, variant=variant,
                    binary_sha256=('2' if reference else '3')*64, refreshes=refreshes,
                    simulation=[[0, 33333, None if reference else 33330], [1, 66666, 66666]]))
        return data

    def test_repeat_tail_and_pair_uncertainty(self):
        report = analyze(self.cohort())
        reference = report['pairs'][0]['reference']
        self.assertEqual(reference['distinct_delivered'], 5397)
        self.assertEqual(reference['longest_consecutive_repeats'], 2)
        self.assertEqual(reference['unclosed_tail_us'], 16714)
        self.assertEqual(reference['delivery_intervals']['worst_us'], 50142)
        self.assertEqual(reference['simulation_missed_deadlines'], 1)
        repeat_delta = report['candidate_minus_reference']['repeated_refresh_intervals']
        self.assertEqual(repeat_delta['pair_differences'], [-3]*5)
        self.assertEqual(repeat_delta['bootstrap_mean_95_interval'], [-3, -3])

    def test_rejects_lost_observation_and_mismatched_workload(self):
        data = self.cohort()
        broken = copy.deepcopy(data)
        broken['runs'][0]['refreshes'].pop(100)
        with self.assertRaisesRegex(ValueError, 'physical refresh'):
            analyze(broken)
        data['runs'][-1]['workload_sha256'] = 'a'*64
        with self.assertRaisesRegex(ValueError, 'workload/settings'):
            analyze(data)


if __name__ == '__main__':
    unittest.main()
