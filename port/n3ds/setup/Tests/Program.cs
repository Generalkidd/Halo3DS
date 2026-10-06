using HaloSetup;
using System.Text.Json;

if (args.Length > 0)
{
    // Developer integration runner uses the exact core called by the WinUI app.
    string command = args[0];
    if (command == "texture-pack") { Textures.Generate(args[1],args[2]); Data.Require(Textures.Current(args[1],args[2]),"Archive verification failed"); Console.WriteLine("PASS native texture archive generated and verified"); }
    else if (command == "texture-all") {
        Directory.CreateDirectory(args[2]);
        foreach (string name in Data.Catalog.Keys) {
            string map=Path.Combine(args[1],name+".map");if(!File.Exists(map))continue;
            string packed=Path.Combine(args[2],name+".ntx");Textures.Generate(map,packed);
            Data.Require(Textures.Current(map,packed),"Archive verification failed: "+name);Console.WriteLine("PASS "+name+" "+new FileInfo(packed).Length);
        }
    }
    else if (command == "loading")
    {
        var files = Sources.Discover(args[1]);
        byte[] art = Loading.Find(files, null, args[2]) ?? throw new Exception("Artwork missing");
        Directory.CreateDirectory(args[2]); File.WriteAllBytes(Path.Combine(args[2], Loading.Name), art);
        Data.Require(Data.Hash(art) == Loading.AssetHash, "Incorrect loading artwork");
        Data.Require(Loading.Extract(art) != null, "Existing loading file rejected");
        art[100] ^= 1; Data.Require(Loading.Extract(art) == null, "Corrupt loading file accepted");
        Data.Require(Loading.Extract(new byte[5]) == null, "Truncated loading file accepted");
        Console.WriteLine("PASS loading artwork extraction, reuse, corrupt/truncated rejection");
    }
    else if (command == "inspect")
    {
        var files = Sources.Discover(args[1]);
        foreach (var (name, value) in files.Where(p => p.Key.EndsWith(".map") || p.Key == "default.xbe"))
            Console.WriteLine($"{name}: {value.Length}");
        Console.WriteLine("HUD: " + Data.Hash(Hud.Find(files, null, args[2])));
    }
    else if (command == "install")
    {
        var progress = new DirectProgress();
        var result = Installer.Run(new(args[1], args[2], args[3], args.Length > 4 ? args[4] : null), progress);
        Console.WriteLine(JsonSerializer.Serialize(result));
    }
    else if (command == "audit")
    {
        foreach (var (name, row) in Data.Catalog)
        {
            Relocation.Generate(Path.Combine(args[1], name + ".map"), args[2], name);
            foreach (var (file, expected) in row.Sidecars)
                Data.Require(Data.HashFile(Path.Combine(args[2], file)) == expected, file + " differs from working hardware data");
            Console.WriteLine(name + " PASS");
        }
    }
    else if (command == "exercise")
    {
        string testRoot = Path.GetFullPath(args[4]);
        Data.Require(!Directory.Exists(testRoot), "Use a fresh test output folder");
        string input = Path.Combine(testRoot, "maps-only"), dest = Path.Combine(testRoot, "output");
        Directory.CreateDirectory(input); Directory.CreateDirectory(dest);
        foreach (string n in new[] { "ui", "bloodgulch" }) File.Copy(Path.Combine(args[1], n + ".map"), Path.Combine(input, n + ".map"));
        try { Hud.Find(Sources.Discover(input), null, dest); throw new Exception("Missing HUD silently accepted"); }
        catch (SetupException) { Console.WriteLine("PASS maps-only missing HUD explanation"); }
        var options = new SetupOptions(input, dest, args[3], args[2]);
        using var cts = new CancellationTokenSource();
        try
        {
            Installer.Run(options, new CallbackProgress(p => { if (p.Percent > 3) cts.Cancel(); }), cts.Token);
            throw new Exception("Install cancellation ignored");
        }
        catch (OperationCanceledException) { Console.WriteLine("PASS cancellation while preparing"); }
        Data.Require(!Directory.EnumerateDirectories(Path.Combine(dest, "halo-source"), ".halo-setup-*").Any(), "Cancelled staging was not cleaned");
        var first = Installer.Run(options);
        Data.Require(first.Prepared == 2, "Expanded maps installation failed");
        Console.WriteLine("PASS expanded maps-only input with separately selected retail XBE");
        string save = Path.Combine(dest, "halo-source", "state", "keep.bin");
        Directory.CreateDirectory(Path.GetDirectoryName(save)!); File.WriteAllBytes(save, [1, 2, 3, 4]);
        string sidecar = Path.Combine(dest, "halo-source", "ui.nrl");
        File.WriteAllBytes(sidecar, [0]);
        // Existing valid HUD file now supplies compatibility without an XBE.
        var repaired = Installer.Run(options with { CompatibilityFile = null });
        Data.Require(repaired.Prepared == 1 && repaired.AlreadyCorrect == 1, "Repair did not isolate the damaged map");
        Data.Require(File.ReadAllBytes(save).SequenceEqual(new byte[] { 1, 2, 3, 4 }), "Save changed");
        Console.WriteLine("PASS targeted repair and save preservation");
        var again = Installer.Run(options);
        Data.Require(again.Prepared == 0 && again.AlreadyCorrect == 2, "Correct files were reprocessed");
        Console.WriteLine("PASS repeat-run skip");
        Installer.Run(options with { Source = "", LauncherOnly = true });
        Data.Require(File.ReadAllBytes(save).SequenceEqual(new byte[] { 1, 2, 3, 4 }), "Update altered saves");
        Console.WriteLine("PASS launcher-only update");
        // Reject a corrupt source before publishing that map.
        string corrupt = Path.Combine(input, "bloodgulch.map");
        using (var f = new FileStream(corrupt, FileMode.Open)) { f.Position = 4096; f.WriteByte(0xff); }
        File.Delete(Path.Combine(dest, "halo-source", "bloodgulch.nrl"));
        try { Installer.Run(options); throw new Exception("Corrupt source accepted"); }
        catch (SetupException) { Console.WriteLine("PASS corrupt source rejection"); }
        Data.Require(Data.HashFile(Path.Combine(dest, "halo-source", "bloodgulch.map")) == Data.Catalog["bloodgulch"].Sha256, "Existing good map overwritten by corrupt input");
        Console.WriteLine("PASS good destination map preserved after input failure");
    }
    else throw new Exception("Unknown command");
    return;
}

