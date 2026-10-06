namespace HaloSetup;

/// <summary>Optional retail loading artwork, extracted locally; no pixels are bundled.</summary>
public static class Loading
{
    public const string Name = "ntsc2276-loading.bin";
    public const string AssetHash = "47838bfd25e0f4a763a78ec1da4c1ee61b6a4f56c1e7eb2d05d5ea9dc1bd59dd";
    const string XbeHash = "ed3a8e962351ad6c4b3b620768fb6a0bda658963390252439ea036d5ede3a3ac";
    public static byte[]? Extract(byte[] data)
    {
        string hash = Data.Hash(data);
        if (hash == AssetHash) return data;
        if (hash != XbeHash) return null;
        // Retail progress-bar loader: dimensions followed by run-length/intensity
        // nibbles, at VA 0x210988 (.data file offset 0x20a728), 26596 RLE bytes.
        byte[] result = new byte[26604];
        Array.Copy(data, 0x20a728, result, 0, result.Length);
        Data.Require(Data.Hash(result) == AssetHash, "Loading artwork verification failed.");
        return result;
    }
    public static byte[]? Find(Dictionary<string, SourceEntry> files, string? extra, string destination)
    {
        var options = new List<SourceEntry>();
        foreach (string name in new[] { Name, "default.xbe" })
            if (files.TryGetValue(name, out var f)) options.Add(f);
        if (!string.IsNullOrWhiteSpace(extra)) options.Add(SourceEntry.FromFile(extra));
        string existing = Path.Combine(destination, "halo-source", Name);
        if (File.Exists(existing)) options.Add(SourceEntry.FromFile(existing));
        foreach (var option in options) {
            byte[]? found = Extract(option.Small());
            if (found != null) return found;
        }
        // Maps + previously extracted HUD remain usable without additional files.
        return null;
    }
}
