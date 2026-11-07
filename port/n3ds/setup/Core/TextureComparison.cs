#if NET10_0_OR_GREATER
using System.Runtime.InteropServices;

namespace HaloSetup;

public enum TextureComparisonMode : uint { Raw = 1, Lz4Hc = 2, Compact = 3 }

public sealed record TextureComparisonResult(long ArchiveBytes, int Entries, int RawRgba8,
    int Lz4Rgba8, int Rgb8, int La8, string? Lz4Version);

public static partial class Textures
{
    const uint ComparisonSchema = 2;
    const uint ComparisonRgba8 = 1, ComparisonRgb8 = 2, ComparisonLa8 = 3;
    const uint ComparisonRaw = 0, ComparisonLz4 = 1;
    const int ComparisonLz4Limit = 256 * 1024;

    // Exact catalogued-map offsets of filtered BSP lightmap descriptors.
    // For these groups the only bitmap dependencies in global tags and all
    // streamed BSPs are their owning BSP lightmap references. Mirror the
    // runtime gate. Unknown maps and other uses retain CPU-readable RGBA8.
    static readonly (int Start, int Count)[] ComparisonA10Lightmaps =
    [
        (0xd6e94c, 30), (0xd73288, 38), (0xd75488, 36), (0xd77270, 38),
        (0xd7867c, 50), (0xd7a0bc, 47), (0xd7ca60, 54), (0xd7d6ac, 7),
        (0xd801bc, 9)
    ];
    static readonly (int Start, int Count)[] ComparisonB30Lightmaps =
        [(0xeeee8c, 26), (0xefcc80, 29)];

    static bool ComparisonAllowsCompact(string name, TexturePlan plan)
    {
        if (!plan.Lightmap || plan.Format != 6) return false;
        var ranges = name switch
        {
            "a10" => ComparisonA10Lightmaps,
            "b30" => ComparisonB30Lightmaps,
            _ => []
        };
        foreach (var (start, count) in ranges)
        {
            int offset = plan.At - start;
            if (offset >= 0 && offset < count * 48 && offset % 48 == 0) return true;
        }
        return false;
    }

    sealed record ComparisonPayload(uint Offset, uint StoredBytes, uint ResidentBytes,
        uint Format, uint Codec, uint NativeHash, uint CanonicalHash);

    static byte[] ComparisonCanonical(FileStream input, TexturePlan plan)
    {
        byte[] source = Read(input, plan.Source, plan.Bytes);
        return plan.Lightmap
            ? FilterLightmap(source, plan.Width, plan.Height, plan.TargetWidth, plan.TargetHeight)
            : Convert(source, plan.Format, plan.Width, plan.Height, plan.TargetWidth, plan.TargetHeight);
    }

    static byte[] ComparisonCompact(byte[] rgba, out uint format)
    {
        bool opaque = true, gray = true;
        for (int i = 0; i < rgba.Length; i += 4)
        {
            opaque &= rgba[i] == 255;
            gray &= rgba[i + 1] == rgba[i + 2] && rgba[i + 2] == rgba[i + 3];
        }
        if (!gray && !opaque)
        {
            format = ComparisonRgba8;
            return rgba;
        }
        // The original tiled array stores A,B,G,R in memory. Compact
        // texels keep its Morton order and store A,L or B,G,R respectively.
        int stride = gray ? 2 : 3;
        format = gray ? ComparisonLa8 : ComparisonRgb8;
        var native = new byte[rgba.Length / 4 * stride];
        for (int i = 0, at = 0; i < rgba.Length; i += 4, at += stride)
            rgba.AsSpan(i + (gray ? 0 : 1), stride).CopyTo(native.AsSpan(at, stride));
        return native;
    }

    static bool ComparisonExact(byte[] native, uint format, byte[] rgba)
    {
        if (format == ComparisonRgba8) return native.AsSpan().SequenceEqual(rgba);
        int stride = format == ComparisonLa8 ? 2 : 3;
        if (native.Length != rgba.Length / 4 * stride) return false;
        for (int i = 0, at = 0; i < rgba.Length; i += 4, at += stride)
        {
            if (format == ComparisonLa8)
            {
                if (native[at] != rgba[i] || native[at + 1] != rgba[i + 1] ||
                    native[at + 1] != rgba[i + 2] || native[at + 1] != rgba[i + 3]) return false;
            }
            else if (rgba[i] != 255 || !native.AsSpan(at, 3).SequenceEqual(rgba.AsSpan(i + 1, 3)))
                return false;
        }
        return true;
    }

