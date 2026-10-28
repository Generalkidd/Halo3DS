import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest


N3DS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(N3DS / "tools"))
import input_replay as replay


CAPTURE = """\
FRONTEND: original menu lifecycle startup
FRONTEND INPUT: frame=1 held=40000000 circle=-156,156 look=-32768,32767 enabled=1 cinematic=0 skip=0 touch=0,0
FRONTEND INPUT: frame=2 held=00100400 circle=1,-2 look=3,-4 enabled=0 cinematic=1 skip=0 touch=319,239
FRONTEND INPUT: frame=4 held=40000000 circle=0,0 look=0,0 enabled=1 cinematic=0 skip=0 touch=65535,65535
FRONTEND INPUT: frame=31 held=40000000 circle=0,0 look=0,0 enabled=1 cinematic=0 skip=0 touch=65535,65535
FRONTEND END: frames=32 menu=0 time=19
RESULT: PASS
LOG QUEUE: dropped=0 io_errors=0; routine writes ran off the frame thread
"""


def with_checksum(data):
    data = bytearray(data)
    struct.pack_into("<I", data, 20, replay.checksum(data, data[replay.HEADER.size:]))
    return bytes(data)


class InputReplayTests(unittest.TestCase):
    def setUp(self):
        self.capture = replay.inspect_capture(CAPTURE)
        self.samples = replay.expand_samples(self.capture, 32)
        self.fixture = replay.encode_fixture(self.samples, 0x12345678)

    def completed_log(self):
        seed, checksum, samples = replay.decode_fixture(self.fixture)
        start = (f"INPUT REPLAY START: version=1 frames={len(samples)} "
                 f"seed={seed:08x} checksum={checksum:08x}\n")
        end = f"INPUT REPLAY END: frames={len(samples)} expected={len(samples)} status=complete\n"
        return CAPTURE.replace("FRONTEND INPUT: frame=1", start + "FRONTEND INPUT: frame=1", 1).replace(
            "FRONTEND END:", end + "FRONTEND END:", 1
        )

    def test_expands_changes_and_preserves_every_field(self):
        self.assertEqual(self.samples[0], (0x40000000, -156, 156, -32768, 32767, 0, 0))
        self.assertEqual(self.samples[1], (0x00100400, 1, -2, 3, -4, 319, 239))
        self.assertEqual(self.samples[1], self.samples[2])
        self.assertEqual(self.samples[3], self.samples[30])
        self.assertEqual(self.samples[30], self.samples[31])
        seed, _, decoded = replay.decode_fixture(self.fixture)
        self.assertEqual(seed, 0x12345678)
        self.assertEqual(decoded, self.samples)
        self.assertEqual(len(self.fixture), replay.HEADER.size + 32 * replay.SAMPLE.size)
        self.assertEqual(replay.encode_fixture(self.samples, seed), self.fixture)

    def test_capture_lifecycle_loss_and_missing_heartbeats(self):
        cases = {
            "start": CAPTURE.replace("FRONTEND: original menu lifecycle startup\n", ""),
            "end": CAPTURE.replace("FRONTEND END:", "NOT AN END:"),
            "result": CAPTURE.replace("RESULT: PASS", "RESULT: FAIL"),
            "footer": CAPTURE.replace("LOG QUEUE: dropped=", "NOT A FOOTER: dropped="),
            "truncated": CAPTURE.rstrip("\n"),
            "dropped": CAPTURE.replace("dropped=0", "dropped=1"),
            "io errors": CAPTURE.replace("io_errors=0", "io_errors=1"),
            "duplicate footer": CAPTURE + CAPTURE.splitlines(keepends=True)[-1],
            "record after footer": CAPTURE + "extra record\n",
            "missing first frame": CAPTURE.replace("frame=1 held=", "frame=3 held=", 1),
            "missing heartbeat": CAPTURE.replace("frame=31 held=", "frame=30 held=", 1),
            "duplicate frame": CAPTURE.replace("frame=4 held=", "frame=2 held=", 1),
            "past end": CAPTURE.replace("frame=31 held=", "frame=33 held=", 1),
            "malformed": CAPTURE.replace("circle=-156,156", "circle=bad"),
        }
        for name, capture in cases.items():
            with self.subTest(name=name), self.assertRaises(replay.ReplayError):
                replay.inspect_capture(capture)

    def test_input_bounds_and_stop_chord_are_rejected(self):
        for bad in (
            CAPTURE.replace("circle=-156,156", "circle=32768,156"),
            CAPTURE.replace("touch=319,239", "touch=320,239"),
            CAPTURE.replace("touch=319,239", "touch=319,240"),
            CAPTURE.replace("held=00100400", "held=0000000c"),
            CAPTURE.replace("touch=0,0", "touch=65536,0", 1),
        ):
            with self.subTest(capture=bad), self.assertRaises(replay.ReplayError):
                replay.inspect_capture(bad)
        for count in (0, 33, replay.MAX_FRAMES + 1):
            with self.subTest(count=count), self.assertRaises(replay.ReplayError):
                replay.expand_samples(self.capture, count)
        with self.assertRaises(replay.ReplayError):
            replay.encode_fixture(self.samples, 0x100000000)

    def test_exact_maximum_frame_count(self):
        samples = [self.samples[0]] * replay.MAX_FRAMES
        data = replay.encode_fixture(samples, 0xFFFFFFFF)
        self.assertEqual(len(replay.decode_fixture(data)[2]), replay.MAX_FRAMES)
        self.assertEqual(len(data), replay.HEADER.size + replay.MAX_FRAMES * replay.SAMPLE.size)
        with self.assertRaises(replay.ReplayError):
            replay.encode_fixture(samples + samples[:1], 0)
        with self.assertRaises(replay.ReplayError):
            replay.encode_fixture([], 0)

    def test_corrupt_truncated_and_unsupported_fixtures(self):
        cases = [
            self.fixture[:20],
            self.fixture[:-1],
            self.fixture + b"\x00",
            b"BAD!" + self.fixture[4:],
            self.fixture[:4] + struct.pack("<I", 2) + self.fixture[8:],
            self.fixture[:8] + struct.pack("<I", 0) + self.fixture[12:],
            self.fixture[:8] + struct.pack("<I", replay.MAX_FRAMES + 1) + self.fixture[12:],
            self.fixture[:16] + struct.pack("<I", 1) + self.fixture[20:],
            self.fixture[:24] + bytes([self.fixture[24] ^ 1]) + self.fixture[25:],
        ]
        bad_touch = bytearray(self.fixture)
        struct.pack_into("<H", bad_touch, replay.HEADER.size + replay.SAMPLE.size + 12, 320)
        cases.append(with_checksum(bad_touch))
        bad_stop = bytearray(self.fixture)
        struct.pack_into("<I", bad_stop, replay.HEADER.size, replay.START_SELECT)
        cases.append(with_checksum(bad_stop))
        for data in cases:
            with self.subTest(length=len(data)), self.assertRaises(replay.ReplayError):
                replay.decode_fixture(data)

    def test_completed_replay_lifecycle_and_exact_inputs(self):
        log = self.completed_log()
        self.assertEqual(replay.check_capture(self.fixture, replay.inspect_capture(log)), 32)
        changed_metadata = log.replace("enabled=0 cinematic=1", "enabled=1 cinematic=0")
        self.assertEqual(replay.check_capture(self.fixture, replay.inspect_capture(changed_metadata)), 32)
        cases = {
            "no replay start": log.replace("INPUT REPLAY START:", "NOT A REPLAY START:"),
            "no replay end": log.replace("INPUT REPLAY END:", "NOT A REPLAY END:"),
            "wrong version": log.replace("version=1", "version=2"),
            "wrong start count": log.replace("frames=32 seed=", "frames=31 seed="),
            "wrong seed": log.replace("seed=12345678", "seed=12345679"),
            "wrong checksum": log.replace("checksum=", "checksum=0", 1),
            "user stop": log.replace("status=complete", "status=user-stop"),
            "wrong count": log.replace("expected=32", "expected=31"),
            "different input": log.replace("circle=1,-2", "circle=2,-2"),
            "after end": log.replace("FRONTEND END:", CAPTURE.splitlines()[1] + "\nFRONTEND END:"),
        }
        for name, value in cases.items():
            with self.subTest(name=name), self.assertRaises(replay.ReplayError):
                replay.check_capture(self.fixture, replay.inspect_capture(value))

    def test_cli_creates_new_file_and_does_not_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log, output = root / "input.log", root / "input.replay"
            log.write_text(CAPTURE, encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(replay.main(["create", str(log), str(output), "--frames", "4"]), 0)
            self.assertEqual(len(replay.decode_fixture(output.read_bytes())[2]), 4)
            original = output.read_bytes()
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(replay.main(["create", str(log), str(output)]), 2)
            self.assertEqual(output.read_bytes(), original)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(replay.main(["create", str(root / "missing"), str(root / "other")]), 2)
            output.write_bytes(b"\0" * (replay.HEADER.size + replay.MAX_FRAMES * replay.SAMPLE.size + 1))
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(replay.main(["check", str(output), str(log)]), 2)


if __name__ == "__main__":
    unittest.main()
