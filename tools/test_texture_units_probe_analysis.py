#!/usr/bin/env python3
"""Synthetic protocol checks; these are not measurements from a console."""
import unittest
from analyze_texture_units_probe import analyze


def fixture():
    lines = ["GPU PROBE START: run=1 tick=123 generation=2 bsp=0 camera=1,2,3"]
    for phase, median in enumerate((80, 60, 82, 61, 84), 1):
        candidate = phase % 2 == 0
        mode = "unused-texture-units" if candidate else "reference"
        lines.append(f"GPU PROBE PHASE: run=1 phase={phase} mode={mode} samples=12 queue_min_ms=50 queue_median_ms={median} queue_max_ms=90 begin_wait_mean_ms=3")
        count = 10 if candidate else 0
        lines.append(f"GPU PROBE TEXTURE UNITS: run=1 phase={phase} reduced_draws={count}..{count} omitted_units={count}..{count} mask_writes=2..2")
        lines.append(f"GPU PROBE COUNTS: run=1 phase={phase} renderer_draws=30..30 renderer_elements=300..300 opaque_bsp_draws=20..20 opaque_bsp_elements=200..200")
    lines.append("GPU PROBE COMPLETE: run=1 missing=0 rendering=unused-texture-units")
    return "\n".join(lines)


class ProbeAnalysisTest(unittest.TestCase):
    def test_bracketing(self):
        result = analyze(fixture(), 1)
        first, second = result["comparisons"]
        self.assertEqual(first["bracket_mean_ms"], 81)
        self.assertEqual(second["bracket_mean_ms"], 83)
        self.assertAlmostEqual(first["queue_reduction_percent"], 100*21/81)

    def test_rejects_bad_evidence(self):
        good = fixture()
        mutations = [
            good.rsplit("\n", 1)[0], good + "\n" + good,
            good.replace("GPU PROBE COMPLETE", "GPU PROBE ABORT"),
            good.replace("missing=0", "missing=1"),
            good.replace("samples=12", "samples=11", 1),
            good.replace("queue_median_ms=80", "queue_median_ms=nan"),
            good.replace("queue_median_ms=80", "queue_median_ms=99"),
            good.replace("renderer_draws=30..30", "renderer_draws=29..30", 1),
            good.replace("renderer_draws=30..30", "renderer_draws=29..29", 1),
            good.replace("reduced_draws=10..10", "reduced_draws=0..0"),
            good.replace("omitted_units=10..10", "omitted_units=20..20", 1),
            good.replace("rendering=unused-texture-units", "rendering=normal"),
            good.replace("phase=3", "phase=2"),
            "\n".join(line for line in good.splitlines() if "TEXTURE UNITS" not in line),
        ]
        for log in mutations:
            with self.subTest(log=log):
                with self.assertRaises(ValueError):
                    analyze(log, 1)

    def test_selects_run_and_accepts_log_prefix(self):
        log = fixture().replace("run=1", "run=2") + "\n" + fixture()
        log = "\n".join("[log] " + line for line in log.splitlines())
        self.assertEqual(analyze(log, 2)["run"], 2)
        with self.assertRaises(ValueError):
            analyze(log, 3)


if __name__ == "__main__":
    unittest.main()
