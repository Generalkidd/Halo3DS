// New3DS integration additions by Kevin Boggs.
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text.Json;

namespace HaloSetup;

public sealed class SetupException(string message) : Exception(message);
public sealed record MapInfo(string Title, long Bytes, string Sha256, long SourceBytes,
    string SourceSha256, Dictionary<string, string> Sidecars);
public sealed record SetupProgress(string Message, double Percent);
public sealed record SetupResult(int Maps, int Prepared, int AlreadyCorrect, bool DspPresent, string Destination, int PcFiles = 0, string CompatibilityWarning = "");

public static class Data
{
    public const string Build = "01.10.12.2276";
    public const int Chunk = 1024 * 1024;
    // Kevin Boggs: DiscWarning - recognize the three supported USA disc images by content.
    public static string DiscWarning(string source, CancellationToken ct, Action<string> report)
    {
        if (Directory.Exists(source)) return "Dumped folder: the original disc revision cannot be verified. Compatibility depends on the supplied files.";
        var supported = new Dictionary<long, string> {
            [3546677248L] = "dbf613567abf003fa0775ffa9679c044b92c0e5e",
            [3625713664L] = "bf3e1071bf8b7f0080cae86adc32b44233dbe8aa",
            [3728867328L] = "3accdedac9385bedc8020c8f07bd101ccf043d41"
        };
        using var file = File.OpenRead(source);
        if (supported.TryGetValue(file.Length, out string? expected)) {
            using var hash = SHA1.Create();
            byte[] buffer = new byte[Chunk]; int read; long last = 0;
            while ((read = file.Read(buffer, 0, buffer.Length)) > 0) {
                ct.ThrowIfCancellationRequested(); hash.TransformBlock(buffer, 0, read, buffer, 0);
                if (Environment.TickCount64 - last > 200) { report($"Identifying Xbox disc... {100 * file.Position / file.Length}%"); last = Environment.TickCount64; }
            }
            hash.TransformFinalBlock(new byte[0], 0, 0);
            if (Convert.ToHexStringLower(hash.Hash!) == expected) return "";
        }
        return "Untested Xbox disc revision. Setup will try these files, but gameplay compatibility is not guaranteed. Official support covers the USA original, Rev 1 and Rev 2 images.";
    }
    public static T Resource<T>(string name)
    {
        var assembly = typeof(Data).Assembly;
        var key = assembly.GetManifestResourceNames().Single(x => x.EndsWith("." + name, StringComparison.Ordinal));
        using var stream = assembly.GetManifestResourceStream(key)!;
        return JsonSerializer.Deserialize<T>(stream)!;
    }
    public static readonly Dictionary<string, MapInfo> Catalog = Resource<Dictionary<string, MapInfo>>("catalog.json");
    public static string Hash(byte[] data) => Convert.ToHexStringLower(SHA256.HashData(data));
    public static string HashFile(string path, CancellationToken ct = default)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var f = File.OpenRead(path);
        var buffer = new byte[Chunk];
        int count;
        while ((count = f.Read(buffer)) > 0) { ct.ThrowIfCancellationRequested(); hash.AppendData(buffer, 0, count); }
        return Convert.ToHexStringLower(hash.GetHashAndReset());
    }
    public static uint U32(byte[] data, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset, 4));
    public static ushort U16(byte[] data, int offset) => BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset, 2));
    public static void Require(bool condition, string message)
    { if (!condition) throw new SetupException(message); }
}

public sealed record SourceEntry(string Path, long Offset, long Length)
{
    public static SourceEntry FromFile(string path) => new(System.IO.Path.GetFullPath(path), 0, new FileInfo(path).Length);
    public Stream Open() => new SliceStream(Path, Offset, Length);
    public byte[] Head(int bytes)
    {
        using var s = Open();
        var data = new byte[(int)Math.Min(bytes, Length)];
        s.ReadExactly(data);
        return data;
    }
    public byte[] Small()
    {
        Data.Require(Length <= 32 * Data.Chunk, "The compatibility file is unexpectedly large.");
        return Head((int)Length);
    }
}

// File-backed slice prevents decompression from reading outside its XISO entry.
internal sealed class SliceStream : Stream
{
    private readonly FileStream file;
    private readonly long start, length;
    private long position;
    public SliceStream(string path, long offset, long size)
    { file = File.OpenRead(path); start = offset; length = size; file.Position = offset; }
    public override int Read(byte[] b, int o, int n) => Read(b.AsSpan(o, n));
    public override int Read(Span<byte> b)
    {
        int n = file.Read(b[..(int)Math.Min(b.Length, length - position)]);
        if (n == 0 && position < length) throw new SetupException("A source file is incomplete or was disconnected.");
        position += n; return n;
    }
    public override long Seek(long offset, SeekOrigin origin)
    {
        long target = origin switch { SeekOrigin.Begin => offset, SeekOrigin.Current => position + offset, _ => length + offset };
        Data.Require(target >= 0 && target <= length, "Invalid source file range.");
        file.Position = start + target; return position = target;
    }
    public override bool CanRead => true;
    public override bool CanSeek => true;
    public override bool CanWrite => false;
    public override long Length => length;
    public override long Position { get => position; set => Seek(value, SeekOrigin.Begin); }
    public override void Flush() { }
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] b, int o, int n) => throw new NotSupportedException();
    protected override void Dispose(bool disposing) { if (disposing) file.Dispose(); base.Dispose(disposing); }
}
