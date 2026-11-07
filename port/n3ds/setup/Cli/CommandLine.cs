using System.Diagnostics;
using System.Security.Cryptography;

namespace HaloSetup.Cli;

public static class CommandLine
{
    const string Help = """
        Halo3DS setup — Windows, Linux and macOS

        Run without arguments for step-by-step setup in a terminal.

        Usage:
          halo3ds-setup guided
          halo3ds-setup prepare --source PATH --destination PATH --game PATH [--compatibility PATH] [--candidate-textures] [--quiet]
          halo3ds-setup update --destination PATH --game PATH [--quiet]
          halo3ds-setup verify --source PATH [--quiet]
          halo3ds-setup texture-compare --map PATH --output PATH --variant raw|lz4hc|compact [--lz4-library PATH]
          halo3ds-setup texture-lod --map PATH --output PATH
          halo3ds-setup --help

        prepare  Convert Xbox game data and install Halo3DS to an existing SD-card
                 root or output folder. Correct files are reused; saves are kept.
        update   Replace only the launcher in an existing installation.
        verify   Check source map headers and SHA-256 hashes without writing files.
                 This does not verify HUD compatibility, DSP or an installation.
        texture-compare  Cook and verify one development .ntx4 comparison archive
                         from an expanded retail map. Output must be a new file.
        texture-lod      Cook one development .ntxm fixture from expanded retail
                         b30.map. Output must be a new file.

        --source         Xbox ISO/XISO, maps folder or extracted Xbox game folder.
                         Requires English North American retail build 01.10.12.2276.
                         PC/MCC game data is unsupported.
        --destination    Existing SD-card root or output folder. Never formatted.
        --game           Halo3DS.3dsx to install (required; not compiled by this tool).
        --compatibility  Matching default.xbe or ntsc2276-hud-strings.bin, if needed.
        --candidate-textures  Developer opt-in: also prepare RGBA8 .ntx4 archives.
                              Legacy .ntx files remain available. Prepare only.
        --lz4-library    Optional path to the official liblz4 shared library for
                         the lz4hc variant. The encoder uses HC level 9.
        --quiet          Suppress progress; retain the result and any errors.

        Quote paths containing spaces. Press Ctrl+C to cancel safely.
        Exit codes: 0 success, 1 setup/input failure, 2 usage error, 130 cancelled.
        """;

    public static int Run(string[] args, TextWriter output, TextWriter error, CancellationToken ct = default, TextReader? input = null)
    {
        try
        {
            // Prompt only when explicitly requested or supplied an interactive reader.
            // Redirected no-argument invocations still print help instead of waiting.
            if ((args.Length == 0 && input != null) || args.SequenceEqual(new[] { "guided" }))
            {
                if (input == null) throw new UsageException("Guided setup needs terminal input. Use prepare, update or verify in scripts.");
                args = GuidedSetup.ReadArguments(input, output, ct);
            }
            var command = Parse(args);
            if (command == null) { output.WriteLine(Help); return 0; }
            ct.ThrowIfCancellationRequested();
            if (command.Comparison is { } comparison)
            {
                var archive = Textures.GenerateComparison(comparison.Map, comparison.Output, comparison.Mode,
                    comparison.Lz4Library, ct);
                output.WriteLine($"Verified comparison archive: {archive.ArchiveBytes} bytes, {archive.Entries} keys. " +
                    $"Distinct payloads: raw RGBA8 {archive.RawRgba8}, LZ4HC RGBA8 {archive.Lz4Rgba8}, " +
                    $"RGB8 {archive.Rgb8}, LA8 {archive.La8}.");
                if (archive.Lz4Version != null) output.WriteLine($"LZ4HC level 9, liblz4 {archive.Lz4Version}.");
                return 0;
            }
            if (command.Lod is { } lod)
            {
                var samples = Textures.GenerateLodFixture(lod.Map, lod.Output, ct);
                output.WriteLine($"Verified texture LOD fixture with {samples.Count} authored RGB8 chains from 64 to 8 pixels.");
                foreach (var sample in samples)
                    output.WriteLine($"{sample.Label} from {sample.Bitmap}, descriptor 0x{sample.BitmapOffset:x}, first source mip {sample.FirstMip}.");
                return 0;
            }
            var progress = command.Quiet ? null : new TerminalProgress(error);
            if (command.Name == "verify")
            {
                Verify(command.Source!, output, progress, ct);
                return 0;
            }
            // Explicit paths avoid accidentally installing a stale executable
            // found in the current directory or next to a different setup app.
            var result = Installer.Run(new(command.Source ?? "", command.Destination!, command.Game!,
                command.Compatibility, command.Name == "update", command.CandidateTextures), progress, ct);
            output.WriteLine(command.Name == "update"
                ? $"Launcher updated: {result.Destination}"
                : $"Prepared {result.Prepared} map(s); reused {result.AlreadyCorrect}. Destination: {result.Destination}");
            if (!result.DspPresent)
                output.WriteLine("Sound needs DSP firmware. On your console, open Rosalina with L + D-pad Down + SELECT, then Miscellaneous options > Dump DSP firmware.");
            output.WriteLine("Use a FAT32 SD card. If you prepared an output folder, copy its 3ds and halo-source folders to the card root.");
            return 0;
        }
        catch (UsageException ex) { error.WriteLine($"Error: {ex.Message}\nRun halo3ds-setup --help for usage."); return 2; }
        catch (OperationCanceledException) { error.WriteLine("Cancelled. Completed files are retained; rerun the same command to continue."); return 130; }
        catch (Exception ex) when (ex is SetupException or IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            error.WriteLine($"Setup could not finish: {ex.Message}");
            error.WriteLine("Check the selected paths and try again. Completed files and saved progress are retained.");
            return 1;
        }
    }

