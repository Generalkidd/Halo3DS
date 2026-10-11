# Cross-platform command-line setup

The CLI prepares Halo3DS game data on Windows, Linux and macOS using the same
converter as the Windows setup apps. It accepts the English North American
original Xbox release, build `01.10.12.2276`. PC/MCC data is unsupported.
It does not compile the engine. Bring a `Halo3DS.3dsx` from a release or your build.

## Easy setup (no programming tools needed)

You need three things:

- Your unchanged English North American **original Xbox** Halo game dump, as
  an ISO/XISO or extracted folder. The tool does not download the game.
- The **Halo3DS.3dsx** game file from the
  [Halo3DS releases page](https://github.com/Generalkidd/Halo3DS/releases).
  Download that file itself, rather than a Windows setup EXE.
- An existing FAT32 SD card or an empty folder where setup can put the results.
  The tool never formats your card. If you use a folder, copy the resulting
  `3ds` and `halo-source` folders to the SD card's top level afterwards.

Packaged CLI builds include their own .NET runtime. You do not need .NET, Java,
Python, Wine or a Windows computer to run them. Choose the package for **the
computer running setup**, not for the 3DS:

| Your computer | Package name |
| --- | --- |
| Mac with an Apple M-series chip | `halo3ds-setup-osx-arm64.tar.gz` |
| Mac with an Intel processor | `halo3ds-setup-osx-x64.tar.gz` |
| Most Windows PCs | `halo3ds-setup-win-x64.zip` |
| Windows on ARM | `halo3ds-setup-win-arm64.zip` |
| Linux PC with an Intel/AMD processor | `halo3ds-setup-linux-x64.tar.gz` |
| Linux on ARM64 | `halo3ds-setup-linux-arm64.tar.gz` |

On Mac, **Apple menu → About This Mac** shows the chip or processor. Current
packages are unsigned. Native system dependencies still apply; Linux packages
need a glibc-based distribution.

### Mac: start with a double click

1. Double-click the downloaded `.tar.gz` file to extract it. Open the `app`
   folder inside.
2. Double-click **Start Halo3DS Setup.command**. A Terminal window opens with
   a menu. Choose **1** for first-time setup.
3. At each path question, drag the requested file or folder from Finder into
   the Terminal window, then press Enter. You can also paste a path. Your SD
   card appears in Finder's sidebar and is mounted inside `/Volumes`.
4. With a complete Xbox ISO or extracted dump, press Enter to skip the optional
   compatibility-file question. Maps-only dumps need the matching `default.xbe`.
5. Read the displayed choices. Type **Y** and press Enter to begin. Wait for
   **Ready to play** and **Prepared … Destination: …**. The window stays open
   so you can read the result, any audio instructions, or an error.

If macOS blocks the unsigned tool, follow
[Apple's instructions for opening a trusted downloaded app](https://support.apple.com/102445).
The approval is in **System Settings → Privacy & Security → Open Anyway**
after an attempted launch. Only approve a download whose source you trust;
there is no need to turn off macOS security globally. Public releases should
be signed and notarized before distributing them to a wider audience.

### Windows and Linux

On Windows, extract the ZIP and double-click **Start Halo3DS Setup.cmd** in
its `app` folder. Choose **1** and follow the same questions.

On Linux, extract the archive and open a terminal in its `app` folder. Run:

```sh
./halo3ds-setup
```

The same command starts guided setup in a Mac terminal. Type **Q** at any
question or press **Ctrl+C** to cancel. Pressing Enter at the final confirmation
cancels; no installation starts until you answer **Y**. If a run is interrupted,
start it again with the same paths. Completed maps are checked and reused;
saves and unrelated files are retained.

### If something goes wrong

| Message or symptom | What to do |
| --- | --- |
| File or folder was not found | Drag it from Finder/File Explorer again. Select the file itself, rather than its containing folder. |
| Choose a Halo3DS.3dsx file | Download `Halo3DS.3dsx` from the release; a ZIP or Windows EXE is not the game file. |
| Unsupported Xbox release / PC/MCC | Use the English North American original Xbox retail dump, build `01.10.12.2276`. |
| Missing compatibility/HUD data | Use the full Xbox ISO, or select its matching `default.xbe` when asked. |
| Not enough destination space | Free the stated space or choose another output folder, then rerun. |
| Sound needs DSP firmware | On your console, open Rosalina with **L + D-pad Down + SELECT**, then **Miscellaneous options → Dump DSP firmware**. |

## Commands for experienced users and scripts

Explicit commands skip the questions. The destination must already exist.
Quote paths containing spaces. `--help` prints every option; `guided` explicitly
starts the questions even with redirected input.

```sh
./halo3ds-setup verify --source "/path/to/Xbox Halo.iso"
./halo3ds-setup prepare --source "/path/to/Xbox Halo.iso" --destination "/path/to/output" --game "/path/to/Halo3DS.3dsx"
./halo3ds-setup update --destination "/path/to/output" --game "/path/to/new/Halo3DS.3dsx"
```

PowerShell example:

```powershell
./halo3ds-setup.exe prepare --source 'C:\Games\Halo Xbox.iso' --destination 'H:\' --game 'C:\Builds\Halo3DS.3dsx'
```

`prepare` also accepts an extracted Xbox game folder or maps folder. If its
matching `default.xbe` is not alongside the maps, add
`--compatibility "/path/to/default.xbe"`. A verified
`ntsc2276-hud-strings.bin` is also accepted. Existing correct maps are reused,
damaged outputs are repaired, and saves and unrelated files are retained.
`update` changes the launcher in an already prepared installation.

`verify` is read-only: it validates supported map headers and hashes for all
recognized maps present, requiring `ui.map` and at least one playable map.
A successful verification does not guarantee a full 24-map dump, available HUD
data, DSP firmware, destination capacity or a valid game executable. `prepare`
performs the install-time checks and conversion/read-back verification.

Progress goes to stderr and the result goes to stdout. `--quiet` suppresses
progress. Ctrl+C requests cancellation; long conversion work checks for it and
finishes any active atomic replacement safely. Completed files remain usable
for a subsequent run. Rerun the same command to continue.

Exit codes: **0** success, **1** input/setup failure, **2** invalid arguments,
**130** cancelled.

If you prepared a normal folder, copy its `3ds` and `halo-source` folders to
the root of a FAT32 SD card. For audio, dump DSP firmware on your console via
Rosalina: **L + D-pad Down + SELECT → Miscellaneous options → Dump DSP firmware**.

## Developing and packaging

Install .NET SDK 10. From the repository root:

```sh
dotnet run --project port/n3ds/setup/Cli -- --help
dotnet run --project port/n3ds/setup/Tests
dotnet run --project port/n3ds/setup/Cli.Tests
```

The tests use synthetic data. The CLI tests cover guided input and path retries,
confirmation/cancellation without writes, argument errors, launcher updates,
save preservation, progress, corrupt input and redirected destination rejection. Full conversion testing needs your supported Xbox data
and game executable; see `docs/DEVELOPMENT.md` for integration checks.

With Python 3.10+, Git and .NET SDK 10, publish a self-contained package:

```sh
python3 port/n3ds/setup/tools/package-cli.py --runtime linux-x64
```

Supported package targets: `linux-x64`, `linux-arm64`, `win-x64`, `win-arm64`,
`osx-x64`, `osx-arm64`. Each needs its own package. Runtime packs are downloaded
by .NET during publishing. Use `--dotnet PATH` for a locally installed SDK.
Default output is `build/setup-cli/RUNTIME`; use `--output PATH` for another
fresh directory. Existing output directories are rejected.

Packages contain the app, notices and corresponding source. They exclude the
game executable, game data, XDK headers and DSP firmware. Before publishing,
run `python3 tools/check_repository_contents.py --working-tree`, inspect the
package, and test it on the target OS. Cross-publishing alone is not runtime
validation on Windows or macOS.

The `Setup CLI` GitHub Actions workflow tests and packages on native Linux,
Windows, Intel Mac and Apple Silicon runners. Its downloadable artifacts are
unsigned test packages, not automatically published releases. Windows/Linux
ARM64 targets can also be packaged, but are not in the native CI matrix.
