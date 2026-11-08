"""Run the production ARM compressed animation samplers against synthetic streams."""

import argparse
from pathlib import Path
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (
    UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_PC,
    UC_ARM_REG_R0, UC_ARM_REG_SP,
)

from verify_action_replay import ROOT, STOP, compile_args, run


SAMPLER = Path("source/models/model_animations.c")
FIXTURE = ROOT / "port/n3ds/tests/fixtures/animation_tracks_arm.c"
SOURCES = (Path("source/math/real_math.c"), Path("source/models/model_animation_definitions.c"))


def invoke(binary):
    with binary.open("rb") as stream:
        elf = ELFFile(stream)
        arm = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        arm.mem_map(0x10000, 0x1F0000)
        arm.mem_map(0x800000, 0x100000)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD":
                arm.mem_write(segment["p_vaddr"], segment.data())
        arm.reg_write(UC_ARM_REG_C1_C0_2, 0xF00000)
        arm.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        arm.reg_write(UC_ARM_REG_SP, 0x8FFF00)
        arm.reg_write(UC_ARM_REG_LR, STOP)
        arm.emu_start(elf.header["e_entry"], STOP, timeout=5000000, count=10000000)
        assert arm.reg_read(UC_ARM_REG_PC) == STOP, "fixture did not reach its exit"
        assert not arm.reg_read(UC_ARM_REG_R0), f"fixture failed at line {arm.reg_read(UC_ARM_REG_R0)}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--gcc", required=True, type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="halo-animation-test-") as directory:
        out = Path(directory)
        objects = []
        for source in SOURCES:
            obj = out / (source.stem + ".o")
            run([str(args.clang), *compile_args(build, source), "-c", str(ROOT / source), "-o", str(obj)])
            objects.append(obj)

        flags = [flag for flag in compile_args(build, SAMPLER)
                 if not flag.startswith("-DHALO_N3DS_ALGO_COMPRESSED_TRACKS=")]
        for mode in (1, 2):
            obj = out / "fixture.o"
            binary = out / "fixture.elf"
            run([str(args.clang), *flags, f"-DHALO_N3DS_ALGO_COMPRESSED_TRACKS={mode}",
                 "-c", str(FIXTURE), "-o", str(obj)])
            run([str(args.gcc), "-march=armv6k", "-mfloat-abi=hard", "-mfpu=vfp", "-marm",
                 "-nostartfiles", "-Wl,--gc-sections,-e,animation_tracks_fixture,-Ttext=0x20000",
                 "-Wl,--wrap=free", str(obj), *map(str, objects), "-lm", "-o", str(binary)])
            invoke(binary)
    print("PASS: ARM candidate and verify match exact, fractional, wrapped, reversed, constant and overlay samples")
    print("PASS: ARM cache pressure, generation change, malformed track, unowned stream and allocation fallback")


if __name__ == "__main__":
    main()
