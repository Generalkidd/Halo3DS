"""Validate frame-profile logs without treating timing as presentation evidence."""
import argparse
from dataclasses import dataclass, field
import math
from pathlib import Path
import sys
from typing import Iterable


class CaptureError(ValueError):
    """A capture cannot be interpreted reliably."""


@dataclass(frozen=True)
class Distribution:
    p50: float
    p95: float
    p99: float
    maximum: float


@dataclass
class Window:
    number: int
    scene: str
    map_name: str
    model: str
    stereo: int
    quality: int
    first: int
    last: int
    samples: int
    invalid: int
    dropped: int
    metadata_missing: int
    gpu_begins: int
    gpu_begin_anomalies: int
    gpu_submissions: int
    gpu_submission_anomalies: int
    queue_samples: int
    queue_missing: int
    queue_invalid: int
    status: str
    line: int
    records: list[str] = field(default_factory=list)
    work_ms: Distribution | None = None
    wall_ms: Distribution | None = None
    wait_ms: Distribution | None = None
    queue_ms: Distribution | None = None
    work_over: int | None = None
    work_streak: int | None = None
    wall_over: int | None = None
    wall_streak: int | None = None
    vblank_callbacks: int | None = None
    vblank_max: int | None = None


@dataclass(frozen=True)
class Capture:
    windows: list[Window]
    loading_overlaps: int
    aborted: int
    user_stops: int


def _fail(line: int, message: str) -> None:
    raise CaptureError(f"line {line}: {message}")


def _fields(text: str, expected: set[str], line: int) -> dict[str, str]:
    result: dict[str, str] = {}
    for token in text.split():
        if "=" not in token:
            _fail(line, f"unexpected token {token!r}")
        key, value = token.split("=", 1)
        if key in result:
            _fail(line, f"duplicate field {key!r}")
        result[key] = value
    missing, extra = expected - result.keys(), result.keys() - expected
    if missing or extra:
        _fail(line, f"invalid fields; missing={sorted(missing)}, unexpected={sorted(extra)}")
    return result


def _uint(values: dict[str, str], key: str, line: int) -> int:
    value = values[key]
    if not value.isascii() or not value.isdecimal():
        _fail(line, f"{key} must be a nonnegative integer")
    try:
        return int(value)
    except ValueError as error:
        raise CaptureError(f"line {line}: {key} is too large to parse") from error


def _number(values: dict[str, str], key: str, line: int) -> float:
    try:
        value = float(values[key])
    except ValueError as error:
        raise CaptureError(f"line {line}: {key} must be a finite nonnegative number") from error
    if not math.isfinite(value) or value < 0:
        _fail(line, f"{key} must be a finite nonnegative number")
    return value


def _distribution(values: dict[str, str], keys: tuple[str, str, str, str], line: int) -> Distribution:
    numbers = [_number(values, key, line) for key in keys]
    if numbers != sorted(numbers):
        _fail(line, f"percentiles are not ordered for {keys[0]}")
    return Distribution(*numbers)


