#!/usr/bin/env python3
"""Validate one native unused-texture-units run; report queue times, not FPS."""
import argparse
import json
import math
from pathlib import Path
import re


def analyze(text, run):
    records = []
    for line in text.splitlines():
        match = re.search(r"GPU PROBE ([A-Z ]+): (.*)", line)
        if not match:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", match[2]))
        if fields.get("run") == str(run):
            records.append((match[1], fields))
    def require(condition, reason):
        if not condition:
            raise ValueError(reason)
    require(records and records[0][0] == "START", "missing start")
    require(records[-1][0] == "COMPLETE", "missing completion or trailing records")
    require(sum(kind == "START" for kind, _ in records) == 1, "duplicate run ID")
    require(not any(kind == "ABORT" for kind, _ in records), "aborted run")
    complete = records[-1][1]
    require(complete.get("rendering") == "unused-texture-units", "wrong probe variant")
    require(complete.get("missing") == "0", "missing timing samples; repeat run")
    expected = ["START"] + [kind for _ in range(5)
                           for kind in ("PHASE", "TEXTURE UNITS", "COUNTS")] + ["COMPLETE"]
    require([kind for kind, _ in records] == expected, "incomplete, duplicate or reordered phase records")
    medians, counts, candidate_units = [], [], []
    for phase in range(1, 6):
        timing, units, geometry = [fields for _, fields in records[1 + (phase-1)*3:4 + (phase-1)*3]]
        require(all(row.get("phase") == str(phase) for row in (timing, units, geometry)), "phase identity mismatch")
        mode = "reference" if phase % 2 else "unused-texture-units"
        require(timing.get("mode") == mode and timing.get("samples") == "12", "wrong phase mode or sample count")
        values = [float(timing[key]) for key in ("queue_min_ms", "queue_median_ms", "queue_max_ms", "begin_wait_mean_ms")]
        require(all(math.isfinite(value) and value >= 0 for value in values), "invalid timing")
        require(0 < values[0] <= values[1] <= values[2], "invalid timing range")
        medians.append(values[1])
        def fixed_range(row, key):
            match = re.fullmatch(r"(\d+)\.\.(\d+)", row[key])
            require(match is not None, "invalid counter range")
            low, high = map(int, match.groups())
            require(low == high, f"changing {key} in phase {phase}; repeat with a stable view")
            return low
        counts.append(tuple(fixed_range(geometry, key) for key in (
            "renderer_draws", "renderer_elements", "opaque_bsp_draws", "opaque_bsp_elements")))
        reduced, omitted, writes = [fixed_range(units, key) for key in (
            "reduced_draws", "omitted_units", "mask_writes")]
        require((reduced == omitted == 0) if phase % 2 else (reduced > 0 and omitted > 0),
                "candidate inactive or reference unexpectedly reduced")
        if phase % 2 == 0:
            candidate_units.append((reduced, omitted, writes))
    require(all(count == counts[0] for count in counts), "geometry differs between phases")
    require(all(value > 0 for value in counts[0]), "empty renderer or terrain workload")
    require(candidate_units[0] == candidate_units[1], "texture-unit workload differs between candidate phases")
    comparisons = []
    for index in (1, 3):
        before, candidate, after = medians[index-1:index+2]
        baseline = (before + after) / 2
        comparisons.append({"phase": index+1, "candidate_queue_ms": candidate,
                            "reference_before_ms": before, "reference_after_ms": after,
                            "bracket_mean_ms": baseline, "queue_reduction_percent": 100*(baseline-candidate)/baseline,
                            "reference_drift_percent": 100*abs(after-before)/baseline})
    return {"run": run, "comparisons": comparisons,
            "scope": "Stable logged workload only. Queue timing is not displayed FPS or visual parity; reference drift remains a confounder."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--run", type=int, required=True)
    args = parser.parse_args()
    try:
        result = analyze(args.log.read_text(), args.run)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"Unusable comparison: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
