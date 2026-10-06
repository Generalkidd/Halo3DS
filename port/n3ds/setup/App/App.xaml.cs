using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace HaloSetup;

public partial class App : Application
{
    Window? window;
    public App() { InitializeComponent(); }
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    { window = new SetupWindow(); window.Activate(); }
}

public sealed class SetupWindow : Window
{
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern uint GetDpiForWindow(nint hwnd);
    readonly TextBox source = new() { Header = "1. Your Halo game files", PlaceholderText = "Choose an Xbox XISO or dumped maps folder" };
    readonly TextBox destination = new() { Header = "2. SD card or output folder", PlaceholderText = "Choose the root of your SD card" };
    readonly TextBox build = new() { Header = "Game build (.3dsx)" };
    readonly TextBox compatibility = new() { Header = "HUD compatibility file (only if requested)", PlaceholderText = "Automatically found in most full game dumps" };
    readonly CheckBox updateOnly = new() { Content = "Update the launcher only (keep installed maps)" };
    readonly ProgressBar progress = new() { Minimum = 0, Maximum = 100, Value = 0 };
    readonly TextBlock status = new() { Text = "Choose your game files and destination to begin.", TextWrapping = TextWrapping.Wrap };
    readonly InfoBar message = new() { IsOpen = false, IsClosable = true };
    readonly Button start = new() { Content = "Prepare SD Card", Style = (Style)Application.Current.Resources["AccentButtonStyle"] };
    readonly Button cancel = new() { Content = "Cancel", IsEnabled = false };
    readonly List<Control> inputs = [];
    readonly StackPanel layout = new() { Spacing = 18, Padding = new Thickness(30), MaxWidth = 850, HorizontalAlignment = HorizontalAlignment.Stretch };
    CancellationTokenSource? cancellation;
    bool busy;