def _new_window(text: str, line: int, expected_id: int, expected_frame: int, capacity: int) -> Window:
    keys = {
        "id", "scene", "map", "model", "stereo", "quality", "first", "last", "samples",
        "invalid", "dropped", "metadata_missing", "gpu_begins", "gpu_begins_anomalous",
        "gpu_submissions", "gpu_submissions_anomalous", "queue_samples", "queue_missing",
        "queue_invalid", "status", "presentation",
    }
    values = _fields(text, keys, line)
    number = _uint(values, "id", line)
    if number != expected_id:
        _fail(line, f"expected window {expected_id}, got {number}")
    if values["scene"] not in {"menu", "gameplay", "gameplay-ui", "cinematic"}:
        _fail(line, "unknown scene")
    if values["model"] not in {"original", "new"} or values["presentation"] != "unverified":
        _fail(line, "unknown model or presentation status")
    stereo = _uint(values, "stereo", line)
    quality = _uint(values, "quality", line)
    first, last = _uint(values, "first", line), _uint(values, "last", line)
    samples, invalid, dropped = (_uint(values, key, line) for key in ("samples", "invalid", "dropped"))
    if stereo > 1 or first != expected_frame or last < first:
        _fail(line, f"invalid stereo or frame range; expected frame {expected_frame}")
    if last - first + 1 != samples + invalid + dropped or not 1 <= samples + invalid + dropped <= capacity:
        _fail(line, "frame range and sample counts do not agree")
    metadata_missing = _uint(values, "metadata_missing", line)
    gpu_begins = _uint(values, "gpu_begins", line)
    gpu_begin_anomalies = _uint(values, "gpu_begins_anomalous", line)
    gpu_submissions = _uint(values, "gpu_submissions", line)
    gpu_submission_anomalies = _uint(values, "gpu_submissions_anomalous", line)
    queue_samples, queue_missing, queue_invalid = (
        _uint(values, key, line) for key in ("queue_samples", "queue_missing", "queue_invalid")
    )
    if metadata_missing not in (0, 1) or metadata_missing != int(values["map"] in ("", "none")):
        _fail(line, "map metadata status is inconsistent")
    if gpu_begin_anomalies > samples or gpu_submission_anomalies > samples:
        _fail(line, "GPU anomaly count exceeds sample count")
    if gpu_begins < samples - gpu_begin_anomalies or gpu_submissions < samples - gpu_submission_anomalies:
        _fail(line, "GPU counts cannot account for the reported samples")
    if not gpu_begin_anomalies and gpu_begins != samples:
        _fail(line, "GPU begin count is inconsistent")
    if not gpu_submission_anomalies and gpu_submissions != samples:
        _fail(line, "GPU submission count is inconsistent")
    if queue_samples + queue_missing + queue_invalid != samples:
        _fail(line, "GPU queue sample counts do not agree")
    if queue_missing < gpu_begin_anomalies:
        _fail(line, "GPU begin anomalies must have unavailable queue samples")
    complete = not (invalid or dropped or metadata_missing or gpu_begin_anomalies or
                    gpu_submission_anomalies or queue_missing or queue_invalid)
    if values["status"] != ("complete" if complete else "incomplete"):
        _fail(line, "window status disagrees with its sample counts")
    return Window(number, values["scene"], values["map"], values["model"], stereo, quality,
                  first, last, samples, invalid, dropped, metadata_missing, gpu_begins,
                  gpu_begin_anomalies, gpu_submissions, gpu_submission_anomalies,
                  queue_samples, queue_missing, queue_invalid, values["status"], line)


