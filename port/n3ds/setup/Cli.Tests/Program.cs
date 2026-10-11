using HaloSetup;
using HaloSetup.Cli;

// Asset-free integration checks call the real CLI and installer. Synthetic
// launchers exercise installation mechanics; they are not playable game builds.
string root = Path.Combine(Path.GetTempPath(), "halo-cli-tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
int passed = 0;
void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
(string Output, string Error) Run(int expected, params string[] arguments)
{
    using var output = new StringWriter(); using var error = new StringWriter();
    int code = CommandLine.Run(arguments, output, error);
    Check(code == expected, $"Expected {expected}, got {code}: {error}");
    return (output.ToString(), error.ToString());
}
void Pass(string name) { passed++; Console.WriteLine("PASS " + name); }
try
{
    Check(Run(0, "--help").Output.Contains("prepare"), "Help missing");
    Run(0); Run(0, "prepare", "--help");
    Run(2, "unknown"); Run(2, "prepare"); Run(2, "verify", "--source");
    Run(2, "verify", "--source", "--quiet");
    Run(2, "verify", "--source", root, "--source", root);
    Run(2, "verify", "--source", root, "--game", "unexpected");
    Run(2, "update", "--source", root);
    Run(2, "verify", "--source", root, "--quiet", "--quiet");
    Check(!Directory.EnumerateFileSystemEntries(root).Any(), "Usage errors created files");
    Pass("help and invalid arguments without writes");

    string dest = Path.Combine(root, "output with spaces"); Directory.CreateDirectory(dest);
    string exe = Path.Combine(root, "test build.3dsx");
    byte[] launcher = new byte[64]; "3DSX"u8.CopyTo(launcher); launcher[32] = 42; File.WriteAllBytes(exe, launcher);
    Run(1, "update", "--destination", dest, "--game", exe);
    Check(!Directory.EnumerateFileSystemEntries(dest).Any(), "Incomplete installation changed");
    Pass("update rejects unprepared destination");

    string game = Path.Combine(dest, "halo-source"); Directory.CreateDirectory(game);
    File.WriteAllText(Path.Combine(game, "ui.map"), "existing map");
    File.WriteAllText(Path.Combine(game, Hud.Name), "existing HUD");
    string saves = Path.Combine(game, "state"); Directory.CreateDirectory(saves);
    File.WriteAllText(Path.Combine(saves, "keep.bin"), "saved progress");
    File.WriteAllText(Path.Combine(dest, "unrelated.txt"), "unrelated");
    File.WriteAllText(Path.Combine(game, "setup-manifest.json"), "existing manifest");
    var result = Run(0, "update", "--destination", dest, "--game", exe);
    Check(result.Output.Contains("Launcher updated") && result.Output.Contains("DSP"), "Missing result or DSP instructions");
    Check(result.Error.Contains("100.0%"), "Missing progress");
    string installed = Path.Combine(dest, "3ds", "Halo3DS", "Halo3DS.3dsx");
    Check(File.ReadAllBytes(installed).SequenceEqual(launcher), "Wrong launcher installed");
    Check(File.ReadAllText(Path.Combine(saves, "keep.bin")) == "saved progress", "Save changed");
    Check(File.ReadAllText(Path.Combine(game, "ui.map")) == "existing map", "Map changed");
    Check(File.ReadAllText(Path.Combine(game, "setup-manifest.json")) == "existing manifest", "Manifest changed");
    Check(File.ReadAllText(Path.Combine(dest, "unrelated.txt")) == "unrelated", "Unrelated file changed");
    Pass("launcher update with spaces, progress, DSP notice and preservation");

    File.WriteAllBytes(Path.Combine(dest, "3ds", "dspfirm.cdc"), [1]);
    result = Run(0, "update", "--destination", dest, "--game", exe, "--quiet");
    Check(result.Error == "" && !result.Output.Contains("Sound needs"), "Quiet or DSP detection failed");
    Check(!Directory.EnumerateFileSystemEntries(game, ".halo-setup-*").Any(), "Staging leaked");
    Pass("repeat update and quiet output");

    File.WriteAllBytes(exe, new byte[64]);
    Run(1, "update", "--destination", dest, "--game", exe);
    Check(File.ReadAllBytes(installed).SequenceEqual(launcher), "Invalid launcher replaced good build");
    File.WriteAllBytes(exe, launcher);
    Pass("bad executable preserves installed launcher");

    using (var cts = new CancellationTokenSource())
    {
        cts.Cancel(); using var output = new StringWriter(); using var error = new StringWriter();
        Check(CommandLine.Run(["update", "--destination", dest, "--game", exe], output, error, cts.Token) == 130,
            "Cancellation exit code incorrect");
        Check(output.ToString() == "" && error.ToString().Contains("Cancelled"), "Cancellation messaging incorrect");
        Check(File.ReadAllBytes(installed).SequenceEqual(launcher), "Cancelled operation changed launcher");
    }
    Pass("cancelled invocation returns 130 without changing launcher");

    string iso = Path.Combine(root, "pc.iso");
    byte[] pc = new byte[0x11000]; "CD001"u8.CopyTo(pc.AsSpan(0x8001)); File.WriteAllBytes(iso, pc);
    result = Run(1, "verify", "--source", iso);
    Check(result.Error.Contains("PC/MCC"), "Unsupported disc explanation missing");
    string empty = Path.Combine(root, "empty"); Directory.CreateDirectory(empty);
    Run(1, "prepare", "--source", iso, "--destination", empty, "--game", exe);
    Check(!Directory.EnumerateFileSystemEntries(empty).Any(), "Invalid source wrote destination files");
    Run(1, "verify", "--source", Path.Combine(root, "missing.iso"));
    Pass("PC image and missing input rejection without writes");

    // Sparse synthetic maps pass header checks but fail the real catalog hash.
    string maps = Path.Combine(root, "maps"); Directory.CreateDirectory(maps);
    foreach (string name in new[] { "ui", "bloodgulch" })
    {
        byte[] header = new byte[2048]; "daeh"u8.CopyTo(header); "toof"u8.CopyTo(header.AsSpan(2044));
        BitConverter.GetBytes(5u).CopyTo(header, 4);
        BitConverter.GetBytes((uint)Data.Catalog[name].Bytes).CopyTo(header, 8);
        System.Text.Encoding.ASCII.GetBytes(name).CopyTo(header, 32);
        System.Text.Encoding.ASCII.GetBytes(Data.Build).CopyTo(header, 64);
        using var f = File.Create(Path.Combine(maps, name + ".map"));
        f.Write(header); f.SetLength(Data.Catalog[name].SourceBytes);
    }
    result = Run(1, "verify", "--source", maps, "--quiet");
    Check(result.Error.Contains("integrity check"), "Verify accepted or did not hash invalid source maps");
    Check(Directory.GetFileSystemEntries(maps).Length == 2, "Verify wrote extra files");
    Pass("verify checks hashes after valid headers without writing files");

    using (var cts = new CancellationTokenSource())
    {
        using var output = new StringWriter();
        using var error = new CancelOnProgress(cts);
        Check(CommandLine.Run(["verify", "--source", maps], output, error, cts.Token) == 130,
            "In-flight source verification did not cancel");
        Check(error.ToString().Contains("Verifying") && error.ToString().Contains("Cancelled"), "No in-flight cancellation report");
        Check(output.ToString() == "", "Cancelled verification reported success");
    }
    Pass("cancellation during source hashing");

    if (!OperatingSystem.IsWindows())
    {
        string linked = Path.Combine(root, "linked output"); Directory.CreateDirectory(linked);
        Directory.CreateSymbolicLink(Path.Combine(linked, "halo-source"), game);
        Run(1, "update", "--destination", linked, "--game", exe);
        Check(!Directory.Exists(Path.Combine(linked, "3ds")), "Symlink rejection wrote launcher");
        Pass("redirected destination child rejected");
    }
    (string Output, string Error) Guided(int expected, string answers, bool explicitCommand = true)
    {
        using var input = new StringReader(answers);
        using var output = new StringWriter(); using var error = new StringWriter();
        int code = CommandLine.Run(explicitCommand ? ["guided"] : [], output, error, input: input);
        Check(code == expected, $"Guided setup expected {expected}, got {code}: {error}");
        return (output.ToString(), error.ToString());
    }
    string original = Convert.ToHexString(File.ReadAllBytes(installed));
    Guided(130, "q\n"); Guided(130, "");
    Guided(130, $"2\n{dest}\n{exe}\nn\n");
    Guided(130, $"2\n{dest}\n{exe}\n\n");
    Check(Convert.ToHexString(File.ReadAllBytes(installed)) == original, "Cancelled guided setup changed launcher");
    Run(2, "guided");
    Pass("guided cancellation, end of input, safe default and missing terminal");

    string quotedDest = "\"" + dest + "\"";
    string quotedExe = "'" + exe + "'";
    result = Guided(0, $"invalid\n2\n{root}/missing\n{quotedDest}\n{iso}\n{quotedExe}\nmaybe\ny\n", false);
    Check(result.Output.Contains("Review your choices") && result.Output.Contains("Install to: " + dest)
        && result.Output.Contains("not found") && result.Output.Contains("rather than a ZIP")
        && result.Output.Contains("Enter Y") && result.Output.Contains("Launcher updated"), "Missing guided validation or summary");
    Check(File.ReadAllText(Path.Combine(saves, "keep.bin")) == "saved progress", "Guided update changed save");
    Check(File.ReadAllBytes(installed).SequenceEqual(launcher), "Guided update installed wrong launcher");
    Pass("guided no-argument update, retries, quoted paths, review and save preservation");

    if (!OperatingSystem.IsWindows())
    {
        Guided(0, $"2\n{dest.Replace(" ", "\\ ")}\n{exe.Replace(" ", "\\ ")}\nyes\n");
        string special = Path.Combine(root, "Finder $tag (copy) & #1"); Directory.CreateDirectory(special);
        string specialExe = Path.Combine(special, "Halo3DS.3dsx"); File.WriteAllBytes(specialExe, launcher);
        string dragged = string.Concat(specialExe.Select(c => char.IsWhiteSpace(c) || "$()&#".Contains(c) ? "\\" + c : c.ToString()));
        Guided(0, $"2\n{dest}\n{dragged}\ny\n");
        Pass("Finder-style escaped spaces and punctuation without shell evaluation");
    }
    result = Guided(1, $"3\n{iso}\ny\n");
    Check(result.Error.Contains("PC/MCC"), "Guided verification lost source explanation");
    string guidedDest = Path.Combine(root, "guided empty"); Directory.CreateDirectory(guidedDest);
    Guided(130, $"1\n{iso}\n{guidedDest}\n{exe}\n\nn\n");
    Check(!Directory.EnumerateFileSystemEntries(guidedDest).Any(), "Unconfirmed preparation wrote files");
    Pass("guided source verification and prepare review without writes");
    Console.WriteLine($"{passed} CLI checks passed");
}
finally { Directory.Delete(root, true); }

sealed class CancelOnProgress(CancellationTokenSource cancellation) : StringWriter
{
    public override void WriteLine(string? value)
    {
        base.WriteLine(value);
        if (value?.Contains("Verifying") == true) cancellation.Cancel();
    }
}