    static TextBlock Text(string value, double size = 14) => new() { Text = value, FontSize = size, TextWrapping = TextWrapping.Wrap };
    Button Button(string label, Action action)
    {
        var button = new Button { Content = label };
        button.Click += (_, _) => action(); inputs.Add(button); return button;
    }
    static StackPanel Row(params UIElement[] controls)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        foreach (var control in controls) row.Children.Add(control); return row;
    }
    public SetupWindow()
    {
        Title = "Halo 3DS Setup";
        inputs.AddRange([source, destination, build, compatibility]);
        double scale = GetDpiForWindow(WindowNative.GetWindowHandle(this)) / 96.0;
        var area = Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(AppWindow.Id,
            Microsoft.UI.Windowing.DisplayAreaFallback.Primary).WorkArea;
        AppWindow.Resize(new Windows.Graphics.SizeInt32(
            Math.Min((int)Math.Round(840 * scale), area.Width),
            Math.Min((int)Math.Round(850 * scale), area.Height)));
        layout.Children.Add(Text("Halo 3DS Setup", 32));
        layout.Children.Add(Text("Bring your copy of Halo to your Nintendo 3DS or 2DS.", 16));
        var card = new StackPanel { Spacing = 10 };
        card.Children.Add(source);
        card.Children.Add(Row(Button("Choose XISO…", async () => await PickFile(source, ".iso", ".xiso")),
            Button("Choose maps folder…", async () => await PickFolder(source))));
        card.Children.Add(Text("Requires Halo: Combat Evolved for the original Xbox — English North American retail release. No prototype or debug version needed. PC/MCC maps are not supported."));
        layout.Children.Add(card);
        var target = new StackPanel { Spacing = 10 };
        target.Children.Add(destination); target.Children.Add(Button("Choose destination…", async () => await PickFolder(destination)));
        target.Children.Add(Text("Installs the game and checks its files. Existing saves and other apps are kept. No formatting."));
        layout.Children.Add(target);
        var extra = new StackPanel { Spacing = 10 };
        build.Text = Path.Combine(AppContext.BaseDirectory, "Halo3DS.3dsx");
        extra.Children.Add(build); extra.Children.Add(Button("Choose game build…", async () => await PickFile(build, ".3dsx")));
        extra.Children.Add(compatibility); extra.Children.Add(Row(Button("Choose compatibility file…", async () => await PickFile(compatibility, ".xbe", ".bin")), Button("Clear", () => compatibility.Text = "")));
        extra.Children.Add(updateOnly); inputs.Add(updateOnly);
        var expander = new Expander { Header = "Additional options", Content = extra, HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch };
        layout.Children.Add(expander);
        layout.Children.Add(progress); layout.Children.Add(status); layout.Children.Add(message);
        layout.Children.Add(Row(start, cancel));
        var openLog = new HyperlinkButton { Content = "Open setup log folder" };
        openLog.Click += (_, _) => OpenLogs(); layout.Children.Add(openLog);
        layout.Children.Add(Text("Your files stay on this computer. Setup works offline. Windows 10/11 · Nintendo 3DS / 2DS family, including original models"));
        Content = new ScrollViewer { Content = layout, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Background = (Brush)Application.Current.Resources["ApplicationPageBackgroundThemeBrush"] };
        start.Click += async (_, _) => await Run();
        cancel.Click += (_, _) => { cancellation?.Cancel(); cancel.IsEnabled = false; status.Text = "Stopping safely…"; };
        AppWindow.Closing += (_, args) =>
        {
            if (!busy) return;
            args.Cancel = true; cancellation?.Cancel(); cancel.IsEnabled = false;
            status.Text = "Stopping safely. Close this window when setup has stopped.";
        };
    }
    async Task PickFile(TextBox target, params string[] types)
    {
        try
        {
            var picker = new FileOpenPicker(); InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
            foreach (string type in types) picker.FileTypeFilter.Add(type);
            var file = await picker.PickSingleFileAsync(); if (file != null) target.Text = file.Path;
        }
        catch (Exception e) { ShowError(e.Message); }
    }
    async Task PickFolder(TextBox target)
    {
        try
        {
            var picker = new FolderPicker(); InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
            picker.FileTypeFilter.Add("*"); var folder = await picker.PickSingleFolderAsync();
            if (folder != null) target.Text = folder.Path;
        }
        catch (Exception e) { ShowError(e.Message); }
    }
    void ShowError(string error)
    { message.Title = "Setup needs your attention"; message.Message = error; message.Severity = InfoBarSeverity.Error; message.IsOpen = true; }
    static string LogFolder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Halo3DS Setup", "Logs");
    static void OpenLogs()
    {
        Directory.CreateDirectory(LogFolder);
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(LogFolder) { UseShellExecute = true });
    }
    async Task Run()
    {
        if (busy) return;
        if (string.IsNullOrEmpty(destination.Text) || (updateOnly.IsChecked != true && string.IsNullOrEmpty(source.Text)))
        { ShowError("Choose your game files and destination first."); return; }
        var options = new SetupOptions(source.Text, destination.Text, build.Text, compatibility.Text, updateOnly.IsChecked == true);
        busy = true; start.IsEnabled = false; cancel.IsEnabled = true; message.IsOpen = false;
        foreach (var control in inputs) control.IsEnabled = false;
        cancellation = new(); progress.Value = 0;
        var reports = new Progress<SetupProgress>(p => { status.Text = p.Message; progress.Value = p.Percent; });
        try
        {
            Directory.CreateDirectory(LogFolder);
            string log = Path.Combine(LogFolder, DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".txt");
            try
            {
                File.WriteAllText(log, $"Halo 3DS Setup\nStarted: {DateTime.Now:O}\n");
                var result = await Task.Run(() => Installer.Run(options, reports, cancellation.Token));
                File.AppendAllText(log, System.Text.Json.JsonSerializer.Serialize(result) + "\nPASS\n");
                message.Title = "Your files are ready";
                message.Severity = result.DspPresent ? InfoBarSeverity.Success : InfoBarSeverity.Warning;
                string done = options.LauncherOnly ? "Launcher updated. " : $"{result.Maps} maps ready ({result.AlreadyCorrect} already correct). ";
                message.Message = done + "Safely eject the SD card, insert it in your Nintendo 3DS or 2DS, then open Halo3DS in the Homebrew Launcher." +
                    (result.DspPresent ? "" : "\n\nAudio needs one console step: open Rosalina with L + D-pad Down + SELECT → Miscellaneous options → Dump DSP firmware. Setup does not include Nintendo firmware.");
                message.IsOpen = true;
            }
            catch (Exception error)
            {
                File.AppendAllText(log, error + "\n"); throw;
            }
        }
        catch (OperationCanceledException)
        { status.Text = "Stopped. Completed maps are retained. Run setup again to finish before launching the game."; }
        catch (Exception e)
        {
            status.Text = "Setup stopped. Reconnect the card if needed, then run setup again before launching the game.";
            ShowError(e is SetupException ? e.Message : "Setup couldn't finish: " + e.Message + "\nThe setup log has more details.");
        }
        finally
        {
            busy = false; start.IsEnabled = true; cancel.IsEnabled = false;
            foreach (var control in inputs) control.IsEnabled = true;
            cancellation.Dispose(); cancellation = null;
        }
    }
}
