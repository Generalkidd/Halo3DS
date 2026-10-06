namespace HaloSetup;

public static class Hud
{
    public const string Name = "ntsc2276-hud-strings.bin";
    const string HudHash = "4d272fcae6d5fbc90691666585f1a7c171fcc5918713ca8e97de6c711a2d4bce";
    const string XbeHash = "ed3a8e962351ad6c4b3b620768fb6a0bda658963390252439ea036d5ede3a3ac";
    static readonly int[] offsets = [0x229d78,0x229d90,0x229da0,0x229de0,0x229e18,0x229e40,0x229e6c,
        0x229e7c,0x229ea8,0x229ec8,0x229ee4,0x229f00,0x229f1c,0x229f38,0x229f70,0x229f8c,0x229fb0,
        0x229fd4,0x229ff8,0x22a020,0x22a064,0x22a08c,0x22a0bc,0x22a0e8,0x22a104,0x22a13c,0x22a170];
    public static byte[] Extract(byte[] data)
    {
        string hash = Data.Hash(data);
        if (hash == HudHash) return data;
        Data.Require(hash == XbeHash, "This Xbox executable does not match the supported release. Choose default.xbe from your English North American retail copy of Halo: Combat Evolved for the original Xbox, or an existing ntsc2276-hud-strings.bin. No prototype or debug executable is needed.");
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        foreach (uint value in new uint[] { 0x5453484e, 2276, 74, 27 }) writer.Write(value);
        foreach (int offset in offsets)
        {
            int end = offset;
            while (end < offset + 254 && (data[end] != 0 || data[end + 1] != 0)) end += 2;
            Data.Require(end < offset + 254, "The Xbox executable has invalid HUD text.");
            byte[] slot = new byte[256]; data.AsSpan(offset, end + 2 - offset).CopyTo(slot); writer.Write(slot);
        }
        byte[] result = stream.ToArray();
        Data.Require(Data.Hash(result) == HudHash, "HUD compatibility verification failed.");
        return result;
    }
    public static byte[] Find(Dictionary<string, SourceEntry> files, string? extra, string destination)
    {
        var options = new List<SourceEntry>();
        if (!string.IsNullOrWhiteSpace(extra)) options.Add(SourceEntry.FromFile(extra));
        foreach (string name in new[] { Name, "default.xbe" }) if (files.TryGetValue(name, out var f)) options.Add(f);
        string existing = Path.Combine(destination, "halo-source", Name);
        if (File.Exists(existing)) options.Add(SourceEntry.FromFile(existing));
        SetupException? last = null;
        foreach (var option in options)
            try { return Extract(option.Small()); } catch (SetupException error) { last = error; }
        if (last != null) throw last;
        throw new SetupException("Your maps were found. This build also needs a small HUD text file. Under Additional options, select the matching default.xbe or an existing ntsc2276-hud-strings.bin. An XISO or full game folder usually provides this automatically. No debug executable or source code is needed.");
    }
}
