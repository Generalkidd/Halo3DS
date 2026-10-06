using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;

internal static class Program
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBoxW(nint hwnd, string text, string caption, uint type);

    [STAThread]
    private static int Main()
    {
        // Each run owns one fresh directory; concurrent copies cannot modify one another.
        string root = Path.Combine(Path.GetTempPath(), "Halo3DS-Setup");
        string run = Path.Combine(root, Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(run);
            using (Stream payload = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")
                ?? throw new InvalidDataException("The bundled application is missing."))
            using (var zip = new ZipArchive(payload, ZipArchiveMode.Read))
                zip.ExtractToDirectory(run);
            var start = new ProcessStartInfo(Path.Combine(run, "Halo3DS Setup.exe"))
            {
                WorkingDirectory = run,
                UseShellExecute = false
            };
            using var child = Process.Start(start) ?? throw new IOException("Setup could not start.");
            child.WaitForExit();
            return child.ExitCode;
        }
        catch (Exception e)
        {
            MessageBoxW(0, "Halo3DS Setup could not open. Please check that your temporary drive has free space, then try again.\n\n" + e.Message,
                "Halo3DS Setup", 0x10);
            return 1;
        }
        finally
        {
            // Only delete this invocation's directory, never the shared root or user data.
            if (Path.GetDirectoryName(Path.GetFullPath(run)) == Path.GetFullPath(root))
            {
                // Windows can briefly retain DLL handles after the app exits.
                for (int attempt = 0; attempt < 10; attempt++)
                {
                    try { if (Directory.Exists(run)) Directory.Delete(run, true); break; }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                    Thread.Sleep(500);
                }
            }
        }
    }
}
