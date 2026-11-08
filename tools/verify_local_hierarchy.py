"""Run the production ARM object hierarchy cache against actual VFP matrix math."""

import argparse
from pathlib import Path
import tempfile

from verify_action_replay import ROOT, compile_args, run
from verify_animation_tracks import invoke


CALLER = Path("source/objects/objects.c")
FIXTURE = ROOT / "port/n3ds/tests/fixtures/local_hierarchy_arm.c"
SOURCES = (Path("source/math/matrix_math.c"), Path("port/n3ds/src/engine_math.c"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--gcc", required=True, type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="halo-hierarchy-test-") as directory:
        out = Path(directory)
        objects = []
        for source in SOURCES:
            obj = out / (source.stem + ".o")
            run([str(args.clang), *compile_args(build, source), "-c", str(ROOT / source), "-o", str(obj)])
            objects.append(obj)

        flags = [flag for flag in compile_args(build, CALLER)
                 if not flag.startswith("-DHALO_N3DS_ALGO_LOCAL_HIERARCHY=")]
        for mode in (1, 2):
            obj = out / "fixture.o"
            binary = out / "fixture.elf"
            run([str(args.clang), *flags, f"-DHALO_N3DS_ALGO_LOCAL_HIERARCHY={mode}",
                 "-DHALO_N3DS_ALGORITHM_PROFILE=1", "-DHALO_N3DS_PERFORMANCE_PROFILE=1",
                 "-c", str(FIXTURE), "-o", str(obj)])
            run([str(args.gcc), "-march=armv6k", "-mfloat-abi=hard", "-mfpu=vfp", "-marm",
                 "-nostartfiles", "-Wl,--gc-sections,-e,local_hierarchy_fixture,-Ttext=0x20000",
                 "-Wl,--wrap=free", str(obj), *map(str, objects), "-lm", "-o", str(binary)])
            invoke(binary)
    print("PASS: ARM local and world hierarchy reuse, changed parent, exact signed zero, postprocessed output,"
          " object/model reuse, pressure, reset, alias, precision and allocation fallback")


if __name__ == "__main__":
    main()