    static void ComparisonVerify(FileStream input, FileStream packed, byte[] header, byte[] directory,
        Dictionary<TexturePlan, ComparisonPayload> payloads, ComparisonLz4Codec? lz4, CancellationToken ct)
    {
        Data.Require(packed.Length == Data.U32(header, 56) &&
            Read(packed, 0, header.Length).AsSpan().SequenceEqual(header) &&
            Read(packed, header.Length, directory.Length).AsSpan().SequenceEqual(directory),
            "Comparison archive directory failed read-back verification.");
        long end = CandidatePayloadStart;
        foreach (var (plan, payload) in payloads.OrderBy(entry => entry.Value.Offset))
        {
            ct.ThrowIfCancellationRequested();
            long next = (end + 3) & ~3L;
            Data.Require(next == payload.Offset &&
                Read(packed, end, (int)(next - end)).All(value => value == 0),
                "Invalid comparison archive padding or payload overlap.");
            byte[] stored = Read(packed, payload.Offset, checked((int)payload.StoredBytes));
            byte[] native = payload.Codec == ComparisonLz4
                ? lz4!.Decode(stored, checked((int)payload.ResidentBytes))
                : stored;
            byte[] rgba = ComparisonCanonical(input, plan);
            Data.Require(native.Length == payload.ResidentBytes && Hash(native) == payload.NativeHash &&
                Hash(rgba) == payload.CanonicalHash && ComparisonExact(native, payload.Format, rgba),
                "Comparison archive payload differs from the canonical RGBA8 image.");
            end = (long)payload.Offset + payload.StoredBytes;
        }
        Data.Require(end == packed.Length, "Unexpected trailing comparison archive data.");
    }

    public static TextureComparisonResult GenerateComparison(string map, string output, TextureComparisonMode mode,
        string? lz4Library = null, CancellationToken ct = default)
    {
        Data.Require(mode is TextureComparisonMode.Raw or TextureComparisonMode.Lz4Hc or TextureComparisonMode.Compact,
            "Invalid texture comparison mode.");
        Data.Require(lz4Library == null || mode == TextureComparisonMode.Lz4Hc,
            "The optional LZ4 library applies to the lz4hc mode.");
        output = Path.GetFullPath(output);
        string directory = Path.GetDirectoryName(output)!;
        Data.Require(!File.Exists(output) && !Directory.Exists(output), "Comparison output already exists.");
        Data.Require(Directory.Exists(directory), "Create the comparison output directory before running the cooker.");
        ct.ThrowIfCancellationRequested();
        using var lz4 = mode == TextureComparisonMode.Lz4Hc ? new ComparisonLz4Codec(lz4Library) : null;
        using var input = File.OpenRead(map);
        byte[] identity = CandidateMapHash(input, map, ct);
        var plans = Plans(input, map, ct, out byte[] mapHeader, out byte[] tags);
        Data.Require(plans.Count > 0, "No compatible comparison texture plans were found.");
        CandidateSortPlans(plans);
        long limit = CandidateArchiveBytes(plans) + (long)plans.Count * 3;
        Data.Require(limit <= int.MaxValue, "Comparison archive exceeds the console file-size limit.");
        string volume = OperatingSystem.IsWindows() ? Path.GetPathRoot(directory)! : directory;
        Data.Require(new DriveInfo(volume).AvailableFreeSpace >= limit,
            "The output volume does not have room for the uncompressed comparison archive bound.");
        string name = Path.GetFileNameWithoutExtension(map);
        // Exact aliases keep their order and share the same extent in every
        // arm. A compact alias must be safe for every member of the group.
        var rgbaOnly = new HashSet<TexturePlan>(CandidatePayloadComparer.Instance);
        if (mode == TextureComparisonMode.Compact)
            foreach (var plan in plans)
                if (!ComparisonAllowsCompact(name, plan)) rgbaOnly.Add(plan);

        var payloads = new Dictionary<TexturePlan, ComparisonPayload>(CandidatePayloadComparer.Instance);
        var index = new byte[plans.Count * CandidateEntryBytes];
        string staged = Path.Combine(directory, ".halo-texture-" + Guid.NewGuid().ToString("N"));
        bool stagedCreated = false;
        try
        {
            long archiveBytes;
            using (var target = new FileStream(staged, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None))
            {
                stagedCreated = true;
                target.Position = CandidatePayloadStart;
                for (int i = 0; i < plans.Count; ++i)
                {
                    ct.ThrowIfCancellationRequested();
                    var plan = plans[i];
                    if (!payloads.TryGetValue(plan, out var payload))
                    {
                        byte[] rgba = ComparisonCanonical(input, plan);
                        uint format = ComparisonRgba8;
                        byte[] native = mode == TextureComparisonMode.Compact && !rgbaOnly.Contains(plan)
                            ? ComparisonCompact(rgba, out format) : rgba;
                        uint canonicalHash = Hash(rgba);
                        byte[]? encoded = lz4?.Encode(native);
                        byte[] stored = encoded ?? native;
                        while ((target.Position & 3) != 0) target.WriteByte(0);
                        payload = new(checked((uint)target.Position), (uint)stored.Length, (uint)native.Length,
                            format, encoded == null ? ComparisonRaw : ComparisonLz4,
                            format == ComparisonRgba8 ? canonicalHash : Hash(native), canonicalHash);
                        target.Write(stored);
                        payloads.Add(plan, payload);
                    }
                    uint[] entry = CandidateEntry(plan, payload.Offset, payload.StoredBytes, payload.NativeHash);
                    entry[6] = payload.Format;
                    if (payload.Format != ComparisonRgba8) entry[11] = 0;
                    entry[12] = payload.ResidentBytes;
                    entry[22] = payload.Codec;
                    entry[23] = payload.CanonicalHash;
                    for (int word = 0; word < entry.Length; ++word)
                        CandidateWord(index, i * CandidateEntryBytes + word * 4, entry[word]);
                }
                archiveBytes = target.Length;
                var header = new byte[CandidateHeaderBytes];
                uint[] fields = [CandidateMagic, ComparisonSchema, CandidateHeaderBytes, CandidateEntryBytes,
                    CandidatePolicy, 0x01020304, checked((uint)input.Length), Hash(mapHeader),
                    (uint)tags.Length, CandidateCrc(tags), (uint)plans.Count, CandidateHeaderBytes,
                    (uint)index.Length, CandidatePayloadStart, checked((uint)archiveBytes), Hash(index)];
                for (int i = 0; i < fields.Length; ++i) CandidateWord(header, i * 4, fields[i]);
                Array.Copy(identity, 0, header, 64, identity.Length);
                CandidateWord(header, 100, (uint)mode);
                CandidateWord(header, 96, Hash(header));
                target.Position = 0;
                target.Write(header);
                target.Write(index);
                target.Flush(true);
                ComparisonVerify(input, target, header, index, payloads, lz4, ct);
            }
            ct.ThrowIfCancellationRequested();
            File.Move(staged, output, false);
            stagedCreated = false;
            return new(archiveBytes, plans.Count,
                payloads.Values.Count(p => p.Format == ComparisonRgba8 && p.Codec == ComparisonRaw),
                payloads.Values.Count(p => p.Codec == ComparisonLz4),
                payloads.Values.Count(p => p.Format == ComparisonRgb8),
                payloads.Values.Count(p => p.Format == ComparisonLa8), lz4?.Version);
        }
        finally
        {
            if (stagedCreated) File.Delete(staged);
        }
    }