def _window_record(window: Window, kind: str, text: str, line: int) -> None:
    expected_order = ["timing"] if not window.samples else ["timing", "stages", "wait", "gpu"]
    if len(window.records) >= len(expected_order) or kind != expected_order[len(window.records)]:
        _fail(line, f"unexpected {kind} record for window {window.number}")
    if kind == "timing" and not window.samples:
        prefix, separator, suffix = text.partition(" ")
        values = _fields(prefix, {"id"}, line)
        if not separator or suffix != "no valid samples":
            _fail(line, "expected no-valid-samples timing record")
    elif kind == "timing":
        keys = {
            "id", "work_ms_p50", "work_ms_p95", "work_ms_p99", "work_ms_max",
            "wall_ms_p50", "wall_ms_p95", "wall_ms_p99", "wall_ms_max", "work_over_30hz",
            "work_max_streak", "wall_over_30hz", "wall_max_streak", "vblank_callbacks", "vblank_max_per_frame",
        }
        values = _fields(text, keys, line)
        work = _distribution(values, ("work_ms_p50", "work_ms_p95", "work_ms_p99", "work_ms_max"), line)
        wall = _distribution(values, ("wall_ms_p50", "wall_ms_p95", "wall_ms_p99", "wall_ms_max"), line)
        if any(wall_value < work_value for work_value, wall_value in zip(
                (work.p50, work.p95, work.p99, work.maximum),
                (wall.p50, wall.p95, wall.p99, wall.maximum))):
            _fail(line, "wall-time distribution is shorter than work time")
        work_over, wall_over = _uint(values, "work_over_30hz", line), _uint(values, "wall_over_30hz", line)
        if work_over > wall_over or wall_over > window.samples:
            _fail(line, "over-budget counts are inconsistent")
        work_streak, wall_streak = _uint(values, "work_max_streak", line), _uint(values, "wall_max_streak", line)
        for count, streak, key in ((work_over, work_streak, "work_max_streak"), (wall_over, wall_streak, "wall_max_streak")):
            if bool(count) != bool(streak):
                _fail(line, f"{key} disagrees with its count")
        vblank_callbacks = _uint(values, "vblank_callbacks", line)
        vblank_max = _uint(values, "vblank_max_per_frame", line)
        if vblank_max > vblank_callbacks:
            _fail(line, "VBlank callback counts are inconsistent")
        window.work_ms, window.wall_ms = work, wall
        window.work_over, window.work_streak = work_over, work_streak
        window.wall_over, window.wall_streak = wall_over, wall_streak
        window.vblank_callbacks, window.vblank_max = vblank_callbacks, vblank_max
    elif kind == "stages":
        prefix, separator, rest = text.partition(" mean_ms ")
        if not separator:
            _fail(line, "missing stage timing label")
        values = _fields(prefix + " " + rest, {
            "id", "services", "input", "simulation", "audio", "render", "tail", "pacing", "gap"
        }, line)
        for key in ("services", "input", "simulation", "audio", "render", "tail", "pacing", "gap"):
            _number(values, key, line)
    elif kind == "wait":
        wait_samples = window.samples - window.gpu_begin_anomalies
        if not wait_samples:
            prefix, separator, suffix = text.partition(" ")
            values = _fields(prefix, {"id"}, line)
            if not separator or suffix != "unavailable":
                _fail(line, "expected unavailable GPU wait")
        else:
            values = _fields(text, {"id", "samples", "mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms"}, line)
            if _uint(values, "samples", line) != wait_samples:
                _fail(line, "GPU wait sample count is inconsistent")
            _number(values, "mean_ms", line)
            window.wait_ms = _distribution(values, ("p50_ms", "p95_ms", "p99_ms", "max_ms"), line)
    else:
        if not window.queue_samples:
            values = _fields(text, {"id", "previous_queue"}, line)
            if values["previous_queue"] != "unavailable":
                _fail(line, "expected unavailable GPU queue")
        else:
            metrics, separator, note = text.partition("; ")
            if not separator or note != "overlaps CPU and includes transfers":
                _fail(line, "missing GPU queue timing note")
            values = _fields(metrics, {
                "id", "samples", "previous_queue_mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms"
            }, line)
            if _uint(values, "samples", line) != window.queue_samples:
                _fail(line, "GPU queue sample count is inconsistent")
            _number(values, "previous_queue_mean_ms", line)
            window.queue_ms = _distribution(values, ("p50_ms", "p95_ms", "p99_ms", "max_ms"), line)
    if _uint(values, "id", line) != window.number:
        _fail(line, f"record belongs to a different window than {window.number}")
    window.records.append(kind)


def _finish_window(window: Window) -> None:
    expected = ["timing"] if not window.samples else ["timing", "stages", "wait", "gpu"]
    if window.records != expected:
        _fail(window.line, f"window {window.number} is missing or has reordered records")