int passed = 0;
void ExpectFailure(string name, Action action)
{
    try { action(); throw new Exception(name + " unexpectedly succeeded"); }
    catch (SetupException) { passed++; Console.WriteLine("PASS " + name); }
}
string root = Path.Combine(Path.GetTempPath(), "halo-setup-tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
try
{
    ExpectFailure("path traversal", () => Installer.SafePath(root, "../escape"));
    string legacyLauncher = Path.Combine(root, "3ds", "HaloSourceEngine", "HaloSourceEngine.3dsx");
    Directory.CreateDirectory(Path.GetDirectoryName(legacyLauncher)!);
    File.WriteAllBytes(legacyLauncher, [1, 2, 3, 4]);
    string unrelated = Path.Combine(Path.GetDirectoryName(legacyLauncher)!, "keep.txt");
    File.WriteAllText(unrelated, "keep");
    Installer.RetireLegacyLauncher(root);
    var backups = Directory.GetFiles(Path.Combine(root, "halo-source", "launcher-backups"), "*.3dsx", SearchOption.AllDirectories);
    Data.Require(!File.Exists(legacyLauncher) && backups.Length == 1 &&
        File.ReadAllBytes(backups[0]).SequenceEqual(new byte[] { 1, 2, 3, 4 }) &&
        File.ReadAllText(unrelated) == "keep", "Legacy launcher migration failed");
    Installer.RetireLegacyLauncher(root);
    passed++; Console.WriteLine("PASS legacy launcher backup, unrelated files preserved, repeat migration");
    ExpectFailure("sibling prefix escape", () => Installer.SafePath(root, "../" + Path.GetFileName(root) + "-other/file"));
    ExpectFailure("absolute destination child", () => Installer.SafePath(root, Path.Combine(root, "absolute")));
    ExpectFailure("empty destination", () => Installer.NormalizeDestination(" "));
    foreach (string folder in new[] { root, root + Path.DirectorySeparatorChar })
        Data.Require(Installer.SafePath(folder, "halo-source/ui.map") == Path.Combine(root, "halo-source", "ui.map"), "Output folder path failed");
    passed++; Console.WriteLine("PASS output folder with and without trailing separator");
    if (OperatingSystem.IsWindows())
    {
        foreach (string drive in new[] { "H:", "D:", "Z:" })
        {
            Data.Require(Installer.NormalizeDestination(" " + drive + " ") == drive + "\\", "Bare drive normalization failed");
            foreach (string form in new[] { drive, drive + "\\", drive + "/" })
                Data.Require(Installer.SafePath(form, "3ds/Halo3DS/Halo3DS.3dsx") ==
                    drive + "\\3ds\\Halo3DS\\Halo3DS.3dsx", "Drive-root destination rejected");
        }
        passed++; Console.WriteLine("PASS H:, D:, Z: roots and both slash forms (read-only)");
    }
    ExpectFailure("bad HUD file", () => Hud.Extract(new byte[6928]));
    string bad = Path.Combine(root, "bad.iso"); File.WriteAllBytes(bad, new byte[2048]);
    ExpectFailure("invalid ISO", () => new Xiso(bad));
    ExpectFailure("empty maps folder", () => Sources.Discover(root));
    string ui = Path.Combine(root, "ui.map"); File.WriteAllBytes(ui, new byte[2048]);
    ExpectFailure("invalid map", () => Installer.CheckMap(SourceEntry.FromFile(ui), "ui", Data.Catalog["ui"]));
    byte[] header = new byte[2048]; "daeh"u8.CopyTo(header); "toof"u8.CopyTo(header.AsSpan(2044)); header[4] = 5;
    "ui"u8.CopyTo(header.AsSpan(32)); "01.01.14.2342"u8.CopyTo(header.AsSpan(64)); File.WriteAllBytes(ui, header);
    ExpectFailure("unsupported map build", () => Installer.CheckMap(SourceEntry.FromFile(ui), "ui", Data.Catalog["ui"]));
    // Minimal synthetic XDVDFS entry. Also exercises malformed links and filename traversal.
    byte[] iso = new byte[0x14000]; "MICROSOFT*XBOX*MEDIA"u8.CopyTo(iso.AsSpan(0x10000));
    "MICROSOFT*XBOX*MEDIA"u8.CopyTo(iso.AsSpan(0x107ec));
    BitConverter.GetBytes(34u).CopyTo(iso, 0x10014); BitConverter.GetBytes(2048u).CopyTo(iso, 0x10018);
    BitConverter.GetBytes(36u).CopyTo(iso, 0x11004); BitConverter.GetBytes(4u).CopyTo(iso, 0x11008);
    iso[0x1100d] = 6; "ui.map"u8.CopyTo(iso.AsSpan(0x1100e)); "test"u8.CopyTo(iso.AsSpan(0x12000));
    File.WriteAllBytes(bad, iso);
    Data.Require(new Xiso(bad).Files["ui.map"].Small().SequenceEqual("test"u8.ToArray()), "XISO entry read failed");
    passed++; Console.WriteLine("PASS synthetic XISO");
    iso[0x11000] = 1; File.WriteAllBytes(bad, iso);
    ExpectFailure("bad directory child", () => new Xiso(bad));
    iso[0x11000] = 0; "../bad"u8.CopyTo(iso.AsSpan(0x1100e)); File.WriteAllBytes(bad, iso);
    ExpectFailure("unsafe image filename", () => new Xiso(bad));
    using var cancellation = new CancellationTokenSource(); cancellation.Cancel();
    try { Data.HashFile(ui, cancellation.Token); throw new Exception("cancel ignored"); }
    catch (OperationCanceledException) { passed++; Console.WriteLine("PASS cancellation"); }
    Console.WriteLine($"{passed} checks passed");
}
finally { Directory.Delete(root, true); }

sealed class DirectProgress : IProgress<SetupProgress>
{
    string last = "";
    public void Report(SetupProgress value)
    { if (value.Message != last) { last = value.Message; Console.WriteLine($"{value.Percent:F0}% {last}"); } }
}

sealed class CallbackProgress(Action<SetupProgress> callback) : IProgress<SetupProgress>
{ public void Report(SetupProgress value) => callback(value); }
