#!/usr/bin/env python3
"""Exercise controller and graphics settings with fake HID and private SD roots."""

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path


def checked_record(data):
    checksum = 2166136261
    for byte in data:
        checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
    return bytes(data) + struct.pack("<I", checksum)


def legacy_file(swapped):
    return checked_record(struct.pack("<6I", 0x3343544C, 1, 125, 80, 1, swapped))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    port = tests.parent
    source = port.parents[1] / "source"
    with tempfile.TemporaryDirectory(prefix="halo-controller-settings-") as directory:
        temporary = Path(directory)
        # The Xbox headers declare functions with these POSIX names.
        prelude = temporary / "host_prelude.h"
        prelude.write_text(
            "#include <stdlib.h>\n#include <string.h>\n"
            "#define strnlen halo_legacy_strnlen\n#define random halo_legacy_random\n"
        )
        executable = temporary / "controller-settings-test"
        subprocess.run([
            args.clang, "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-DHALO_N3DS", "-DBUILDING_CSERIES",
            "-Wno-visibility", "-include", str(prelude),
            "-I", str(source / "cseries"), "-I", str(source / "math"),
            "-I", str(source), "-I", str(port / "include"),
            str(tests / "host/controller_settings_test.c"),
            str(port / "src/engine_controls.c"),
            str(port / "src/engine_graphics_options.c"),
            str(port / "src/engine_input.c"),
            str(tests / "src/engine_input_tests.c"),
            "-o", str(executable),
        ], check=True, timeout=45)

        def run(scenario, working_dir):
            subprocess.run([str(executable), scenario], cwd=working_dir, check=True, timeout=15)

        sd = temporary / "sd"
        sd.mkdir()
        run("new", sd)
        run("original", sd)
        run("customize", sd)
        run("read-primary", sd)
        run("read-original", sd)
        primary = sd / "controller-settings.bin"
        backup = sd / "controller-settings.bak"
        assert primary.read_bytes()[4:8] == struct.pack("<I", 2)
        last_good = backup.read_bytes()
        valid = primary.read_bytes()
        rotation_sd = temporary / "rotation-sd"
        rotation_sd.mkdir()
        (rotation_sd / primary.name).write_bytes(valid)
        (rotation_sd / backup.name).write_bytes(last_good)
        run("rotation-failure", rotation_sd)
        assert (rotation_sd / backup.name).read_bytes() == valid
        run("read-primary", rotation_sd)

        io_sd = temporary / "io-sd"
        io_sd.mkdir()
        (io_sd / primary.name).write_bytes(valid)
        for operation in ("open", "size", "read", "close"):
            run(f"{operation}-failure", io_sd)
            assert (io_sd / primary.name).read_bytes() == valid
            assert not (io_sd / backup.name).exists()
        run("read-primary", io_sd)

        corrupt = bytearray(valid)
        corrupt[-1] ^= 0x01
        primary.write_bytes(corrupt)
        run("read-backup", sd)
        # A valid checksum is insufficient: an out-of-range binding must be rejected.
        invalid_binding = bytearray(valid[:-4])
        invalid_binding[36] = 255  # New model L (8 byte header, seven u32 settings).
        primary.write_bytes(checked_record(invalid_binding))
        run("read-backup", sd)
        run("save-failure", sd)
        assert backup.read_bytes() == last_good
        run("read-backup", sd)
        run("save-retry", sd)
        run("read-primary", sd)

        for swapped in (0, 1):
            legacy_sd = temporary / f"legacy-{swapped}"
            legacy_sd.mkdir()
            (legacy_sd / primary.name).write_bytes(legacy_file(swapped))
            run("legacy-swap" if swapped else "legacy-default", legacy_sd)

        graphics_sd = temporary / "graphics-sd"
        graphics_sd.mkdir()
        run("graphics-first-save", graphics_sd)
        run("graphics-reload-balanced", graphics_sd)
        graphics_primary = graphics_sd / "graphics-settings.bin"
        graphics_saved = graphics_primary.read_bytes()
        run("graphics-retry", graphics_sd)
        run("graphics-reload-classic", graphics_sd)
        # A valid checksum cannot make an out-of-range graphics option safe.
        for word in (2, 3):  # Texture and resolution, after the versioned header.
            invalid_graphics = bytearray(graphics_saved[:-4])
            struct.pack_into("<I", invalid_graphics, word * 4, 255)
            graphics_primary.write_bytes(checked_record(invalid_graphics))
            run("graphics-reload-balanced", graphics_sd)

        for scenario, reload in (
            ("graphics-original", "graphics-reload-native"),
            ("graphics-locked", "graphics-reload-locked-save"),
        ):
            isolated_sd = temporary / scenario
            isolated_sd.mkdir()
            (isolated_sd / graphics_primary.name).write_bytes(graphics_saved)
            run(scenario, isolated_sd)
            run(reload, isolated_sd)

        legacy_graphics = temporary / "graphics-legacy"
        legacy_graphics.mkdir()
        legacy_record = checked_record(struct.pack("<3I", 0x33584647, 1, 1))
        legacy_primary = legacy_graphics / graphics_primary.name
        legacy_primary.write_bytes(legacy_record)
        run("graphics-legacy", legacy_graphics)
        assert legacy_primary.read_bytes()[4:8] == struct.pack("<I", 2)
        assert (legacy_graphics / "graphics-settings.bak").read_bytes() == legacy_record
        run("graphics-reload-native", legacy_graphics)


if __name__ == "__main__":
    main()
