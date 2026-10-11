namespace HaloSetup.Cli;

// The wizard only collects and reviews paths. Conversion and installation still
// use the shared installer, so guided and scripted setup cannot drift apart.
internal static class GuidedSetup
{
    public static string[] ReadArguments(TextReader input, TextWriter output, CancellationToken ct)
    {
        string Read(string prompt)
        {
            ct.ThrowIfCancellationRequested();
            output.Write(prompt); output.Flush();
            // Console.ReadLine blocks while idle. Waiting on a background reader
            // lets Ctrl+C cancel the prompt immediately without touching any files.
            string? value = ct.CanBeCanceled
                ? Task.Run(input.ReadLine, ct).WaitAsync(ct).GetAwaiter().GetResult()
                : input.ReadLine();
            ct.ThrowIfCancellationRequested();
            if (value == null || value.Trim().Equals("q", StringComparison.OrdinalIgnoreCase))
                throw new OperationCanceledException(ct);
            return value.Trim();
        }
        string PathPrompt(string prompt, string kind, bool optional = false)
        {
            while (true)
            {
                string value = Read(prompt);
                if (optional && value.Length == 0) return "";
                try
                {
                    // Finder and Explorer can paste quoted paths. Finder may also
                    // escape spaces with a backslash; interpret paths, never shell commands.
                    bool quoted = value.Length >= 2 && value[0] is '\'' or '"' && value[^1] == value[0];
                    if (quoted) value = value[1..^1];
                    else if (!OperatingSystem.IsWindows() && !File.Exists(value) && !Directory.Exists(value))
                        value = System.Text.RegularExpressions.Regex.Replace(value, @"\\(.)", "$1");
                    if (value.StartsWith("~/", StringComparison.Ordinal) || value == "~")
                        value = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), value.Length > 2 ? value[2..] : "");
                    if (string.IsNullOrWhiteSpace(value)) throw new ArgumentException();
                    string path = Installer.NormalizeDestination(value);
                    bool valid = kind == "folder" ? Directory.Exists(path) : kind == "source"
                        ? File.Exists(path) || Directory.Exists(path) : File.Exists(path);
                    if (!valid)
                    {
                        output.WriteLine(kind == "folder"
                            ? "That folder was not found. Choose your SD card, or create an output folder in Finder/File Explorer first."
                            : "That file or folder was not found. Try dragging it from Finder/File Explorer into this window.");
                        continue;
                    }
                    if (kind == "game")
                    {
                        using var file = File.OpenRead(path);
                        byte[] header = new byte[4];
                        if (file.Read(header) != 4 || !header.AsSpan().SequenceEqual("3DSX"u8))
                        { output.WriteLine("Choose the Halo3DS.3dsx game file, rather than a ZIP archive or setup program."); continue; }
                    }
                    return path;
                }
                catch (Exception ex) when (ex is ArgumentException or IOException or UnauthorizedAccessException or NotSupportedException)
                { output.WriteLine("Could not open that path. Check it and try again."); }
            }
        }

        output.WriteLine("Halo3DS setup - step by step");
        output.WriteLine("Type Q at any prompt to cancel. You can also press Ctrl+C.");
        output.WriteLine("1. Set up game files on an SD card or output folder (first-time setup)");
        output.WriteLine("2. Update the Halo3DS game file (already set up)");
        output.WriteLine("3. Check an Xbox game dump (does not install anything)");
        string choice;
        while ((choice = Read("Choose 1, 2 or 3: ")) is not ("1" or "2" or "3"))
            output.WriteLine("Please enter 1, 2 or 3, or Q to cancel.");
        string command = choice == "1" ? "prepare" : choice == "2" ? "update" : "verify";
        output.WriteLine("Drag a file or folder into this window, or paste its full path, then press Enter.");
        var args = new List<string> { command };
        string? source = null, destination = null, game = null, compatibility = null;
        if (command != "update")
        {
            output.WriteLine("Use an English North American Halo: Combat Evolved dump for the original Xbox. PC/MCC data does not work.");
            source = PathPrompt("Xbox ISO or extracted game/maps folder: ", "source");
            args.AddRange(["--source", source]);
        }
        if (command != "verify")
        {
            output.WriteLine("Choose the SD card itself, not its 3ds or halo-source folder. On Mac, SD cards are inside /Volumes.");
            output.WriteLine("An existing empty output folder works too; you can copy the results to a FAT32 card later. Cards are never formatted.");
            destination = PathPrompt("SD card or output folder: ", "folder");
            game = PathPrompt("Halo3DS.3dsx game file (download separately from the release): ", "game");
            args.AddRange(["--destination", destination, "--game", game]);
        }
        if (command == "prepare")
        {
            output.WriteLine("With an Xbox ISO or a full extracted dump, just press Enter below.");
            output.WriteLine("If you only have maps, select the matching default.xbe from the same Xbox dump.");
            compatibility = PathPrompt("Optional default.xbe or HUD compatibility file [Enter to skip]: ", "file", optional: true);
            if (compatibility.Length != 0) args.AddRange(["--compatibility", compatibility]);
        }
        output.WriteLine(); output.WriteLine("Review your choices:");
        output.WriteLine("Action: " + (command == "prepare" ? "Prepare game files and install the launcher" : command == "update" ? "Update only the game launcher" : "Check source files without installing"));
        if (source != null) output.WriteLine("Xbox data: " + source);
        if (destination != null) output.WriteLine("Install to: " + destination);
        if (game != null) output.WriteLine("Game build: " + game);
        if (!string.IsNullOrEmpty(compatibility)) output.WriteLine("Compatibility data: " + compatibility);
        output.WriteLine("Existing correct maps and saved progress are kept.");
        while (true)
        {
            string answer = Read("Continue? [y/N]: ");
            if (answer.Equals("y", StringComparison.OrdinalIgnoreCase) || answer.Equals("yes", StringComparison.OrdinalIgnoreCase)) return args.ToArray();
            if (answer.Length == 0 || answer.Equals("n", StringComparison.OrdinalIgnoreCase) || answer.Equals("no", StringComparison.OrdinalIgnoreCase))
                throw new OperationCanceledException(ct);
            output.WriteLine("Enter Y to continue or N to cancel.");
        }
    }
}
