#!/usr/bin/env python3
"""Summarize paired physical New 2DS XL refresh observations."""

import argparse
import json
import random
import re
import statistics
from pathlib import Path


def integer(value, label):
    if type(value) is not int or value < 0:
        raise ValueError(f'{label} must be a nonnegative integer')
    return value


def quantile(ordered, percent):
    return ordered[(len(ordered) * percent + 99) // 100 - 1]


def intervals(values):
    if not values:
        return None
    ordered = sorted(values)
    return {**{f'p{p}_us': quantile(ordered, p) for p in (50, 95, 99)},
            'worst_us': ordered[-1], 'samples': len(ordered)}


def summarize_run(run):
    if run['device'] != 'New Nintendo 2DS XL' or run['observation'] != 'physical-refresh-ids':
        raise ValueError('A physical New 2DS XL refresh observation stream is required')
    if run['timing_mode'] != 'uncaptured' or run['policy'] not in ('cold', 'warm'):
        raise ValueError('Use uncaptured guest timing and a named cold or warm policy')
    for name in ('settings_sha256', 'workload_sha256', 'binary_sha256'):
        if not re.fullmatch('[0-9a-f]{64}', run[name]):
            raise ValueError(f'Invalid {name}')
    refreshes = run['refreshes']
    if len(refreshes) < 2:
        raise ValueError('A run must contain consecutive refresh observations')
    first, last = refreshes[0][1], refreshes[-1][1]
    duration = last - first
    if not 90_000_000 <= duration <= 120_000_000:
        raise ValueError('Each observation window must span 90–120 seconds')
    period = duration / (len(refreshes) - 1)
    if not 16_000 <= period <= 17_500:
        raise ValueError('Average observation cadence differs from the expected display refresh')

    previous = None
    last_delivery = first
    repeats = consecutive = longest = 0
    gaps = []
    for index, time, image in refreshes:
        integer(index, 'refresh index')
        integer(time, 'refresh timestamp')
        integer(image, 'displayed presentation identity')
        if previous:
            old_index, old_time, old_image = previous
            if index != old_index + 1 or not 8_000 <= time - old_time <= 25_000 or image < old_image:
                raise ValueError('Missing, out of order or ambiguous physical refresh observation')
            if image == old_image:
                repeats += 1
                consecutive += 1
                longest = max(longest, consecutive)
            else:
                consecutive = 0
                gaps.append(time - last_delivery)
                last_delivery = time
        previous = index, time, image

    simulation = run['simulation']
    if not simulation:
        raise ValueError('Simulation deadlines must be provided for the active simulation window')
    prior_tick = prior_deadline = None
    missed = missing = 0
    lateness = []
    for tick, deadline, completion in simulation:
        integer(tick, 'simulation tick')
        integer(deadline, 'simulation deadline')
        if not first <= deadline <= last or (prior_tick is not None and
                (tick != prior_tick + 1 or deadline <= prior_deadline)):
            raise ValueError('Missing or unordered simulation deadlines within the observation window')
        if completion is None:
            missing += 1
            missed += 1
        else:
            integer(completion, 'simulation completion')
            missed += completion > deadline
            lateness.append(completion - deadline)
        prior_tick, prior_deadline = tick, deadline
    return {
        'duration_us': duration,
        'refresh_intervals': len(refreshes) - 1,
        'distinct_delivered': len(gaps),
        'distinct_per_second': len(gaps) * 1e6 / duration,
        'repeated_refresh_intervals': repeats,
        'longest_consecutive_repeats': longest,
        'delivery_intervals': intervals(gaps),
        'unclosed_tail_us': last - last_delivery,
        'worst_observed_stall_lower_bound_us': max(max(gaps, default=0), last - last_delivery),
        'simulation_deadlines': len(simulation),
        'simulation_missed_deadlines': missed,
        'simulation_unobserved_completions': missing,
        'simulation_worst_lateness_us': max(lateness, default=None),
    }


def paired_delta(values, generator):
    count = len(values)
    resamples = sorted(statistics.fmean(generator.choices(values, k=count)) for _ in range(10000))
    return {'pair_differences': values, 'mean': statistics.fmean(values),
            'median': statistics.median(values),
            'bootstrap_mean_95_interval': [resamples[249], resamples[9749]]}


def analyze(data):
    if data['schema'] != 1:
        raise ValueError('Unsupported observation schema')
    runs = data['runs']
    if len(runs) < 10 or len(runs) % 2:
        raise ValueError('At least five complete A/B pairs are required')
    keys = ('route', 'policy', 'settings_sha256', 'workload_sha256', 'device')
    cohort = {key: runs[0][key] for key in keys}
    variants = ['reference', 'candidate']
    order = runs[0]['variant']
    if order not in variants:
        raise ValueError('Expected reference or candidate')
    binary_ids = {}
    results = []
    for offset in range(0, len(runs), 2):
        pair = runs[offset:offset + 2]
        expected = [order, variants[1 - variants.index(order)]]
        if [row['variant'] for row in pair] != expected:
            raise ValueError('Alternate reference/candidate order across paired runs')
        order = expected[1]
        compared = {}
        for row in pair:
            if row['pair'] != offset // 2 + 1 or any(row[key] != cohort[key] for key in keys):
                raise ValueError('Pair IDs and workload/settings receipts must match the cohort')
            variant, binary = row['variant'], row['binary_sha256']
            if binary_ids.setdefault(variant, binary) != binary:
                raise ValueError('Each variant must use one executable identity')
            compared[variant] = summarize_run(row)
        results.append(compared)
    generator = random.Random(0)
    metrics = ('distinct_per_second', 'repeated_refresh_intervals', 'longest_consecutive_repeats',
               'worst_observed_stall_lower_bound_us', 'simulation_missed_deadlines')
    differences = {name: paired_delta(
        [pair['candidate'][name] - pair['reference'][name] for pair in results], generator)
        for name in metrics}
    if all(run['delivery_intervals'] for pair in results for run in pair.values()):
        for name in ('p50_us', 'p95_us', 'p99_us'):
            differences['delivery_'+name] = paired_delta([
                pair['candidate']['delivery_intervals'][name] -
                pair['reference']['delivery_intervals'][name] for pair in results], generator)
    return {'cohort': cohort, 'binary_sha256': binary_ids, 'pairs': results,
            'candidate_minus_reference': differences,
            'interval_method': 'Percentile bootstrap over complete run pairs, 10000 resamples, fixed seed 0'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('observations', type=Path)
    args = parser.parse_args()
    try:
        result = analyze(json.loads(args.observations.read_text()))
    except (OSError, ValueError, TypeError, KeyError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
