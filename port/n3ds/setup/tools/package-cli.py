"""Publish a self-contained setup CLI for one desktop OS/architecture.

Requires Python 3.10+, .NET SDK 10, Git and network access for runtime packs.
Run from any directory. Output must be a fresh directory.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import zipfile

SETUP = Path(__file__).resolve().parents[1]
ROOT = SETUP.parents[2]
RUNTIMES = ("linux-x64", "linux-arm64", "win-x64", "win-arm64", "osx-x64", "osx-arm64")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, choices=RUNTIMES)
    parser.add_argument("--output", type=Path, help="Fresh output directory (default: build/setup-cli/RUNTIME)")
    parser.add_argument("--dotnet", default="dotnet", help=".NET SDK executable")
    args = parser.parse_args()
    output = (args.output or ROOT / "build/setup-cli" / args.runtime).resolve()
    if output.exists():
        parser.error(f"Output already exists; choose a fresh directory: {output}")
    if output.is_relative_to(SETUP):
        parser.error("Output must be outside the setup source tree")
    binary = output / "app"
    subprocess.run([
        args.dotnet, "publish", str(SETUP / "Cli/HaloSetup.Cli.csproj"),
        "-c", "Release", "-r", args.runtime, "--self-contained", "true",
        "-p:PublishSingleFile=true", "-p:IncludeNativeLibrariesForSelfExtract=true",
        "-p:DebugType=None", "-m:1", "--disable-build-servers", "-o", str(binary),
    ], cwd=ROOT, check=True)
    # A double-click entry point keeps beginner users out of shell syntax. Keep
    # the terminal open afterwards so conversion errors and next steps stay visible.
    if args.runtime.startswith("osx-"):
        start = binary / "Start Halo3DS Setup.command"
        start.write_text("""#!/bin/bash
cd -- "$(dirname -- "$0")" || exit 1
./halo3ds-setup guided
setup_status=$?
echo
read -r -p "Press Enter to close this window... "
exit "$setup_status"
""", encoding="utf-8")
        start.chmod(0o755)
    elif args.runtime.startswith("win-"):
        (binary / "Start Halo3DS Setup.cmd").write_text(
            '@echo off\n"%~dp0halo3ds-setup.exe" guided\nset "setup_status=%errorlevel%"\npause\nexit /b %setup_status%\n',
            encoding="utf-8")
    # Include corresponding converter sources and notices alongside each binary.
    # Git's ignore rules exclude SDK/game inputs, local settings and generated files.
    paths = ["README.md", "LICENSE.md", "docs/DEVELOPMENT.md", "docs/SETUP-CLI.md",
             "port/n3ds/setup/THIRD-PARTY.md", "port/n3ds/setup/tools/package-cli.py"]
    paths += [f"port/n3ds/setup/{name}" for name in ("Cli", "Cli.Tests", "Core", "Tests", "Resources", "Licenses", "vendor")]
    names = subprocess.check_output([
        "git", "ls-files", "--cached", "--others", "--exclude-standard", "-z", "--", *paths,
    ], cwd=ROOT).decode("utf-8").split("\0")
    with zipfile.ZipFile(binary / "Source.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(set(filter(None, names))):
            source = ROOT / name
            if source.is_symlink():
                raise RuntimeError(f"Source archive cannot include a link: {name}")
            if source.is_file():
                archive.write(source, name)
    shutil.copy2(ROOT / "docs/SETUP-CLI.md", binary / "README.md")
    shutil.copy2(ROOT / "LICENSE.md", binary / "LICENSE.md")
    shutil.copy2(SETUP / "THIRD-PARTY.md", binary / "THIRD-PARTY.md")
    shutil.copytree(SETUP / "Licenses", binary / "Licenses")
    # tar preserves executable permissions on Unix; zip is convenient on Windows.
    archive_format = "zip" if args.runtime.startswith("win-") else "gztar"
    archive = shutil.make_archive(str(output / f"halo3ds-setup-{args.runtime}"), archive_format,
                                  root_dir=output, base_dir="app")
    print(f"Published: {archive}")
    print("No game executable, game assets or DSP firmware are bundled.")


if __name__ == "__main__":
    main()
