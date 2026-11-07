"""Run the production ARM action journal and compatibility translator with stubbed campaign and SD I/O.

Install unicorn and pyelftools, then pass a completed --action-replay build
directory from this checkout and the Clang/devkitARM compilers it used.
"""

import argparse
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (
    UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC,
    UC_ARM_REG_R0, UC_ARM_REG_SP,
)


ROOT = Path(__file__).resolve().parents[1]
REPLAY = Path("port/n3ds/src/engine_action_replay.c")
CRT = Path("port/n3ds/src/engine_crt.c")
FIXTURE = ROOT / "port/n3ds/tests/fixtures/action_replay_arm.c"
STOP = 0x1FFF00


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise RuntimeError("\n".join(line for line in result.stderr.splitlines() if "warning:" not in line))


def compile_args(build, source):
    args = shlex.split((build / "compile" / (str(source) + ".rsp")).read_text())
    old_source = args[args.index("-c") + 1]
    old_root = old_source[:-(len(str(source)) + 1)]
    return [arg.replace(old_root, str(ROOT)) for arg in args[:args.index("-MMD")]
            if not arg.startswith(("-DHALO_N3DS_ACTION_REPLAY=", "-DHALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS="))]


def invoke(binary, test, journal):
    with binary.open("rb") as source:
        elf = ELFFile(source)
        symbols = elf.get_section_by_name(".symtab")
        data = symbols.get_symbol_by_name("journal")[0]["st_value"]
        length = symbols.get_symbol_by_name("journal_bytes")[0]["st_value"]
        arm = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        arm.mem_map(0x10000, 0x1F0000)
        arm.mem_map(0x800000, 0x100000)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD":
                arm.mem_write(segment["p_vaddr"], segment.data())
        if journal:
            arm.mem_write(data, journal)
            arm.mem_write(length, struct.pack("<I", len(journal)))
        arm.reg_write(UC_ARM_REG_C1_C0_2, 0xF00000)
        arm.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        arm.reg_write(UC_ARM_REG_SP, 0x8FFF00)
        arm.reg_write(UC_ARM_REG_LR, STOP)
        arm.reg_write(UC_ARM_REG_R0, test)
        arm.emu_start(elf.header["e_entry"], STOP, timeout=5000000, count=10000000)
        assert arm.reg_read(UC_ARM_REG_PC) == STOP, f"case {test} did not exit (PC={arm.reg_read(UC_ARM_REG_PC):#x})"
        assert not arm.reg_read(UC_ARM_REG_R0), f"case {test} failed at fixture line {arm.reg_read(UC_ARM_REG_R0)}"
        return bytes(arm.mem_read(data, struct.unpack("<I", arm.mem_read(length, 4))[0]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--gcc", required=True, type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="halo-action-test-") as directory:
        out = Path(directory)
        crt = out / "crt.o"
        run([str(args.gcc), *compile_args(build, CRT), "-c", str(ROOT / CRT), "-o", str(crt)])
        journal = b""
        for mode, tests in ((1, (0,)), (2, (1, 2, 3, 4, 5, 6, 7))):
            flags = [*compile_args(build, REPLAY), f"-DHALO_N3DS_ACTION_REPLAY={mode}"]
            objects = []
            for source in (ROOT / REPLAY, FIXTURE):
                obj = out / (source.stem + ".o")
                run([str(args.clang), *flags, "-c", str(source), "-o", str(obj)])
                objects.append(obj)
            binary = out / "fixture.elf"
            run([str(args.gcc), "-march=armv6k", "-mfloat-abi=hard", "-mfpu=vfp", "-marm",
                 "-nostartfiles", "-Wl,--gc-sections,-e,replay_fixture,-Ttext=0x20000",
                 "-Wl,--wrap=halo_n3ds_snprintf,--wrap=fclose", *map(str, (crt, *objects)), "-o", str(binary)])
            for test in tests:
                actual = invoke(binary, test, journal)
                if mode == 1:
                    journal = actual
                else:
                    assert actual == journal or test == 7, f"case {test} changed the input journal"
    print("PASS: ARM record/replay, compatibility rejection, no-player tick, restore, map replacement, "
          "dequeue failure, local RNG divergence before and after a tick, corrupt journal")


if __name__ == "__main__":
    main()
