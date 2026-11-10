namespace HaloSetup;

/// <summary>Optional retail loading artwork, extracted locally; no pixels are bundled.</summary>
public static class Loading
{
    public const string Name = "ntsc2276-loading.bin";
    public const string AssetHash = "47838bfd25e0f4a763a78ec1da4c1ee61b6a4f56c1e7eb2d05d5ea9dc1bd59dd";
    public static byte[]? Extract(byte[] data)
    {
        string hash = Data.Hash(data);
        if (hash == AssetHash) return data;
        if (data.Length < 26604 || System.Text.Encoding.ASCII.GetString(data, 0, 4) != "XBEH") return null;
        byte[] result = new byte[26604];
        for (int offset = 0; offset <= data.Length - result.Length; offset += 4)
        {
            if (Data.U32(data, offset) != 320 || Data.U32(data, offset + 4) != 240) continue;
            Array.Copy(data, offset, result, 0, result.Length);
            if (Data.Hash(result) == AssetHash) return result;
        }
        return null;
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