def inspect_capture(lines: Iterable[str]) -> Capture:
    windows: list[Window] = []
    current: Window | None = None
    started = ended = footer = False
    capacity = expected_frame = 0
    loading = aborted = stops = errors = 0
    end_values: dict[str, str] | None = None
    footer_values: dict[str, str] | None = None
    for line, raw in enumerate(lines, 1):
        text = raw.rstrip("\r\n")
        recognized = text.startswith("FRAME PROFILE ") or text.startswith("LOG QUEUE: dropped")
        if not recognized:
            continue
        if not raw.endswith(("\n", "\r")):
            _fail(line, "truncated record without a line ending")
        if text.startswith("LOG QUEUE: dropped"):
            if footer or not ended:
                _fail(line, "duplicate or premature log-queue footer")
            metrics, separator, note = text[len("LOG QUEUE: "):].partition("; ")
            if not separator or note != "routine writes ran off the frame thread":
                _fail(line, "invalid log-queue footer")
            footer_values = _fields(metrics, {"dropped", "io_errors"}, line)
            footer = True
            continue
        if ended:
            _fail(line, "profile record after the end marker")
        if text.startswith("FRAME PROFILE ERROR: "):
            errors += 1
            continue
        if text.startswith("FRAME PROFILE START: "):
            if started:
                _fail(line, "duplicate profile start")
            values = _fields(text[len("FRAME PROFILE START: "):], {"version", "window", "tick_hz", "presentation"}, line)
            if _uint(values, "version", line) != 1 or values["presentation"] != "unverified":
                _fail(line, "unsupported profile version or presentation status")
            capacity = _uint(values, "window", line)
            if not capacity or not _uint(values, "tick_hz", line):
                _fail(line, "invalid window capacity or tick frequency")
            started, expected_frame = True, 1
            continue
        if not started:
            _fail(line, "profile record before the start marker")
        if text.startswith("FRAME PROFILE STOP: "):
            if text != "FRAME PROFILE STOP: user stopped before rendering; partial iteration excluded":
                _fail(line, "invalid user-stop record")
            stops += 1
            continue
        if text.startswith("FRAME PROFILE ABORTED: "):
            if text != "FRAME PROFILE ABORTED: loop ended before this frame was rendered":
                _fail(line, "invalid aborted-frame record")
            aborted += 1
            continue
        if text.startswith("FRAME PROFILE WINDOW: "):
            if current is not None:
                _finish_window(current)
            current = _new_window(text[len("FRAME PROFILE WINDOW: "):], line, len(windows) + 1, expected_frame, capacity)
            windows.append(current)
            expected_frame = current.last + 1
            continue
        if text.startswith("FRAME PROFILE EXCLUDED: "):
            if current is not None:
                _finish_window(current)
                current = None
            metrics, separator, note = text[len("FRAME PROFILE EXCLUDED: "):].partition("; ")
            if not separator or note != "mixed loading and active work":
                _fail(line, "invalid loading exclusion")
            values = _fields(metrics, {"frame", "loading_submissions"}, line)
            if _uint(values, "frame", line) != expected_frame or not _uint(values, "loading_submissions", line):
                _fail(line, f"invalid loading exclusion; expected frame {expected_frame}")
            expected_frame += 1
            loading += 1
            continue
        if text.startswith("FRAME PROFILE END: "):
            if current is not None:
                _finish_window(current)
                current = None
            end_values = _fields(text[len("FRAME PROFILE END: "):], {
                "windows", "incomplete_windows", "errors", "aborted", "user_stops", "loading_overlaps", "presentation"
            }, line)
            ended = True
            continue
        prefixes = (
            ("FRAME PROFILE TIMING: ", "timing"), ("FRAME PROFILE STAGES: ", "stages"),
            ("FRAME PROFILE GPU WAIT: ", "wait"), ("FRAME PROFILE GPU: ", "gpu"),
        )
        for prefix, kind in prefixes:
            if text.startswith(prefix):
                if current is None:
                    _fail(line, f"{kind} record without a window")
                _window_record(current, kind, text[len(prefix):], line)
                break
        else:
            _fail(line, "unknown profile record")

    if not started or not ended or not footer or end_values is None or footer_values is None:
        raise CaptureError("missing profile start, profile end, or final log-queue footer")
    if end_values["presentation"] != "unverified":
        raise CaptureError("end marker does not report unverified presentation")
    if _uint(end_values, "windows", line) != len(windows):
        raise CaptureError("end marker window count does not match the records")
    if _uint(end_values, "incomplete_windows", line) != sum(w.status == "incomplete" for w in windows):
        raise CaptureError("end marker incomplete-window count does not match the records")
    if (_uint(end_values, "loading_overlaps", line) != loading or
        _uint(end_values, "aborted", line) != aborted or _uint(end_values, "user_stops", line) != stops):
        raise CaptureError("end marker does not match loading, aborted, or user-stop records")
    if errors or _uint(end_values, "errors", line):
        raise CaptureError("profiler reported an error")
    dropped_logs, io_errors = _uint(footer_values, "dropped", line), _uint(footer_values, "io_errors", line)
    if dropped_logs or io_errors:
        raise CaptureError(f"log queue reported dropped={dropped_logs} io_errors={io_errors}")
    return Capture(windows, loading, aborted, stops)