    // The host's official liblz4 is optional. Only the explicit LZ4HC
    // development command loads it, and the runtime only needs block decoding.
    sealed class ComparisonLz4Codec : IDisposable
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate int Compress([In] byte[] source, [Out] byte[] destination, int sourceBytes, int capacity, int level);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate int Decompress([In] byte[] source, [Out] byte[] destination, int sourceBytes, int capacity);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate IntPtr VersionString();

        readonly IntPtr library;
        readonly Compress compress;
        readonly Decompress decompress;
        public string Version { get; }

        static T Export<T>(IntPtr library, string symbol) where T : Delegate
        {
            Data.Require(NativeLibrary.TryGetExport(library, symbol, out var address), $"The LZ4 library is missing {symbol}.");
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }

        public ComparisonLz4Codec(string? path)
        {
            string[] names = path != null ? [Path.GetFullPath(path)] :
                OperatingSystem.IsWindows() ? ["liblz4.dll", "lz4.dll"] :
                OperatingSystem.IsMacOS() ? ["liblz4.1.dylib", "liblz4.dylib"] :
                ["liblz4.so.1", "liblz4.so"];
            foreach (string name in names)
                if (NativeLibrary.TryLoad(name, out library)) break;
            Data.Require(library != IntPtr.Zero,
                "LZ4HC mode requires an official liblz4 shared library. Install liblz4 or pass --lz4-library PATH.");
            try
            {
                compress = Export<Compress>(library, "LZ4_compress_HC");
                decompress = Export<Decompress>(library, "LZ4_decompress_safe");
                Version = Marshal.PtrToStringAnsi(Export<VersionString>(library, "LZ4_versionString")()) ?? "unknown";
            }
            catch
            {
                NativeLibrary.Free(library);
                throw;
            }
        }

        public byte[]? Encode(byte[] native)
        {
            // The maximum possible alignment pad is three bytes. Even with
            // padding, compressed storage must save space against raw RGBA8.
            var buffer = new byte[Math.Min(native.Length - 4, ComparisonLz4Limit)];
            int size = compress(native, buffer, native.Length, buffer.Length, 9);
            return size > 0 ? buffer.AsSpan(0, size).ToArray() : null;
        }

        public byte[] Decode(byte[] stored, int residentBytes)
        {
            var native = new byte[residentBytes];
            Data.Require(decompress(stored, native, stored.Length, native.Length) == native.Length,
                "Comparison LZ4 block failed exact-length decompression.");
            return native;
        }

        public void Dispose() => NativeLibrary.Free(library);
    }
}
#endif
