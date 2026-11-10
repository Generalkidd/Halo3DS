// New3DS integration additions by Kevin Boggs.
using System.Text;

namespace HaloSetup;

// Kevin Boggs: PcMapFile - an inspected owner-supplied file, never bundled game content.
public sealed record PcMapFile(string Source, string Name, string Hash, long Bytes);

// Kevin Boggs: PcMaps - optional PC retail multiplayer data shared by both graphical installers.
public static class PcMaps
{
    static readonly string[] Multiplayer = { "beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout",
        "damnation", "dangercanyon", "deathisland", "gephyrophobia", "hangemhigh", "icefields", "infinity",
        "longest", "prisoner", "putput", "ratrace", "sidewinder", "timberland", "wizard" };

    // Kevin Boggs: Inspect - validate all selected PC files before destination writes.
    public static List<PcMapFile> Inspect(string? folder, string destination, CancellationToken ct,
        Action<string> report)
    {
        var result = new List<PcMapFile>();
        if (folder == null || folder.Trim().Length == 0) return result;
        folder = Path.GetFullPath(folder.Trim());
        Data.Require(Directory.Exists(folder), "Choose your installed Halo PC maps folder. PC disc images must be extracted first.");
        if (Directory.Exists(Path.Combine(folder, "maps"))) folder = Path.Combine(folder, "maps");
        var files = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (string file in Directory.EnumerateFiles(folder))
        {
            string name = Path.GetFileName(file).ToLowerInvariant();
            if (!name.EndsWith(".map", StringComparison.Ordinal)) continue;
            Data.Require(!files.ContainsKey(name), "The PC folder contains duplicate filenames: " + name);
            files.Add(name, file);
        }
        foreach (string required in new[] { "ui.map", "bitmaps.map", "sounds.map" })
            Data.Require(files.ContainsKey(required), "The PC maps folder is missing " + required + ". Choose the maps folder from an installed Halo: Combat Evolved PC retail game, not Custom Edition or MCC.");
        var names = Multiplayer.Select(n => n + ".map").Where(files.ContainsKey).ToList();
        Data.Require(names.Count > 0, "The PC folder needs at least one supported retail multiplayer map.");
        names.AddRange(new[] { "ui.map", "bitmaps.map", "sounds.map" });
        if (files.ContainsKey("loc.map")) names.Add("loc.map");
        foreach (string name in names)
        {
            ct.ThrowIfCancellationRequested();report("Checking PC " + name + "...");
            string source = files[name], target = Installer.SafePath(destination, "halo-source/pc-retail/" + name);
            Data.Require(!source.Equals(target, StringComparison.OrdinalIgnoreCase), "Choose a PC source outside the destination's halo-source/pc-retail folder.");
            Check(source, name);
            string hash = Data.HashFile(source, ct);
            Data.Require(!Directory.Exists(target), "A folder is blocking the PC file: " + name);
            Data.Require(!File.Exists(target) || Data.HashFile(target, ct) == hash,
                "A different PC file is already installed: " + name + ". Use an empty output folder to prepare a different PC release. Existing game files were kept.");
            result.Add(new PcMapFile(source, name, hash, new FileInfo(source).Length));
        }
        return result;
    }

    // Kevin Boggs: Check - reject Xbox, Custom Edition, MCC and truncated PC caches.
    static void Check(string path, string name)
    {
        using var file = File.OpenRead(path);
        Data.Require(file.Length >= 2048 && file.Length < 2147483648L, "Invalid or incomplete PC file: " + name);
        byte[] header = new byte[2048];file.ReadExactly(header);
        if (name == "bitmaps.map" || name == "sounds.map" || name == "loc.map")
        {
            uint kind = name == "bitmaps.map" ? 1u : name == "sounds.map" ? 2u : 3u;
            uint names = Data.U32(header, 4), index = Data.U32(header, 8), count = Data.U32(header, 12);
            Data.Require(Data.U32(header, 0) == kind && names >= 16 && names <= index &&
                index <= file.Length && count > 0 && (long)count * 12 <= file.Length - index,
                "Invalid PC resource file: " + name);
            return;
        }
        uint tags = Data.U32(header, 16), size = Data.U32(header, 20);
        string expected = name.Substring(0, name.Length - 4);
        Data.Require(Encoding.ASCII.GetString(header, 0, 4) == "daeh" &&
            Encoding.ASCII.GetString(header, 2044, 4) == "toof" && Data.U32(header, 4) == 7 &&
            Data.U32(header, 8) == file.Length && tags >= 2048 && size >= 40 &&
            (long)tags + size <= file.Length && Encoding.ASCII.GetString(header, 32, 32).Split('\0')[0] == expected &&
            Data.U16(header, 96) == (name == "ui.map" ? 2 : 1),
            name + " is not a complete Halo: Combat Evolved PC retail map. Custom Edition, MCC and Xbox maps cannot be used in the optional PC folder.");
    }

    // Kevin Boggs: Install - cancellable, verified copies with per-file atomic publication.
    public static void Install(List<PcMapFile> files, string destination, CancellationToken ct, Action<string> report)
    {
        foreach (PcMapFile file in files)
        {
            ct.ThrowIfCancellationRequested();
            string target = Installer.SafePath(destination, "halo-source/pc-retail/" + file.Name);
            if (File.Exists(target))
            {
                Data.Require(Data.HashFile(target, ct) == file.Hash, "The installed PC file changed during setup: " + file.Name);
                continue;
            }
            report("Copying PC " + file.Name + "...");
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            string partial = Installer.SafePath(destination, "halo-source/pc-retail/.partial-" + Guid.NewGuid().ToString("N"));
            try
            {
                using (var source = File.OpenRead(file.Source))
                using (var output = new FileStream(partial, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    byte[] buffer = new byte[65536];int n;
                    while ((n = source.Read(buffer, 0, buffer.Length)) > 0)
                    { ct.ThrowIfCancellationRequested();output.Write(buffer, 0, n); }
                }
                Data.Require(new FileInfo(partial).Length == file.Bytes && Data.HashFile(partial, ct) == file.Hash,
                    "PC copy verification failed: " + file.Name + ". Check the SD card and retry.");
                ct.ThrowIfCancellationRequested();
                Installer.SafePath(destination, "halo-source/pc-retail/" + file.Name);
                File.Move(partial, target);
            }
            finally { if (File.Exists(partial)) File.Delete(partial); }
        }
    }
}