def _format_distribution(label: str, values: Distribution | None) -> str:
    if values is None:
        return f"{label}=unavailable"
    return (f"{label}_p50={values.p50:.3f} {label}_p95={values.p95:.3f} "
            f"{label}_p99={values.p99:.3f} {label}_max={values.maximum:.3f}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="engine log path, or - for standard input")
    parser.add_argument("--window", type=int, help="report one window only, without assessing the rest of the run")
    args = parser.parse_args(argv)
    try:
        if args.log == "-":
            capture = inspect_capture(sys.stdin)
        else:
            with Path(args.log).open("r", encoding="utf-8") as source:
                capture = inspect_capture(source)
        if args.window is not None:
            if args.window < 1 or args.window > len(capture.windows):
                raise CaptureError(f"window {args.window} does not exist")
            selected = [capture.windows[args.window - 1]]
        else:
            selected = capture.windows
    except (CaptureError, OSError, UnicodeError) as error:
        print(f"INVALID PROFILE LOG: {error}", file=sys.stderr)
        return 2
    scope = f"window {args.window} only" if args.window is not None else "entire recorded run"
    complete = bool(selected) and all(window.status == "complete" for window in selected)
    if args.window is None and (capture.loading_overlaps or capture.aborted):
        complete = False
    print(f"Log integrity: no reported loss; scope: {scope}")
    print(f"CPU/GPU sample coverage: {'complete' if complete else 'incomplete'}; "
          f"windows={len(capture.windows)} loading_overlaps={capture.loading_overlaps} "
          f"aborted={capture.aborted} user_stops={capture.user_stops}")
    for window in selected:
        print(f"window={window.number} frames={window.first}-{window.last} scene={window.scene} "
              f"map={window.map_name or 'missing'} model={window.model} stereo={window.stereo} "
              f"quality={window.quality} samples={window.samples} status={window.status} "
              f"invalid={window.invalid} dropped={window.dropped} metadata_missing={window.metadata_missing} "
              f"gpu_begin_anomalies={window.gpu_begin_anomalies} gpu_submission_anomalies={window.gpu_submission_anomalies} "
              f"queue_missing={window.queue_missing} queue_invalid={window.queue_invalid}")
        if window.work_ms is None:
            print("  work_ms=unavailable wall_ms=unavailable")
        else:
            print(f"  {_format_distribution('work_ms', window.work_ms)} "
                  f"work_over_30hz={window.work_over} work_max_streak={window.work_streak}")
            print(f"  {_format_distribution('wall_ms', window.wall_ms)} "
                  f"wall_over_30hz={window.wall_over} wall_max_streak={window.wall_streak} "
                  f"vblank_callbacks={window.vblank_callbacks} vblank_max_per_frame={window.vblank_max}")
            print(f"  {_format_distribution('gpu_wait_ms', window.wait_ms)} "
                  f"{_format_distribution('previous_queue_ms', window.queue_ms)}")
    print("Over-budget streaks can start in an earlier window of the same context.")
    print("Over-budget counts and VBlank callbacks are not missed-presentation counts; GPU queue time overlaps CPU work.")
    print("Presentation: unverified; these records cannot establish a 30 FPS lock.")
    return 0 if complete else 1


if __name__ == "__main__":
    raise SystemExit(main())
