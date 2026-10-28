import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


N3DS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(N3DS / "tools"))
import frame_profile_report as report


CAPTURE = """\
startup: unrelated log line
FRAME PROFILE START: version=1 window=2 tick_hz=3000 presentation=unverified
FRAME PROFILE WINDOW: id=1 scene=gameplay map=a10 model=original stereo=0 quality=0 first=1 last=2 samples=2 invalid=0 dropped=0 metadata_missing=0 gpu_begins=2 gpu_begins_anomalous=0 gpu_submissions=2 gpu_submissions_anomalous=0 queue_samples=1 queue_missing=1 queue_invalid=0 status=incomplete presentation=unverified
unrelated interleaved diagnostics
FRAME PROFILE TIMING: id=1 work_ms_p50=10.000 work_ms_p95=34.000 work_ms_p99=34.000 work_ms_max=34.000 wall_ms_p50=20.000 wall_ms_p95=40.000 wall_ms_p99=40.000 wall_ms_max=40.000 work_over_30hz=1 work_max_streak=1 wall_over_30hz=1 wall_max_streak=1 vblank_callbacks=4 vblank_max_per_frame=2
FRAME PROFILE STAGES: id=1 mean_ms services=1.000 input=1.000 simulation=1.000 audio=1.000 render=4.000 tail=2.000 pacing=5.000 gap=1.000
FRAME PROFILE GPU WAIT: id=1 samples=2 mean_ms=1.500 p50_ms=1.000 p95_ms=2.000 p99_ms=2.000 max_ms=2.000
FRAME PROFILE GPU: id=1 samples=1 previous_queue_mean_ms=8.000 p50_ms=8.000 p95_ms=8.000 p99_ms=8.000 max_ms=8.000; overlaps CPU and includes transfers
FRAME PROFILE WINDOW: id=2 scene=gameplay map=a10 model=original stereo=0 quality=0 first=3 last=4 samples=2 invalid=0 dropped=0 metadata_missing=0 gpu_begins=2 gpu_begins_anomalous=0 gpu_submissions=2 gpu_submissions_anomalous=0 queue_samples=2 queue_missing=0 queue_invalid=0 status=complete presentation=unverified
FRAME PROFILE TIMING: id=2 work_ms_p50=10.000 work_ms_p95=20.000 work_ms_p99=20.000 work_ms_max=20.000 wall_ms_p50=20.000 wall_ms_p95=30.000 wall_ms_p99=30.000 wall_ms_max=30.000 work_over_30hz=0 work_max_streak=0 wall_over_30hz=0 wall_max_streak=0 vblank_callbacks=4 vblank_max_per_frame=2
FRAME PROFILE STAGES: id=2 mean_ms services=1.000 input=1.000 simulation=1.000 audio=1.000 render=4.000 tail=2.000 pacing=5.000 gap=1.000
FRAME PROFILE GPU WAIT: id=2 samples=2 mean_ms=1.500 p50_ms=1.000 p95_ms=2.000 p99_ms=2.000 max_ms=2.000
FRAME PROFILE GPU: id=2 samples=2 previous_queue_mean_ms=8.000 p50_ms=8.000 p95_ms=8.000 p99_ms=8.000 max_ms=8.000; overlaps CPU and includes transfers
FRAME PROFILE END: windows=2 incomplete_windows=1 errors=0 aborted=0 user_stops=0 loading_overlaps=0 presentation=unverified
LOG QUEUE: dropped=0 io_errors=0; routine writes ran off the frame thread
"""

NO_VALID_SAMPLES = """\
FRAME PROFILE START: version=1 window=300 tick_hz=3000 presentation=unverified
FRAME PROFILE WINDOW: id=1 scene=menu map=ui model=original stereo=0 quality=0 first=1 last=1 samples=0 invalid=1 dropped=0 metadata_missing=0 gpu_begins=0 gpu_begins_anomalous=0 gpu_submissions=0 gpu_submissions_anomalous=0 queue_samples=0 queue_missing=0 queue_invalid=0 status=incomplete presentation=unverified
FRAME PROFILE TIMING: id=1 no valid samples
FRAME PROFILE END: windows=1 incomplete_windows=1 errors=0 aborted=0 user_stops=0 loading_overlaps=0 presentation=unverified
LOG QUEUE: dropped=0 io_errors=0; routine writes ran off the frame thread
"""


