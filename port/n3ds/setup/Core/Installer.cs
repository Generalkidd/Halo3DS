// New3DS integration additions by Kevin Boggs.
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace HaloSetup;

public sealed record SetupOptions(string Source, string Destination, string Executable, string? CompatibilityFile = null,
    bool LauncherOnly = false, string? PcMapsFolder = null);

public static class Installer
{
    public static string NormalizeDestination(string destination)
    {
        destination = destination.Trim();
        Data.Require(destination.Length > 0, "Choose an existing SD card root or output folder.");
        // A bare drive letter means the card root here, not Windows' per-drive current folder.
        if (OperatingSystem.IsWindows() && destination.Length == 2 &&
            char.IsAsciiLetter(destination[0]) && destination[1] == ':')
            destination += Path.DirectorySeparatorChar;
        return Path.GetFullPath(destination);
    }

    public static void RetireLegacyLauncher(string destination)
    {
        string legacy = SafePath(destination, "3ds/HaloSourceEngine/HaloSourceEngine.3dsx");
        if (!File.Exists(legacy)) return;
        // Keep the previous launcher outside Homebrew Launcher's scan directory.
        string backup = SafePath(destination, "halo-source/launcher-backups/" +
            Guid.NewGuid().ToString("N") + "/HaloSourceEngine.3dsx");
        Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
        File.Move(legacy, backup);
    }

    public static string SafePath(string root, string relative)
    {
        root = Path.TrimEndingDirectorySeparator(NormalizeDestination(root));
        // Unix paths may differ only in case. Case-insensitive containment there
        // could accept ../CARD/file as a child of /media/card.
        var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        string result = Path.GetFullPath(Path.Combine(root, relative));
        // TrimEndingDirectorySeparator deliberately preserves drive roots (H:\).
        string prefix = Path.EndsInDirectorySeparator(root) ? root : root + Path.DirectorySeparatorChar;
        Data.Require(!Path.IsPathRooted(relative) &&
            !result.Equals(root, comparison) &&
            result.StartsWith(prefix, comparison),
            "Invalid destination path.");
        // The user explicitly chose this root. Its own mapping and ancestors
        // may legitimately be links (Wine drives, redirected home folders).
        // Only descendants can redirect an installer-owned path elsewhere.
        for (string? current = result; current != null &&
            !current.Equals(root, comparison); current = Path.GetDirectoryName(current))
        {
            try
            {
                // GetAttributes also checks dangling links; Exists may hide them.
                Data.Require((File.GetAttributes(current) & FileAttributes.ReparsePoint) == 0,
                    $"A file or folder inside the selected destination is a redirected link: {current}. Choose an empty output folder, or replace that link with a normal folder before running setup.");
            }
            catch (FileNotFoundException) { } // Setup will create this child.
            catch (DirectoryNotFoundException) { }
            // A disconnected card reader may retain its drive letter. The
            // caller checks Directory.Exists(root) before any installation.
            catch (IOException) when (!Directory.Exists(Path.GetPathRoot(current)!)) { }
        }
        return result;
    }

    public static byte[] CheckMap(SourceEntry entry, string name, MapInfo expected)
    {
        byte[] header = entry.Head(2048);
        Data.Require(header.Length == 2048 && header.AsSpan(0, 4).SequenceEqual("daeh"u8) &&
            header.AsSpan(2044, 4).SequenceEqual("toof"u8) && Data.U32(header, 4) == 5,
            $"{name}.map is not an original Xbox Halo map. Select PC maps in the optional PC folder field instead. MCC is not supported.");
        uint bytes = Data.U32(header, 8), tags = Data.U32(header, 16), tagBytes = Data.U32(header, 20);
        Data.Require(Encoding.ASCII.GetString(header, 32, 32).Split('\0')[0] == name,
            $"{name}.map has a different internal map name.");
        Data.Require(bytes >= 2048 && bytes <= 0x11600000 && tags >= 2048 && tagBytes >= 40 &&
            tagBytes <= 0x1600000 && (long)tags + tagBytes <= bytes && entry.Length >= 2048 && entry.Length <= 0x11600000,
            $"{name}.map has invalid or incomplete map regions.");
        return header;
    }

