"""Create and verify private, frame-indexed input replays. Python 3.10+."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys


MAGIC = b"H3IR"
VERSION = 1
MAX_FRAMES = 12000
DEFAULT_SEED = 0x48414C4F
HEADER = struct.Struct("<4sIIIII")
SAMPLE = struct.Struct("<IhhhhHH")
START_SELECT = (1 << 2) | (1 << 3)
TOUCH = 1 << 20

INPUT = re.compile(
    r"FRONTEND INPUT: frame=(\d+) held=([0-9a-fA-F]{8}) "
    r"circle=(-?\d+),(-?\d+) look=(-?\d+),(-?\d+) "
    r"enabled=-?\d+ cinematic=-?\d+ skip=-?\d+ touch=(\d+),(\d+)"
)
END = re.compile(r"FRONTEND END: frames=(\d+) menu=-?\d+ time=-?\d+")
FOOTER = re.compile(r"LOG QUEUE: dropped=(\d+) io_errors=(\d+);.*")
REPLAY_START = re.compile(
    r"INPUT REPLAY START: version=(\d+) frames=(\d+) seed=([0-9a-fA-F]{8}) "
    r"checksum=([0-9a-fA-F]{8})"
)
REPLAY_END = re.compile(
    r"INPUT REPLAY END: frames=(\d+) expected=(\d+) status=([a-z-]+)"
)


class ReplayError(ValueError):
    pass


def checksum(header, payload):
    value = 2166136261
    for data in (header[:20], payload):
        for byte in data:
            value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def validate_sample(sample):
    held, cx, cy, sx, sy, tx, ty = sample
    if not 0 <= held <= 0xFFFFFFFF:
        raise ReplayError("held buttons exceed 32 bits")
    if any(not -32768 <= value <= 32767 for value in (cx, cy, sx, sy)):
        raise ReplayError("analog input exceeds the signed 16-bit range")
    if not 0 <= tx <= 65535 or not 0 <= ty <= 65535:
        raise ReplayError("touch coordinates exceed the unsigned 16-bit range")
    if held & TOUCH and (tx >= 320 or ty >= 240):
        raise ReplayError("active touch is outside the 320x240 screen")
    if held & START_SELECT == START_SELECT:
        raise ReplayError("a recorded sample contains the physical stop chord")


def inspect_capture(text):
    if not text.endswith("\n"):
        raise ReplayError("capture is empty or ends with a truncated line")
    lines = text.splitlines()
    start = end = result = footer = None
    replay_start = replay_end = None
    events = []
    for index, line in enumerate(lines):
        if line == "FRONTEND: original menu lifecycle startup":
            if start is not None or end is not None:
                raise ReplayError("duplicate or misplaced frontend start")
            start = index
        elif line.startswith("FRONTEND INPUT:"):
            match = INPUT.fullmatch(line)
            if not match or start is None or end is not None or replay_end is not None:
                raise ReplayError("malformed or misplaced input record")
            frame = int(match.group(1))
            sample = (int(match.group(2), 16), *(int(match.group(i)) for i in range(3, 9)))
            validate_sample(sample)
            if frame < 1 or (events and frame <= events[-1][0]):
                raise ReplayError("input frames must be strictly increasing and start at frame 1")
            events.append((frame, sample))
        elif line.startswith("INPUT REPLAY START:"):
            match = REPLAY_START.fullmatch(line)
            if not match or start is None or events or replay_start is not None or end is not None:
                raise ReplayError("malformed or misplaced replay start")
            replay_start = (int(match.group(1)), int(match.group(2)),
                            int(match.group(3), 16), int(match.group(4), 16))
        elif line.startswith("INPUT REPLAY END:"):
            match = REPLAY_END.fullmatch(line)
            if not match or replay_start is None or replay_end is not None or end is not None:
                raise ReplayError("malformed or misplaced replay end")
            replay_end = (int(match.group(1)), int(match.group(2)), match.group(3))
        elif line.startswith("FRONTEND END:"):
            match = END.fullmatch(line)
            if not match or start is None or end is not None:
                raise ReplayError("malformed or duplicate frontend end")
            end = (index, int(match.group(1)))
        elif line.startswith("RESULT:"):
            if line != "RESULT: PASS" or end is None or result is not None:
                raise ReplayError("missing, failed, duplicate, or misplaced result")
            result = index
        elif line.startswith("LOG QUEUE: dropped="):
            match = FOOTER.fullmatch(line)
            if not match or result is None or footer is not None:
                raise ReplayError("malformed, duplicate, or misplaced log footer")
            if int(match.group(1)) or int(match.group(2)):
                raise ReplayError("capture reports dropped logs or I/O errors")
            footer = index
    if start is None or end is None or result is None or footer is None:
        raise ReplayError("capture is missing its frontend lifecycle, result, or log footer")
    if any(line.strip() for line in lines[footer + 1:]):
        raise ReplayError("records follow the final log footer")
    frames = end[1]
    if not frames or not events or events[0][0] != 1 or events[-1][0] > frames:
        raise ReplayError("capture has incomplete or out-of-range input frames")
    heartbeat_count = sum((frame - 1) % 30 == 0 for frame, _ in events)
    if heartbeat_count != (frames - 1) // 30 + 1:
        raise ReplayError("capture is missing a periodic input record")
    if (replay_start is None) != (replay_end is None):
        raise ReplayError("capture has an incomplete replay lifecycle")
    if replay_start is not None and (
        replay_start[0] != VERSION or replay_start[1] != frames or
        replay_end != (frames, frames, "complete")
    ):
        raise ReplayError("source replay has an unsupported or incomplete lifecycle")
    return frames, events, replay_start, replay_end


def expand_samples(capture, frames):
    total, events, _, _ = capture
    if not 1 <= frames <= MAX_FRAMES or frames > total:
        raise ReplayError(f"replay frames must be between 1 and {min(total, MAX_FRAMES)}")
    samples = []
    event_index = 0
    for frame in range(1, frames + 1):
        if event_index < len(events) and events[event_index][0] == frame:
            sample = events[event_index][1]
            event_index += 1
        samples.append(sample)
    return samples


def encode_fixture(samples, seed):
    if not 1 <= len(samples) <= MAX_FRAMES:
        raise ReplayError(f"replay must contain 1 to {MAX_FRAMES} frames")
    if not 0 <= seed <= 0xFFFFFFFF:
        raise ReplayError("seed must be an unsigned 32-bit integer")
    for sample in samples:
        validate_sample(sample)
    payload = b"".join(SAMPLE.pack(*sample) for sample in samples)
    header = HEADER.pack(MAGIC, VERSION, len(samples), seed, 0, 0)
    return HEADER.pack(MAGIC, VERSION, len(samples), seed, 0, checksum(header, payload)) + payload


def decode_fixture(data):
    if len(data) < HEADER.size:
        raise ReplayError("fixture header is truncated")
    magic, version, count, seed, reserved, expected = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION or reserved:
        raise ReplayError("invalid or unsupported fixture header")
    if not 1 <= count <= MAX_FRAMES or len(data) != HEADER.size + count * SAMPLE.size:
        raise ReplayError("invalid fixture frame count or length")
    payload = data[HEADER.size:]
    if checksum(data, payload) != expected:
        raise ReplayError("fixture checksum mismatch")
    samples = list(SAMPLE.iter_unpack(payload))
    for sample in samples:
        validate_sample(sample)
    return seed, expected, samples


def read_fixture(path):
    maximum = HEADER.size + MAX_FRAMES * SAMPLE.size
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        raise ReplayError("fixture exceeds the maximum supported size")
    return data


def check_capture(data, capture):
    seed, expected, samples = decode_fixture(data)
    frames, _, replay_start, replay_end = capture
    count = len(samples)
    if replay_start != (VERSION, count, seed, expected) or replay_end != (count, count, "complete"):
        raise ReplayError("capture does not identify a completed run of this fixture")
    if frames != count:
        raise ReplayError("frontend and fixture frame counts differ")
    for frame, (actual, wanted) in enumerate(zip(expand_samples(capture, count), samples), 1):
        if actual != wanted:
            raise ReplayError(f"input differs from the fixture at frame {frame}")
    return count


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="convert a complete private engine log")
    create.add_argument("log", type=Path)
    create.add_argument("output", type=Path, help="new fixture file; existing files are not overwritten")
    create.add_argument("--frames", type=int, help="replay only this many initial frames")
    create.add_argument("--seed", type=lambda value: int(value, 0), default=DEFAULT_SEED)
    check = commands.add_parser("check", help="verify replay lifecycle and every recorded input")
    check.add_argument("fixture", type=Path)
    check.add_argument("log", type=Path)
    args = parser.parse_args(argv)
    try:
        capture = inspect_capture(args.log.read_text(encoding="utf-8"))
        if args.command == "create":
            frames = args.frames if args.frames is not None else capture[0]
            data = encode_fixture(expand_samples(capture, frames), args.seed)
            with args.output.open("xb") as stream:
                stream.write(data)
            print(f"Created {args.output}: {frames} frames, seed={args.seed:08x}, "
                  f"sha256={hashlib.sha256(data).hexdigest()}")
        else:
            data = read_fixture(args.fixture)
            frames = check_capture(data, capture)
            print(f"Verified {frames} frame inputs and replay lifecycle; gameplay state is not assessed.")
    except (OSError, UnicodeError, ReplayError) as error:
        print(f"INPUT REPLAY ERROR: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
