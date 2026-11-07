#if NET10_0_OR_GREATER
using System.Text;

namespace HaloSetup;

public sealed record TextureLodSample(string Label, string Bitmap, int BitmapOffset, int FirstMip);

public static partial class Textures
{
    const uint TextureLodMagic = 0x314d584e; // NXM1
    const int TextureLodHeaderBytes = 32, TextureLodEntryBytes = 64;
    const int TextureLodSide = 64, TextureLodLevels = 4, TextureLodChainBytes = 16320;

    // Known descriptors in the catalogued retail b30.map. Sand and cliff rock
    // are primary detail inputs of BSP materials. Brushed metal is authored
    // micro detail on the metal strips and generator panel BSP materials.
    static readonly (string Label, string Bitmap, int Offset)[] TextureLodSources =
    [
        ("Sand detail", @"levels\b30\bitmaps\detail sand", 0xef23b0),
        ("Cliff rock detail", @"levels\b30\bitmaps\detail cliff rock", 0xef2e84),
        ("Brushed metal detail", @"levels\b30\bitmaps\detail brushed metal", 0xbb0af0)
    ];

    static void TextureLodWriteChain(FileStream map, TexturePlan plan, byte[] tags, Span<byte> chain, CancellationToken ct)
    {
        Data.Require(plan.Format == 14 && !plan.Lightmap && plan.Width == TextureLodSide &&
            plan.Height == TextureLodSide && plan.TargetWidth == TextureLodSide && plan.TargetHeight == TextureLodSide &&
            plan.SourceMip + TextureLodLevels - 1 <= Data.U16(tags, plan.At + 20),
            "The fixture bitmap does not have the required authored DXT1 mip chain.");
        long sourceEnd = (long)Data.U32(tags, plan.At + 24) + Data.U32(tags, plan.At + 28);
        long source = plan.Source;
        int destination = 0;
        for (int level = 0; level < TextureLodLevels; ++level)
        {
            ct.ThrowIfCancellationRequested();
            int side = TextureLodSide >> level;
            int sourceBytes = side * side / 2; // One eight-byte DXT1 block per 4x4 texels.
            Data.Require(source + sourceBytes <= sourceEnd, "An authored fixture mip exceeds its bitmap extent.");
            byte[] rgba = Convert(Read(map, source, sourceBytes), plan.Format, side, side, side, side);
            for (int texel = 0; texel < rgba.Length; texel += 4)
            {
                Data.Require(rgba[texel] == 255, "An authored fixture mip contains non-opaque texels.");
                // Keep the existing vertical orientation and Morton ordering.
                // Native RGB8 stores B,G,R from the canonical A,B,G,R bytes.
                rgba.AsSpan(texel + 1, 3).CopyTo(chain.Slice(destination, 3));
                destination += 3;
            }
            source += sourceBytes;
        }
        Data.Require(destination == chain.Length, "Fixture mip chain length did not match its layout.");
    }

    public static IReadOnlyList<TextureLodSample> GenerateLodFixture(string map, string output, CancellationToken ct = default)
    {
        Data.Require(Path.GetFileNameWithoutExtension(map) == "b30", "The texture LOD fixture requires expanded retail b30.map.");
        output = Path.GetFullPath(output);
        string directory = Path.GetDirectoryName(output)!;
        Data.Require(!File.Exists(output) && !Directory.Exists(output), "Texture LOD output already exists.");
        Data.Require(Directory.Exists(directory), "Create the texture LOD output directory before running the cooker.");

        using var input = File.OpenRead(map);
        CandidateMapHash(input, map, ct);
        var plans = Plans(input, map, ct, out _, out byte[] tags);
        int count = TextureLodSources.Length;
        int payloadStart = TextureLodHeaderBytes + count * TextureLodEntryBytes;
        byte[] pack = new byte[payloadStart + count * TextureLodChainBytes];
        var samples = new List<TextureLodSample>(count);
        for (int i = 0; i < count; ++i)
        {
            ct.ThrowIfCancellationRequested();
            var (label, bitmap, offset) = TextureLodSources[i];
            var plan = plans.SingleOrDefault(p => p.At == offset && p.Limit == TextureLodSide)
                ?? throw new SetupException($"The current 64-pixel plan is unavailable for {bitmap}.");
            int payload = payloadStart + i * TextureLodChainBytes;
            var chain = pack.AsSpan(payload, TextureLodChainBytes);
            TextureLodWriteChain(input, plan, tags, chain, ct);

            int entry = TextureLodHeaderBytes + i * TextureLodEntryBytes;
            Encoding.ASCII.GetBytes(label).CopyTo(pack, entry);
            CandidateWord(pack, entry + 32, (uint)payload);
            CandidateWord(pack, entry + 36, TextureLodChainBytes);
            CandidateWord(pack, entry + 40, Hash(chain.ToArray()));
            CandidateWord(pack, entry + 44, (uint)offset);
            CandidateWord(pack, entry + 48, (uint)plan.SourceMip);
            CandidateWord(pack, entry + 52, TextureLodSide);
            CandidateWord(pack, entry + 56, TextureLodSide);
            CandidateWord(pack, entry + 60, TextureLodLevels);
            samples.Add(new(label, bitmap, offset, plan.SourceMip));
        }
        uint directoryHash = Hash(pack.AsSpan(TextureLodHeaderBytes, count * TextureLodEntryBytes).ToArray());
        uint[] header = [TextureLodMagic, 1, (uint)count, TextureLodEntryBytes, ComparisonRgb8,
            (uint)payloadStart, (uint)pack.Length, directoryHash];
        for (int i = 0; i < header.Length; ++i) CandidateWord(pack, i * 4, header[i]);

        string staged = Path.Combine(directory, ".halo-texture-lod-" + Guid.NewGuid().ToString("N"));
        bool stagedCreated = false;
        try
        {
            using (var target = new FileStream(staged, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None))
            {
                stagedCreated = true;
                target.Write(pack);
                target.Flush(true);
                Data.Require(Read(target, 0, pack.Length).AsSpan().SequenceEqual(pack),
                    "Texture LOD pack failed read-back verification.");
            }
            ct.ThrowIfCancellationRequested();
            File.Move(staged, output, false);
            stagedCreated = false;
            return samples;
        }
        finally
        {
            if (stagedCreated) File.Delete(staged);
        }
    }
}
#endif