    public static void PrepareMap(SourceEntry entry, string target, string name, MapInfo expected,
        CancellationToken ct, Action<double> progress)
    {
        byte[] header = CheckMap(entry, name, expected);
        using var source = entry.Open();
        long expectedBytes = Data.U32(header, 8);
        source.Position = 0;
        using var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None, Data.Chunk);
        using var expandedHash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        long written = 0;
        void Write(byte[] data, int length)
        {
            ct.ThrowIfCancellationRequested();
            written += length;
            Data.Require(written <= expectedBytes, $"{name}.map expands beyond its expected size.");
            output.Write(data, 0, length); expandedHash.AppendData(data, 0, length);
            progress(.2 + written / (double)expectedBytes * .8);
        }
        void Copy(Stream input)
        {
            byte[] buffer = new byte[Data.Chunk]; int n;
            while ((n = input.Read(buffer)) > 0) Write(buffer, n);
        }
        if (entry.Length == expectedBytes) Copy(source);
        else
        {
            Write(header, header.Length); source.Position = 2048;
            try { using var decoder = new ZLibStream(source, CompressionMode.Decompress, true); Copy(decoder); }
            catch (InvalidDataException error) { throw new SetupException($"{name}.map could not be decompressed: {error.Message}"); }
        }
        output.Flush(true);
        string expandedDigest = Convert.ToHexStringLower(expandedHash.GetHashAndReset());
        output.Dispose();
        Data.Require(Data.HashFile(target, ct) == expandedDigest, "Destination read-back failed. Check the SD card and try again.");
        Data.Require(written == expectedBytes && (expected.Sha256.Length == 0 || expandedDigest == expected.Sha256),
            $"{name}.map failed verification after preparation.");
    }

    static void AtomicWrite(string target, byte[] bytes)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        string temp = Path.Combine(Path.GetDirectoryName(target)!, ".halo-setup-" + Guid.NewGuid().ToString("N"));
        try
        {
            using (var f = new FileStream(temp, FileMode.CreateNew)) { f.Write(bytes); f.Flush(true); }
            Data.Require(Data.HashFile(temp) == Data.Hash(bytes), "SD-card read-back verification failed.");
            File.Move(temp, target, true);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }

    // Roll back normal I/O failures. After unplug/power loss, rerunning setup
    // verifies each map and sidecar and repairs incomplete groups before launch.
    static void Commit(string stage, string destination, IEnumerable<string> names)
    {
        string backup = Path.Combine(stage, "previous"); Directory.CreateDirectory(backup);
        var installed = new List<string>(); var moved = new List<string>();
        try
        {
            foreach (string name in names)
            {
                string target = SafePath(destination, name);
                if (File.Exists(target)) { File.Move(target, Path.Combine(backup, name)); moved.Add(name); }
                File.Move(Path.Combine(stage, name), target); installed.Add(name);
            }
        }
        catch
        {
            foreach (string name in installed.AsEnumerable().Reverse()) File.Delete(Path.Combine(destination, name));
            foreach (string name in moved) File.Move(Path.Combine(backup, name), Path.Combine(destination, name));
            throw;
        }
    }

    public static SetupResult Run(SetupOptions options, IProgress<SetupProgress>? progress = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        void Report(string message, double value) => progress?.Report(new(message, value));
        string destination = NormalizeDestination(options.Destination);
        Data.Require(Directory.Exists(destination), "Choose an existing SD card root or output folder.");
        string folderName = Path.GetFileName(Path.TrimEndingDirectorySeparator(destination));
        Data.Require(!folderName.Equals("3ds", StringComparison.OrdinalIgnoreCase) && !folderName.Equals("halo-source", StringComparison.OrdinalIgnoreCase),
            "Choose the SD card root (for example E:\\), not its 3ds or halo-source subfolder.");
        if (OperatingSystem.IsWindows() && Path.TrimEndingDirectorySeparator(destination) == Path.TrimEndingDirectorySeparator(Path.GetPathRoot(destination)!))
        {
            var drive = new DriveInfo(destination);
            Data.Require(drive.DriveFormat.Equals("FAT32", StringComparison.OrdinalIgnoreCase),
                "The 3DS needs a FAT32 SD card. This tool does not format or erase cards. You can also prepare a normal output folder and copy it to a FAT32 card later.");
        }
        Data.Require(File.Exists(options.Executable), "Select an existing Halo3DS.3dsx game build. Download it from the Halo3DS release or use your own build.");
        var exeInfo = new FileInfo(options.Executable);
        Data.Require(exeInfo.Length is > 32 and < 32 * Data.Chunk, "The selected game build has an invalid size.");
        byte[] executable = File.ReadAllBytes(options.Executable);
        Data.Require(executable.AsSpan(0, 4).SequenceEqual("3DSX"u8), "The selected game build is not a 3DSX file.");
        string game = SafePath(destination, "halo-source");
        string launcher = SafePath(destination, "3ds/Halo3DS/Halo3DS.3dsx");
        string manifestPath = SafePath(destination, "halo-source/setup-manifest.json");
        string hudPath = SafePath(destination, "halo-source/" + Hud.Name);
        string lockPath = SafePath(destination, "halo-source/.setup.lock");
        Dictionary<string, SourceEntry>? files = null;
        List<string> names = [];
        var catalog = new Dictionary<string, MapInfo>();
        byte[]? hud = null, loading = null;
        string compatibilityWarning = "";
        string loadingPath = SafePath(destination, "halo-source/" + Loading.Name);
        if (!options.LauncherOnly)
        {
            Report("Checking game files…", 0);
            files = Sources.Discover(options.Source);
            compatibilityWarning = Data.DiscWarning(options.Source, ct, text => Report(text, 0));
            if (compatibilityWarning.Length > 0) Report(compatibilityWarning, 0);
            names = Data.Catalog.Keys.Where(n => files.ContainsKey(n + ".map")).ToList();
            Data.Require(names.Contains("ui") && names.Count >= 2, "The folder needs ui.map and at least one campaign or multiplayer map.");
            foreach (string name in names)
            {
                ct.ThrowIfCancellationRequested();
                var entry = files[name + ".map"];
                var known = Data.Catalog[name];
                byte[] header = CheckMap(entry, name, known);
                // Recognized releases retain their byte-exact reference checks.
                // Other revisions are prepared using their own bounded tag data.
                using var input = entry.Open();
                using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
                byte[] block = new byte[Data.Chunk]; int count;
                while ((count = input.Read(block)) > 0) { ct.ThrowIfCancellationRequested(); hash.AppendData(block, 0, count); }
                string sourceHash = Convert.ToHexStringLower(hash.GetHashAndReset());
                catalog[name] = sourceHash == known.Sha256 || sourceHash == known.SourceSha256 ? known :
                    new MapInfo(known.Title, Data.U32(header, 8), "", entry.Length, sourceHash, new Dictionary<string, string>());
                string target = SafePath(destination, "halo-source/" + name + ".map");
                Data.Require(!files[name + ".map"].Path.Equals(target, StringComparison.OrdinalIgnoreCase),
                    "Source and destination maps are the same folder. Choose your original dump or a different output folder.");
                foreach (string sidecar in Data.Catalog[name].Sidecars.Keys) SafePath(destination, "halo-source/" + sidecar);
            }
            hud = Hud.Find(files, options.CompatibilityFile, destination);
            loading = Loading.Find(files, options.CompatibilityFile, destination);
        }
        else
        {
            Data.Require(File.Exists(Path.Combine(game, "ui.map")) && File.Exists(hudPath),
                "Prepare the Xbox game files first. Updating the launcher or adding PC maps requires an existing Xbox installation.");
        }
        var pcFiles = PcMaps.Inspect(options.PcMapsFolder, destination, ct, text => Report(text, 0));
        Directory.CreateDirectory(game);
        // Only one setup process may alter this output at once.
        using var destinationLock = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose);
        var pending = new List<string>();
        foreach (string name in names)
        {
            ct.ThrowIfCancellationRequested();
            Report($"Checking existing {Data.Catalog[name].Title}…", 1);
            var expected = new Dictionary<string, string>(catalog[name].Sidecars) { [name + ".map"] = catalog[name].Sha256 };
            bool good = expected.All(pair => File.Exists(Path.Combine(game, pair.Key)) && Data.HashFile(Path.Combine(game, pair.Key), ct) == pair.Value);
            if (!good) pending.Add(name);
        }
        // Conservative space check retains existing data until each replacement is verified.
        long required = pending.Sum(n => catalog[n].Bytes) + 128 * (long)Data.Chunk;
        required += pcFiles.Where(f => !File.Exists(SafePath(destination, "halo-source/pc-retail/" + f.Name))).Sum(f => f.Bytes);
        // Every texture archive in the hash-verified 24-map catalog is below
        // 64 MiB. Reserve a complete replacement while retaining the old file.
        var texturePending = names.Where(n => pending.Contains(n) || !Textures.Current(
            SafePath(game, n + ".map"), SafePath(game, n + ".ntx"), ct)).ToList();
        required += texturePending.Count * 64 * (long)Data.Chunk;
        var modelPending = names.Where(n => n != "ui" && (pending.Contains(n) || !ModelLods.Current(
            SafePath(game, n + ".map"), SafePath(game, n + ".hld"), ct))).ToList();
        required += modelPending.Count * (long)ModelLods.MaximumBytes;
        // On Unix, stat the destination filesystem, including a mounted SD card,
        // rather than the filesystem containing '/'. Windows uses the drive root.
        string spacePath = OperatingSystem.IsWindows() ? Path.GetPathRoot(destination)! : destination;
        Data.Require(new DriveInfo(spacePath).AvailableFreeSpace >= required,
            $"Please free at least {required / 1073741824.0:F1} GB on the destination and try again. Existing maps stay in place until replacements are verified.");
        for (int index = 0; index < pending.Count; index++)
        {
            ct.ThrowIfCancellationRequested();
            string name = pending[index]; var row = catalog[name];
            string stage = SafePath(game, ".halo-setup-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(stage);
            bool safeToRemove = true;
            try
            {
                string map = Path.Combine(stage, name + ".map");
                long lastReport = 0;
                PrepareMap(files![name + ".map"], map, name, row, ct, fraction =>
                {
                    long now = Environment.TickCount64;
                    if (now - lastReport >= 100 || fraction == 1)
                    { Report($"Preparing {row.Title} ({index + 1}/{pending.Count})…", 3 + 92 * (index + fraction * .8) / pending.Count); lastReport = now; }
                });
                Report($"Checking {row.Title} for the 3DS…", 3 + 92 * (index + .8) / pending.Count);
                string preparedHash = Data.HashFile(map, ct);
                Relocation.Generate(map, stage, name, ct);
                foreach (var (filename, hash) in row.Sidecars)
                    Data.Require(File.Exists(Path.Combine(stage, filename)) && Data.HashFile(Path.Combine(stage, filename), ct) == hash,
                        $"{name}: generated compatibility data did not pass verification.");
                Data.Require(Data.HashFile(map, ct) == preparedHash, "Destination read-back failed. Check the SD card and try again.");
                ct.ThrowIfCancellationRequested();
                safeToRemove = false; // Retain backups if the media disappears during rollback.
                Commit(stage, game, Directory.EnumerateFiles(stage).Where(f => f.EndsWith(".nrl", StringComparison.OrdinalIgnoreCase)).Select(f => Path.GetFileName(f)).Append(name + ".map").ToArray());
                safeToRemove = true;
            }
            finally
            {
                // Only this invocation's exact, generated staging directory is removed.
                if (safeToRemove && Directory.Exists(stage)) Directory.Delete(stage, true);
            }
        }
        // Derived texture archives can be added to an existing installation
        // without replacing its maps or profiles. Launcher-only stays quick.
        if (!options.LauncherOnly)
        foreach (string name in texturePending)
        {
            ct.ThrowIfCancellationRequested();
            string map = SafePath(game, name + ".map"), archive = SafePath(game, name + ".ntx");
            Report($"Preparing faster textures for {Data.Catalog[name].Title}…", 96);
            if (Textures.Current(map, archive, ct)) continue;
            string stage = SafePath(game, ".halo-setup-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(stage); bool safeToRemove = true;
            try
            {
                string temporary = Path.Combine(stage, name + ".ntx");
                Textures.Generate(map, temporary, ct);
                Data.Require(Textures.Current(map, temporary, ct), "Texture preparation did not pass verification. Please check the SD card.");
                ct.ThrowIfCancellationRequested(); safeToRemove = false;
                Commit(stage, game, new[] { name + ".ntx" }); safeToRemove = true;
            }
            finally { if (safeToRemove && Directory.Exists(stage)) Directory.Delete(stage, true); }
        }
        ct.ThrowIfCancellationRequested();
        Report("Installing the game launcher…", 97);
        // Derived Original-model meshes can be added without replacing maps,
        // saves or settings. A launcher-only update keeps its existing packs.
        foreach (string name in modelPending)
        {
            ct.ThrowIfCancellationRequested();
            Report($"Preparing Original 3DS models for {Data.Catalog[name].Title}…", 97);
            string stage = SafePath(game, ".halo-setup-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(stage); bool safeToRemove = true;
            try
            {
                string map = SafePath(game, name + ".map"), temporary = Path.Combine(stage, name + ".hld");
                ModelLods.Generate(map, temporary, ct);
                Data.Require(ModelLods.Current(map, temporary, ct), "Model preparation did not pass verification. Please check the SD card.");
                ct.ThrowIfCancellationRequested(); safeToRemove = false;
                Commit(stage, game, new[] { name + ".hld" }); safeToRemove = true;
            }
            finally { if (safeToRemove && Directory.Exists(stage)) Directory.Delete(stage, true); }
        }
        PcMaps.Install(pcFiles, destination, ct, text => Report(text, 97));
        if (hud != null) AtomicWrite(hudPath, hud);
        if (loading != null) AtomicWrite(loadingPath, loading);
        AtomicWrite(launcher, executable);
        RetireLegacyLauncher(destination);
        string dsp = Path.Combine(destination, "3ds", "dspfirm.cdc");
        bool dspPresent = File.Exists(dsp) && new FileInfo(dsp).Length > 0;
        if (!options.LauncherOnly)
            AtomicWrite(manifestPath, JsonSerializer.SerializeToUtf8Bytes(new { SetupVersion = 1, MapBuild = "source revision",
                Maps = names, LauncherSha256 = Data.Hash(executable), DspPresent = dspPresent }, new JsonSerializerOptions { WriteIndented = true }));
        Report("Ready to play", 100);
        return new(names.Count, pending.Count, names.Count - pending.Count, dspPresent, destination, pcFiles.Count, compatibilityWarning);
    }
}