    static Options? Parse(string[] args)
    {
        if (args.Length == 0 || (args.Length == 1 && args[0] is "--help" or "-h")) return null;
        string name = args[0];
        if (name is not ("prepare" or "update" or "verify" or "texture-compare" or "texture-lod"))
            throw new UsageException($"Unknown command '{name}'.");
        if (args.Length == 2 && args[1] is "--help" or "-h") return null;
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        bool quiet = false, candidateTextures = false;
        for (int i = 1; i < args.Length; i++)
        {
            string option = args[i];
            if (option == "--quiet")
            {
                if (quiet) throw new UsageException("Duplicate option --quiet.");
                quiet = true; continue;
            }
            if (option == "--candidate-textures")
            {
                if (name != "prepare") throw new UsageException("--candidate-textures is only valid for prepare.");
                if (candidateTextures) throw new UsageException("Duplicate option --candidate-textures.");
                candidateTextures = true; continue;
            }
            bool allowed = option switch
            {
                "--source" => name is "prepare" or "verify",
                "--destination" or "--game" => name is "prepare" or "update",
                "--compatibility" => name == "prepare",
                "--map" or "--output" => name is "texture-compare" or "texture-lod",
                "--variant" or "--lz4-library" => name == "texture-compare",
                _ => false
            };
            if (!allowed) throw new UsageException($"Unexpected option '{option}' for {name}.");
            if (++i == args.Length || args[i].StartsWith("--", StringComparison.Ordinal) || string.IsNullOrWhiteSpace(args[i]))
                throw new UsageException(option == "--variant" ? "--variant needs raw, lz4hc or compact." : $"{option} needs a path.");
            if (!values.TryAdd(option, args[i])) throw new UsageException($"Duplicate option {option}.");
        }
        string? Get(string key) => values.GetValueOrDefault(key);
        void Require(string key) { if (Get(key) == null) throw new UsageException($"{name} requires {key}."); }
        if (name == "texture-compare")
        {
            Require("--map"); Require("--output"); Require("--variant");
            var mode = Get("--variant") switch
            {
                "raw" => TextureComparisonMode.Raw,
                "lz4hc" => TextureComparisonMode.Lz4Hc,
                "compact" => TextureComparisonMode.Compact,
                _ => throw new UsageException("--variant needs raw, lz4hc or compact.")
            };
            string? library = Get("--lz4-library");
            if (library != null && mode != TextureComparisonMode.Lz4Hc)
                throw new UsageException("--lz4-library is only valid for --variant lz4hc.");
            return new(name, null, null, null, null, quiet, false,
                new(Get("--map")!, Get("--output")!, mode, library));
        }
        if (name == "texture-lod")
        {
            Require("--map"); Require("--output");
            return new(name, null, null, null, null, quiet, false, Lod: new(Get("--map")!, Get("--output")!));
        }
        if (name != "update") Require("--source");
        if (name != "verify") { Require("--destination"); Require("--game"); }
        return new(name, Get("--source"), Get("--destination"), Get("--game"), Get("--compatibility"), quiet, candidateTextures);
    }

    static void Verify(string source, TextWriter output, IProgress<SetupProgress>? progress, CancellationToken ct)
    {
        var files = Sources.Discover(source);
        var names = Data.Catalog.Keys.Where(n => files.ContainsKey(n + ".map")).ToList();
        Data.Require(names.Contains("ui") && names.Count >= 2, "The source needs ui.map and at least one campaign or multiplayer map.");
        var buffer = new byte[Data.Chunk];
        for (int i = 0; i < names.Count; i++)
        {
            ct.ThrowIfCancellationRequested();
            string name = names[i]; var entry = files[name + ".map"]; var expected = Data.Catalog[name];
            Installer.CheckMap(entry, name, expected);
            using var stream = entry.Open();
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            int count;
            while ((count = stream.Read(buffer)) > 0)
            {
                ct.ThrowIfCancellationRequested(); hash.AppendData(buffer, 0, count);
                progress?.Report(new($"Verifying {name}.map", 100.0 * (i + stream.Position / (double)entry.Length) / names.Count));
            }
            string expectedHash = entry.Length == expected.Bytes ? expected.Sha256 : expected.SourceSha256;
            Data.Require(Convert.ToHexStringLower(hash.GetHashAndReset()) == expectedHash,
                $"{name}.map failed its integrity check. It is damaged, modified, or from an unsupported release.");
        }
        output.WriteLine($"Verified {names.Count} source map(s) for Xbox build {Data.Build}. HUD, DSP and installed files were not checked.");
    }

    sealed record Options(string Name, string? Source, string? Destination, string? Game, string? Compatibility, bool Quiet,
        bool CandidateTextures, ComparisonOptions? Comparison = null, LodOptions? Lod = null);
    sealed record ComparisonOptions(string Map, string Output, TextureComparisonMode Mode, string? Lz4Library);
    sealed record LodOptions(string Map, string Output);
    sealed class UsageException(string message) : Exception(message);

    sealed class TerminalProgress(TextWriter writer) : IProgress<SetupProgress>
    {
        readonly Stopwatch clock = Stopwatch.StartNew();
        string lastMessage = "";
        long lastTime = -1000;
        public void Report(SetupProgress value)
        {
            // Synchronous reporting preserves ordering, including when redirected.
            // Use plain lines so terminals, CI and log files all remain readable.
            long now = clock.ElapsedMilliseconds;
            if (value.Message == lastMessage && now - lastTime < 1000 && value.Percent < 100) return;
            writer.WriteLine(FormattableString.Invariant($"{value.Percent,6:F1}% {value.Message}"));
            lastMessage = value.Message; lastTime = now;
        }
    }
}
