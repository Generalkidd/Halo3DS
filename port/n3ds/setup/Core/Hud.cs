namespace HaloSetup;

public static class Hud
{
    public const string Name = "ntsc2276-hud-strings.bin";
    const string HudHash = "4d272fcae6d5fbc90691666585f1a7c171fcc5918713ca8e97de6c711a2d4bce";
    static readonly string[] fingerprints = [
        "52ad73009cf140ae8713a52ca24a8d2285280068b6930741795b779470d953bf",
        "367459a9f26345ffd3ed24f4ebaff04292b3affbf328ad22b53b855578e0075b",
        "3b70c862e90a7c149e5f69160983a11eff5b112009d8d5828d6dda178c58a055",
        "32201c97b8f91bc85201976756ead243b385aa0e4b0d23c3fe0f25b54e95300b",
        "0458395afa347a9cde32b162d36f1e853e87c14243adfafffa92d7a0a8612455",
        "042c926935fe87cfedcaa4ef1e34f89b773f813fb8b9761a99148a3d1b76fd63",
        "3024f02e016ba267d548242276555b9ef18b02a3af86be58ba641dffdd564987",
        "a8cbdee44a112f9deb421b82f2b95c52d6bab637d9d5812ddd2c5d5ded05a61f",
        "c54ccc951181c8d715b554610db113cecf835d48feddad6adf4bcc187d4bd25a",
        "52dcd48048b94281f6a3467c9f44ecb236f1925b3bdb0e52892316810c13f1aa",
        "aaaab3896ef544424518882e5092fd5f1978e9d55c70da9be7a8336d4112cf2f",
        "1780bb28d8b6be8f61ae5898443dbc762ca282124c28119571097c8af1ae9ad9",
        "882c30e6a384fe0df037de94b986ed715db10fc2ef16e03dbe6f59749dc5c0c1",
        "cb0c4b2d0bf23a49aa556cf6fd03c65b81cc29dfe4a02d484195db693b4ebb5d",
        "c62022f8ad8bf831f480c1387a555eaf4c5620ebee0ca462d4a15d7c4255c13e",
        "362592755ac0b497210e440be2b5750d20d0f098595d09f947c002e716de39dc",
        "edc78dca2bc21df5f44fc80ed84e5c2774220d7dfa69e7de81e565e9743c501d",
        "b433b385c49c424fd47879cacd19c5d61eb4d9f5007746084e0fe3d8fa8bd83a",
        "c1af1f1474f46b3a6eb9bac0460d2c4622c3ab249ef70349d1b5e99af2857df8",
        "d64b38a831adebd4d90df70b90a1ac852fdf1be21566b41c7344eba27c2805c3",
        "c1261259fe2e7668bb061da2267361d5780fe9a517372da2f399650871215596",
        "160297f18553212e29d1b8d17208c5da81cc8a7a5e2bc43eb4c443e71e710bcc",
        "c675435a0feef1e195693d9ac0dfc9f5c72f3c437407a90ac8f41db4b51e14ae",
        "c1c40c73865d78857a109906d7a54da084d170387224e57a12a565e08c6820d7",
        "1a9ef204518ad131e72cd38427b92fc6e1fd1dbd9175c89f8155413b8650a116",
        "29b40e6cc0d5c6fc29c885331bc94efe2787482ac392c92cd005dfee62590ca7",
        "a17df672f760244bdb179af5cdd6ae68e9127d6d79d7622db93b2424ebb911de"
    ];
    public static byte[] Extract(byte[] data)
    {
        string hash = Data.Hash(data);
        if (hash == HudHash) return data;
        Data.Require(data.Length >= 0x178 && System.Text.Encoding.ASCII.GetString(data, 0, 4) == "XBEH",
            "Choose default.xbe from your Xbox game dump, or an existing HUD text file.");
        var slots = new byte[27][];
        var lookup = fingerprints.Select((value, index) => new { value, index }).ToDictionary(p => p.value, p => p.index);
        for (int offset = 0; offset < data.Length - 2; offset += 2)
        {
            if (data[offset] < 32 || data[offset + 1] != 0 || (offset > 1 && (data[offset - 2] != 0 || data[offset - 1] != 0))) continue;
            int end = offset;
            while (end + 1 < data.Length && end - offset < 254 && (data[end] != 0 || data[end + 1] != 0)) end += 2;
            if (end + 1 >= data.Length || end - offset >= 254) continue;
            byte[] text = new byte[end + 2 - offset]; Array.Copy(data, offset, text, 0, text.Length);
            if (lookup.TryGetValue(Data.Hash(text), out int index))
            { slots[index] = new byte[256]; Array.Copy(text, slots[index], text.Length); }
        }
        Data.Require(slots.All(s => s != null), "This executable's HUD text could not be located. Choose an existing ntsc2276-hud-strings.bin under Additional options. The disc revision itself is not restricted.");
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        foreach (uint value in new uint[] { 0x5453484e, 2276, 74, 27 }) writer.Write(value);
        foreach (byte[] slot in slots) writer.Write(slot);
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