def parse(text):
    return report.inspect_capture(io.StringIO(text))


def replace_once(text, old, new):
    if old not in text:
        raise AssertionError(f"fixture does not contain {old!r}")
    return text.replace(old, new, 1)


class FrameProfileReportTests(unittest.TestCase):
    def assert_rejected(self, text):
        with self.assertRaises(report.CaptureError):
            parse(text)

    def test_interleaved_records_and_percentiles(self):
        capture = parse(CAPTURE)
        self.assertEqual([w.status for w in capture.windows], ["incomplete", "complete"])
        self.assertEqual((capture.windows[0].work_ms.p95, capture.windows[0].wall_ms.p95), (34.0, 40.0))
        self.assertEqual(capture.windows[1].queue_ms.p95, 8.0)
        self.assertEqual((capture.loading_overlaps, capture.aborted, capture.user_stops), (0, 0, 0))

    def test_valid_window_with_no_cpu_samples_is_incomplete(self):
        window = parse(NO_VALID_SAMPLES).windows[0]
        self.assertEqual((window.samples, window.invalid, window.status), (0, 1, "incomplete"))
        self.assertIsNone(window.work_ms)

    def test_dropped_and_invalid_queue_samples_remain_incomplete(self):
        dropped = replace_once(NO_VALID_SAMPLES, "invalid=1 dropped=0", "invalid=0 dropped=1")
        self.assertEqual(parse(dropped).windows[0].dropped, 1)
        invalid_queue = replace_once(
            CAPTURE, "queue_samples=1 queue_missing=1 queue_invalid=0",
            "queue_samples=0 queue_missing=1 queue_invalid=1",
        )
        invalid_queue = replace_once(
            invalid_queue,
            "FRAME PROFILE GPU: id=1 samples=1 previous_queue_mean_ms=8.000 p50_ms=8.000 "
            "p95_ms=8.000 p99_ms=8.000 max_ms=8.000; overlaps CPU and includes transfers",
            "FRAME PROFILE GPU: id=1 previous_queue=unavailable",
        )
        window = parse(invalid_queue).windows[0]
        self.assertEqual((window.queue_invalid, window.status), (1, "incomplete"))
        self.assertIsNone(window.queue_ms)

    def test_missing_lifecycle_and_log_loss_are_rejected(self):
        cases = {
            "start": replace_once(CAPTURE, "FRAME PROFILE START:", "NOT A PROFILE START:"),
            "end": replace_once(CAPTURE, "FRAME PROFILE END:", "NOT A PROFILE END:"),
            "footer": replace_once(CAPTURE, "LOG QUEUE: dropped=", "NOT A FOOTER: dropped="),
            "truncated footer": CAPTURE.rstrip("\n"),
            "dropped log": replace_once(CAPTURE, "LOG QUEUE: dropped=0", "LOG QUEUE: dropped=1"),
            "io error": replace_once(CAPTURE, "io_errors=0", "io_errors=1"),
            "profiler error": replace_once(CAPTURE, "errors=0 aborted", "errors=1 aborted"),
            "error marker": "FRAME PROFILE ERROR: failed\n" + CAPTURE,
            "duplicate footer": CAPTURE + CAPTURE.splitlines(keepends=True)[-1],
            "record after end": CAPTURE + "FRAME PROFILE STOP: unexpected\n",
        }
        for name, text in cases.items():
            with self.subTest(name=name):
                self.assert_rejected(text)

    def test_malformed_and_unsupported_records_are_rejected(self):
        cases = {
            "version": ("version=1", "version=2"),
            "frequency": ("tick_hz=3000", "tick_hz=0"),
            "unknown scene": ("scene=gameplay", "scene=unknown"),
            "presentation": ("presentation=unverified", "presentation=verified"),
            "duplicate field": ("stereo=0", "stereo=0 stereo=1"),
            "unknown field": ("quality=0", "quality=0 extra=1"),
            "bad number": ("quality=0", "quality=-1"),
            "nan": ("work_ms_p95=34.000", "work_ms_p95=nan"),
            "infinity": ("previous_queue_mean_ms=8.000", "previous_queue_mean_ms=inf"),
            "negative time": ("render=4.000", "render=-4.000"),
            "percentile order": ("work_ms_p95=34.000", "work_ms_p95=9.000"),
            "wall shorter than work": ("wall_ms_p95=40.000", "wall_ms_p95=30.000"),
            "unknown profile record": ("unrelated interleaved diagnostics", "FRAME PROFILE NEW: value=1"),
        }
        for name, (old, new) in cases.items():
            with self.subTest(name=name):
                self.assert_rejected(replace_once(CAPTURE, old, new))

    def test_window_counts_and_required_records_are_validated(self):
        cases = {
            "skipped window": ("WINDOW: id=2", "WINDOW: id=3"),
            "skipped frame": ("first=3 last=4", "first=4 last=5"),
            "count mismatch": ("first=1 last=2 samples=2", "first=1 last=2 samples=1"),
            "over capacity": ("window=2", "window=1"),
            "queue accounting": ("queue_samples=1 queue_missing=1", "queue_samples=2 queue_missing=1"),
            "begin count": ("gpu_begins=2", "gpu_begins=1"),
            "missing anomalous queue": ("gpu_begins=2 gpu_begins_anomalous=0", "gpu_begins=0 gpu_begins_anomalous=2"),
            "submission anomaly": ("gpu_submissions_anomalous=0", "gpu_submissions_anomalous=3"),
            "status mismatch": ("status=incomplete", "status=complete"),
            "missing metadata": ("map=a10", "map=none"),
            "wrong record id": ("TIMING: id=1", "TIMING: id=2"),
            "missing timing": ("FRAME PROFILE TIMING: id=1", "ignored timing: id=1"),
            "missing GPU wait": ("FRAME PROFILE GPU WAIT: id=1", "ignored GPU wait: id=1"),
            "mismatched GPU count": ("GPU WAIT: id=1 samples=2", "GPU WAIT: id=1 samples=1"),
            "over-budget accounting": ("work_over_30hz=1", "work_over_30hz=3"),
            "streak accounting": ("work_max_streak=1", "work_max_streak=0"),
            "VBlank accounting": ("vblank_max_per_frame=2", "vblank_max_per_frame=5"),
            "summary count": ("END: windows=2", "END: windows=1"),
            "incomplete count": ("incomplete_windows=1", "incomplete_windows=0"),
        }
        for name, (old, new) in cases.items():
            with self.subTest(name=name):
                self.assert_rejected(replace_once(CAPTURE, old, new))

    def test_exclusions_stop_and_abort_accounting(self):
        capture = replace_once(
            CAPTURE, "FRAME PROFILE WINDOW: id=2",
            "FRAME PROFILE EXCLUDED: frame=3 loading_submissions=2; mixed loading and active work\n"
            "FRAME PROFILE WINDOW: id=2",
        )
        capture = replace_once(capture, "first=3 last=4", "first=4 last=5")
        capture = replace_once(capture, "loading_overlaps=0", "loading_overlaps=1")
        capture = replace_once(
            capture, "FRAME PROFILE END:",
            "FRAME PROFILE STOP: user stopped before rendering; partial iteration excluded\n"
            "FRAME PROFILE ABORTED: loop ended before this frame was rendered\nFRAME PROFILE END:"
        )
        capture = replace_once(capture, "aborted=0 user_stops=0", "aborted=1 user_stops=1")
        parsed = parse(capture)
        self.assertEqual((parsed.loading_overlaps, parsed.aborted, parsed.user_stops), (1, 1, 1))
        self.assert_rejected(replace_once(capture, "loading_submissions=2", "loading_submissions=0"))
        self.assert_rejected(replace_once(capture, "user_stops=1", "user_stops=0"))
        self.assert_rejected(replace_once(capture, "partial iteration excluded", "partial iteration included"))

    def test_unavailable_gpu_records_and_metadata(self):
        capture = replace_once(
            NO_VALID_SAMPLES,
            "map=ui model=original stereo=0 quality=0 first=1 last=1 samples=0 invalid=1 dropped=0 metadata_missing=0 gpu_begins=0 gpu_begins_anomalous=0 gpu_submissions=0 gpu_submissions_anomalous=0 queue_samples=0 queue_missing=0 queue_invalid=0",
            "map=none model=original stereo=0 quality=0 first=1 last=1 samples=1 invalid=0 dropped=0 metadata_missing=1 gpu_begins=0 gpu_begins_anomalous=1 gpu_submissions=0 gpu_submissions_anomalous=1 queue_samples=0 queue_missing=1 queue_invalid=0",
        )
        records = (
            "TIMING: id=1 work_ms_p50=1 work_ms_p95=1 work_ms_p99=1 work_ms_max=1 "
            "wall_ms_p50=1 wall_ms_p95=1 wall_ms_p99=1 wall_ms_max=1 work_over_30hz=0 "
            "work_max_streak=0 wall_over_30hz=0 wall_max_streak=0 vblank_callbacks=0 vblank_max_per_frame=0\n"
            "FRAME PROFILE STAGES: id=1 mean_ms services=0 input=0 simulation=0 audio=0 render=1 tail=0 pacing=0 gap=0\n"
            "FRAME PROFILE GPU WAIT: id=1 unavailable\n"
            "FRAME PROFILE GPU: id=1 previous_queue=unavailable"
        )
        capture = replace_once(capture, "TIMING: id=1 no valid samples", records)
        parsed = parse(capture)
        self.assertIsNone(parsed.windows[0].queue_ms)
        self.assertEqual(parsed.windows[0].metadata_missing, 1)

    def test_command_exit_codes_do_not_claim_presentation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.log"
            path.write_text(CAPTURE, encoding="utf-8")
            output, errors = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                self.assertEqual(report.main([str(path)]), 1)
                self.assertEqual(report.main([str(path), "--window", "2"]), 0)
                self.assertEqual(report.main([str(path), "--window", "3"]), 2)
            self.assertIn("scope: window 2 only", output.getvalue())
            self.assertIn("Presentation: unverified", output.getvalue())
            self.assertIn("work_ms_p99=34.000", output.getvalue())
            self.assertIn("work_ms_max=34.000", output.getvalue())
            self.assertIn("work_over_30hz=1 work_max_streak=1", output.getvalue())
            self.assertIn("streaks can start in an earlier window", output.getvalue())
            self.assertIn("not missed-presentation counts", output.getvalue())
            self.assertIn("window 3 does not exist", errors.getvalue())
            path.write_text(CAPTURE.rstrip("\n"), encoding="utf-8")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(report.main([str(path)]), 2)
            path.write_text(
                "FRAME PROFILE START: version=1 window=300 tick_hz=3000 presentation=unverified\n"
                "FRAME PROFILE END: windows=0 incomplete_windows=0 errors=0 aborted=0 user_stops=0 "
                "loading_overlaps=0 presentation=unverified\n"
                "LOG QUEUE: dropped=0 io_errors=0; routine writes ran off the frame thread\n",
                encoding="utf-8",
            )
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(report.main([str(path)]), 1)

    @unittest.skipUnless(shutil.which("gcc") or shutil.which("clang"), "host C compiler not installed")
    def test_real_profiler_emitter_is_accepted(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / ("frame-profile.exe" if sys.platform == "win32" else "frame-profile")
            subprocess.run(
                [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Wformat=2", "-Werror",
                 str(N3DS / "tests" / "host" / "engine_frame_profile_tests.c"), "-o", str(executable)],
                check=True, capture_output=True, text=True,
            )
            captures = {}
            for name in ("fixture", "context-fixture", "anomaly-fixture", "aborted-fixture",
                         "stop-fixture", "invalid-fixture"):
                with self.subTest(fixture=name):
                    result = subprocess.run([str(executable), "--emit-" + name], check=True,
                                            capture_output=True, text=True)
                    captures[name] = parse(result.stdout)
        capture = captures["fixture"]
        self.assertEqual([window.samples for window in capture.windows], [300, 1])
        self.assertEqual([window.status for window in capture.windows], ["incomplete", "complete"])
        self.assertEqual((len(captures["context-fixture"].windows), captures["context-fixture"].loading_overlaps), (4, 1))
        self.assertEqual(captures["anomaly-fixture"].windows[0].gpu_begin_anomalies, 1)
        self.assertEqual(captures["aborted-fixture"].aborted, 1)
        self.assertEqual(captures["stop-fixture"].user_stops, 1)
        self.assertEqual(captures["invalid-fixture"].windows[0].invalid, 1)


if __name__ == "__main__":
    unittest.main()
