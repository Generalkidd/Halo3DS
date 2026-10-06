using System.Text;
namespace HaloSetup;

/// <summary>Bounded read-only XDVDFS reader. Layout reference: XboxDev/extract-xiso.
/// No external extraction command is run and no image filenames become output paths.</summary>
public sealed class Xiso
{
    public Dictionary<string, SourceEntry> Files { get; } = new(StringComparer.OrdinalIgnoreCase);
    readonly string path;
    readonly long length;
    long partition;
    int entries;
    readonly HashSet<(uint, uint)> tables = [];
    public Xiso(string path)
    {
        this.path = Path.GetFullPath(path);
        using var f = File.OpenRead(path);
        length = f.Length;
        foreach (long start in new long[] { 0, 0xFD90000, 0x2080000, 0x18300000 })
        {
            if (start + 0x10800 > length) continue;
            f.Position = start + 0x10000;
            byte[] header = new byte[2048]; f.ReadExactly(header);
            if (!header.AsSpan(0, 20).SequenceEqual("MICROSOFT*XBOX*MEDIA"u8)) continue;
            Data.Require(header.AsSpan(2028, 20).SequenceEqual(header.AsSpan(0, 20)), "The XISO header is damaged.");
            partition = start;
            ReadDirectory(f, Data.U32(header, 20), Data.U32(header, 24), "", 0);
            return;
        }
        throw new SetupException("This is not a supported Xbox XISO. Choose an extracted maps folder instead. ZIP/7z files and split images must be extracted or joined first.");
    }
    void ReadDirectory(FileStream f, uint sector, uint size, string prefix, int depth)
    {
        long offset = partition + sector * 2048L;
        Data.Require(depth <= 24 && size <= 16 * Data.Chunk && offset + size <= length,
            "The XISO contains an invalid directory.");
        if (size == 0) return;
        Data.Require(tables.Add((sector, size)), "The XISO contains a repeated directory.");
        f.Position = offset;
        byte[] table = new byte[size]; f.ReadExactly(table);
        var pending = new Stack<int>(); pending.Push(0);
        var visited = new HashSet<int>();
        while (pending.TryPop(out int pos))
        {
            Data.Require(visited.Add(pos) && pos + 14 <= size, "The XISO directory links are damaged.");
            int left = Data.U16(table, pos), right = Data.U16(table, pos + 2);
            uint fileSector = Data.U32(table, pos + 4), bytes = Data.U32(table, pos + 8);
            int flags = table[pos + 12], n = table[pos + 13];
            Data.Require(n > 0 && pos + 14 + n <= size, "The XISO contains an invalid filename.");
            string name = Encoding.Latin1.GetString(table, pos + 14, n);
            Data.Require(name is not "." and not ".." && name.IndexOfAny(['/', '\\', ':', '\0']) < 0,
                "The XISO contains an unsafe filename.");
            if (left != 0) pending.Push(left * 4);
            if (right != 0) pending.Push(right * 4);
            Data.Require(++entries <= 100000, "This image contains too many files.");
            string full = prefix + name;
            if ((flags & 0x10) != 0) ReadDirectory(f, fileSector, bytes, full + "/", depth + 1);
            else
            {
                long start = partition + fileSector * 2048L;
                Data.Require(start + bytes <= length, "The XISO is incomplete. Please dump the disc again.");
                Data.Require(Files.TryAdd(full, new(path, start, bytes)), "The XISO contains duplicate filenames.");
            }
        }
    }
}

public static class Sources
{
    public static Dictionary<string, SourceEntry> Discover(string source)
    {
        source = Path.GetFullPath(source);
        if (Directory.Exists(source))
        {
            var candidates = new List<string> { source };
            candidates.AddRange(Directory.EnumerateDirectories(source).Where(p =>
                Path.GetFileName(p).Equals("maps", StringComparison.OrdinalIgnoreCase) ||
                Path.GetFileName(p).Equals("halo-source", StringComparison.OrdinalIgnoreCase)));
            var matches = candidates.Where(p => Directory.EnumerateFiles(p).Any(f =>
                Path.GetFileName(f).Equals("ui.map", StringComparison.OrdinalIgnoreCase))).ToList();
            Data.Require(matches.Count == 1, "Choose the maps folder containing ui.map, or the extracted Halo game folder. Exactly one set of maps is required.");
            string folder = matches[0];
            var files = Directory.EnumerateFiles(folder).ToDictionary(p => Path.GetFileName(p), SourceEntry.FromFile, StringComparer.OrdinalIgnoreCase);
            foreach (var nearby in new[] { Directory.GetParent(folder)?.FullName, source }.Where(p => p != null).Distinct())
                foreach (var f in Directory.EnumerateFiles(nearby!))
                    if (Path.GetFileName(f).Equals("default.xbe", StringComparison.OrdinalIgnoreCase) ||
                        Path.GetFileName(f).Equals(Hud.Name, StringComparison.OrdinalIgnoreCase) ||
                        Path.GetFileName(f).Equals(Loading.Name, StringComparison.OrdinalIgnoreCase))
                        files.TryAdd(Path.GetFileName(f), SourceEntry.FromFile(f));
            return files!;
        }
        Data.Require(File.Exists(source), "Choose an existing Xbox XISO or maps folder.");
        var image = new Xiso(source);
        var folders = image.Files.Keys.Where(n => n.Split('/').Last().Equals("ui.map", StringComparison.OrdinalIgnoreCase))
            .Select(n => n[..^6]).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        Data.Require(folders.Count == 1, "The XISO must contain one Halo maps folder with ui.map.");
        string prefix = folders[0];
        var result = image.Files.Where(p => p.Key.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && !p.Key[prefix.Length..].Contains('/'))
            .ToDictionary(p => p.Key[prefix.Length..], p => p.Value, StringComparer.OrdinalIgnoreCase);
        if (image.Files.TryGetValue("default.xbe", out var xbe)) result["default.xbe"] = xbe;
        return result;
    }
}
