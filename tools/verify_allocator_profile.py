"""Run the allocation collector host check and optional pinned ARM allocator check."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def verify_arm(args, out):
    from verify_action_replay import ROOT, compile_args, run
    from verify_animation_tracks import invoke

    gcc = args.gcc.resolve()
    ar = gcc.with_name(gcc.name.replace("-gcc", "-ar"))
    platform = Path("port/n3ds/src/engine_platform.c")
    fixture = Path("port/n3ds/tests/fixtures/allocation_profile_arm.c")
    units = ((platform, gcc), (Path("port/n3ds/src/engine_gpu_linear.cpp"), gcc),
             (Path("port/n3ds/src/engine_mission_diagnostics.c"), args.clang),
             (Path("port/n3ds/tests/src/engine_heap_tests.c"), args.clang), (fixture, gcc))
    defines = ("HALO_N3DS_ALGORITHM_PROFILE", "HALO_N3DS_PERFORMANCE_PROFILE", "HALO_N3DS_ALLOCATION_PROFILE")
    objects = []
    for source, compiler in units:
        base = platform if source == fixture else source
        obj = out / (source.stem + ".o")
        flags = [flag for flag in compile_args(args.arm_build, base)
                 if not any(flag.startswith(f"-D{name}=") for name in defines)]
        run([str(compiler), *flags, *(f"-D{name}=1" for name in defines),
             *(["-fno-builtin"] if source == fixture else []), "-c", str(ROOT / source), "-o", str(obj)])
        objects.append(str(obj))

    libctru = str(args.libctru_archive.resolve())
    # Use the real tree code without bringing in unrelated 3DS service startup.
    tree = [name for name in subprocess.check_output([str(ar), "t", libctru], text=True, timeout=10).splitlines()
            if name.startswith("rbtree_")]
    if not tree:
        raise RuntimeError("libctru archive has no linear address-map implementation")
    subprocess.run([str(ar), "x", libctru, *tree], cwd=out, timeout=10, check=True)
    binary = out / "allocator.elf"
    wraps = ("malloc", "calloc", "memalign", "aligned_alloc", "free", "realloc", "_sbrk_r")
    run([str(gcc), "-march=armv6k", "-mfloat-abi=hard", "-mfpu=vfp", "-marm", "-nostartfiles",
         "-Wl,--gc-sections,-e,allocator_fixture,-Ttext=0x20000", *(f"-Wl,--wrap={name}" for name in wraps),
         *objects, *(str(out / name) for name in tree), str(args.newlib_archive.resolve()), "-o", str(binary)])
    invoke(binary)
    print("PASS: pinned ARM newlib and vendored linear allocator preserve operations and record their post-call usage")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--arm-build", type=Path)
    parser.add_argument("--clang", type=Path)
    parser.add_argument("--gcc", type=Path)
    parser.add_argument("--libctru-archive", type=Path)
    parser.add_argument("--newlib-archive", type=Path)
    args = parser.parse_args()
    if args.arm_build and not all((args.clang, args.gcc, args.libctru_archive, args.newlib_archive)):
        parser.error("--arm-build requires --clang, --gcc, --libctru-archive and --newlib-archive")
    port = Path(__file__).resolve().parents[1] / "port/n3ds"
    with tempfile.TemporaryDirectory(prefix="halo-allocation-test-") as directory:
        binary = Path(directory) / "fixture"
        subprocess.run([args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-I", str(port / "include"), str(port / "tests/fixtures/allocation_profile_host.c"),
                        "-o", str(binary)], timeout=30, check=True)
        subprocess.run([str(binary)], timeout=10, check=True)
        if args.arm_build:
            verify_arm(args, Path(directory))


if __name__ == "__main__":
    main()
