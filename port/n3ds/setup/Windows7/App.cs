using System;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
using HaloSetup;

namespace HaloSetup.Windows7
{
    internal static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                using (var form = new SetupForm())
                {
                    // Developer-only deterministic layout capture; no card writes.
                    if (args.Length == 2 && args[0] == "--capture")
                    {
                        form.Show(); Application.DoEvents();
                        using (var bitmap = new Bitmap(form.Width, form.Height))
                        { form.DrawToBitmap(bitmap, new Rectangle(0, 0, bitmap.Width, bitmap.Height)); bitmap.Save(args[1]); }
                        form.Close(); return;
                    }
                    if(args.Length==4 && args[0]=="--smoke")form.ConfigureSmoke(args[1],args[2],args[3]);
                    Application.Run(form);
                }
            }
            catch (Exception e) { MessageBox.Show(e.Message, "Halo3DS Setup", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }
    }

    internal sealed class SetupForm : Form, IProgress<SetupProgress>
    {
        readonly TextBox source = new TextBox(), destination = new TextBox(), extra = new TextBox(), build = new TextBox();
        readonly CheckBox updateOnly = new CheckBox();
        readonly Label status = new Label();
        readonly ProgressBar progress = new ProgressBar();
        readonly Button prepare = new Button(), cancel = new Button();
        readonly TableLayoutPanel inputs = new TableLayoutPanel();
        readonly BackgroundWorker worker = new BackgroundWorker();
        CancellationTokenSource cancellation;
        StreamWriter log;
        string logPath, temporaryBuild;
        string smokeReport;
        readonly string logDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Halo3DS Setup\\Logs");

        public SetupForm()
        {
            Text = "Halo3DS Setup - Windows 7";
            Font = new Font("Segoe UI", 9F);
            // CLR 2 WinForms does not consistently scale the outer window when
            // started on a modern high-DPI desktop. Size it in device pixels;
            // fonts remain native point-sized text, never bitmap-scaled.
            AutoScaleMode = AutoScaleMode.None;
            float scale; using(var graphics=CreateGraphics())scale=graphics.DpiX/96F;
            Rectangle available=Screen.FromControl(this).WorkingArea;
            ClientSize = new Size(Math.Min((int)(750*scale),available.Width-50), Math.Min((int)(620*scale),available.Height-80));
            MinimumSize = new Size(Math.Min((int)(650*scale),available.Width-50),Math.Min((int)(450*scale),available.Height-80));
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.White;
            var frame=new TableLayoutPanel { Dock=DockStyle.Fill,ColumnCount=1,RowCount=2,Padding=new Padding(18) };
            frame.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
            frame.RowStyles.Add(new RowStyle(SizeType.Percent,100));frame.RowStyles.Add(new RowStyle(SizeType.AutoSize));Controls.Add(frame);
            var scroll=new Panel { Dock=DockStyle.Fill,AutoScroll=true };frame.Controls.Add(scroll,0,0);
            var page = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize=true, Padding = new Padding(4), ColumnCount = 1, RowCount = 5 };
            page.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            scroll.Controls.Add(page);
            page.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            page.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            page.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            page.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            page.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            var footer=new TableLayoutPanel { Dock=DockStyle.Bottom,AutoSize=true,ColumnCount=1,RowCount=3 };
            footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));frame.Controls.Add(footer,0,1);
            page.Controls.Add(new Label { Text = "Halo3DS Setup", Font = new Font("Segoe UI", 22F, FontStyle.Bold), AutoSize = true, Margin = new Padding(0,0,0,6) },0,0);
            page.Controls.Add(new Label { Text = "Prepare your SD card for any modded Nintendo 3DS or 2DS.\r\nThe Halo3DS game launcher is included. No Developer Mode is needed.", AutoSize = true, Margin = new Padding(0,0,0,14) },0,1);
            inputs.Dock = DockStyle.Top; inputs.AutoSize = true; inputs.ColumnCount = 1; inputs.RowCount = 5;
            inputs.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
            page.Controls.Add(inputs,0,2);
            AddPath(inputs, "1. Your Xbox game files", source,
                new[] { "Choose XISO...", "Choose folder..." }, new Action[] { delegate { PickFile(source,"Xbox disc image|*.iso;*.xiso|All files|*.*"); }, delegate { PickFolder(source); } });
            inputs.Controls.Add(new Label { Text = "Use your English North American retail Xbox copy of Halo: Combat Evolved.\r\nChoose an XISO, a full game folder, or a maps folder. PC / MCC files are not supported.", AutoSize=true, Margin=new Padding(0,0,0,12) });
            AddPath(inputs, "2. SD card root or output folder", destination,
                new[] { "Choose folder..." }, new Action[] { delegate { PickFolder(destination); } });
            inputs.Controls.Add(new Label { Text = "For example H:\\. The card must be FAT32. Setup does not format or erase it.", AutoSize=true, Margin=new Padding(0,0,0,8) });
            updateOnly.Text = "Update the launcher only (game files already installed)"; updateOnly.AutoSize = true;
            updateOnly.CheckedChanged += delegate { source.Enabled = !updateOnly.Checked; };
            inputs.Controls.Add(updateOnly);
            var advanced = new GroupBox { Text = "Additional options (usually unnecessary)", AutoSize = true, Dock=DockStyle.Top, Padding=new Padding(10), Margin=new Padding(0,12,0,8) };
            var advancedRows = new TableLayoutPanel { AutoSize=true, Dock=DockStyle.Top, ColumnCount=1 };
            advancedRows.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100)); advanced.Controls.Add(advancedRows); page.Controls.Add(advanced,0,3);
            AddPath(advancedRows,"HUD text: matching default.xbe or existing HUD file",extra,new[]{"Browse..."},new Action[]{delegate { PickFile(extra,"Halo compatibility files|*.xbe;*.bin|All files|*.*"); }});
            AddPath(advancedRows,"Game build override: leave empty to use the included launcher",build,new[]{"Browse..."},new Action[]{delegate { PickFile(build,"3DS homebrew|*.3dsx"); }});
            worker.DoWork += Work; worker.RunWorkerCompleted += Finished;
            status.Text = "Choose your game files and a destination to get started."; status.AutoSize = true;
            status.Dock = DockStyle.Fill; status.Margin = new Padding(0,8,0,8); footer.Controls.Add(status,0,0);
            progress.Dock = DockStyle.Top; progress.Height=(int)(16*scale); progress.Margin=new Padding(0,0,0,6); footer.Controls.Add(progress,0,1);
            var actions = new FlowLayoutPanel { Dock=DockStyle.Top,AutoSize=true, FlowDirection=FlowDirection.RightToLeft, WrapContents=true };
            prepare.Text="Prepare SD card"; prepare.AutoSize=true; prepare.Padding=new Padding(10,3,10,3); prepare.Click+=Start;
            cancel.Text="Cancel"; cancel.AutoSize=true; cancel.Enabled=false; cancel.Click+=delegate { cancellation.Cancel(); cancel.Enabled=false; status.Text="Cancelling safely..."; };
            var logs = new Button { Text="Open logs", AutoSize=true };
            logs.Click+=delegate { try { Directory.CreateDirectory(logDirectory); System.Diagnostics.Process.Start("explorer.exe", "\""+logDirectory+"\""); } catch(Exception e) { ShowError(e.Message); } };
            var sourceButton=new Button{Text="Source / licenses",AutoSize=true};
            sourceButton.Click+=delegate { using(var dialog=new SaveFileDialog {FileName="Halo3DS-Setup-Windows7-Source.zip",Filter="ZIP archive|*.zip"})if(dialog.ShowDialog(this)==DialogResult.OK)try {
                using(var resource=Assembly.GetExecutingAssembly().GetManifestResourceStream("HaloSetup.Source.zip"))
                using(var output=new FileStream(dialog.FileName,FileMode.Create,FileAccess.Write))
                {byte[] buffer=new byte[65536];int n;while((n=resource.Read(buffer,0,buffer.Length))>0)output.Write(buffer,0,n);}
            }catch(Exception error){ShowError(error.Message);} };
            actions.Controls.Add(prepare); actions.Controls.Add(cancel); actions.Controls.Add(logs);actions.Controls.Add(sourceButton); footer.Controls.Add(actions,0,2);
            FormClosing+=delegate(object sender,FormClosingEventArgs e) { if(worker.IsBusy) { e.Cancel=true; cancellation.Cancel(); cancel.Enabled=false; status.Text="Cancelling safely... Close the window once cancellation finishes."; } };
            // Disable all editable fields together while the worker owns the destination.
            advanced.Tag = "inputs";
        }
        static void AddPath(TableLayoutPanel parent,string caption,TextBox box,string[] labels,Action[] actions)
        {
            var row = new TableLayoutPanel { AutoSize=true, Dock=DockStyle.Top, ColumnCount=labels.Length+1, Margin=new Padding(0,0,0,6) };
            row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
            for(int i=0;i<labels.Length;i++)row.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            var label=new Label{Text=caption,AutoSize=true,Margin=new Padding(0,3,0,3)};row.Controls.Add(label,0,0);row.SetColumnSpan(label,labels.Length+1);
            box.Dock=DockStyle.Fill;box.Margin=new Padding(0,3,6,3);row.Controls.Add(box,0,1);
            for(int i=0;i<labels.Length;i++){Action action=actions[i];var button=new Button{Text=labels[i],AutoSize=true,Margin=new Padding(2,1,0,1)};button.Click+=delegate{action();};row.Controls.Add(button,i+1,1);}
            parent.Controls.Add(row);
        }
        static void PickFolder(TextBox box) { using(var dialog=new FolderBrowserDialog { Description="Choose your game folder, SD card root, or output folder.", ShowNewFolderButton=true }) { if(Directory.Exists(box.Text))dialog.SelectedPath=box.Text;if(dialog.ShowDialog()==DialogResult.OK)box.Text=dialog.SelectedPath; } }
        static void PickFile(TextBox box,string filter) { using(var dialog=new OpenFileDialog { Filter=filter,CheckFileExists=true })if(dialog.ShowDialog()==DialogResult.OK)box.Text=dialog.FileName; }
        void ShowError(string message) { MessageBox.Show(this,message,"Setup needs your attention",MessageBoxButtons.OK,MessageBoxIcon.Warning); }
        internal void ConfigureSmoke(string input,string target,string report)
        {
            // Test the real UI worker, bundled launcher extraction and completion
            // without modifying an SD card or waiting for a modal success dialog.
            Data.Require(Directory.Exists(target) && Directory.GetFileSystemEntries(target).Length==0,"Smoke test destination must be an empty test folder.");
            source.Text=input;destination.Text=target;smokeReport=report;
            Shown+=delegate{BeginInvoke((MethodInvoker)delegate{prepare.PerformClick();});};
        }
        void SetBusy(bool value)
        {
            inputs.Enabled=!value;extra.Enabled=!value;build.Enabled=!value;
            SetGroupsEnabled(this,!value);
            prepare.Enabled=!value;cancel.Enabled=value; UseWaitCursor=value;
        }
        static void SetGroupsEnabled(Control root,bool enabled) {foreach(Control control in root.Controls){if(control is GroupBox)control.Enabled=enabled;else SetGroupsEnabled(control,enabled);}}
        void Start(object sender,EventArgs e)
        {
            try
            {
                string target=Installer.NormalizeDestination(destination.Text);
                Data.Require(Directory.Exists(target),"Choose an existing SD card root or output folder.");
                Data.Require(updateOnly.Checked || source.Text.Trim().Length>0,"Choose your Xbox XISO or dumped maps folder first.");
                Directory.CreateDirectory(logDirectory);
                logPath=Path.Combine(logDirectory,"windows7-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N")+".txt");
                log=new StreamWriter(logPath);log.AutoFlush=true;log.WriteLine("Halo3DS Setup Windows 7 1.18.0 | OS: "+Environment.OSVersion+" | CLR: "+Environment.Version);
                cancellation=new CancellationTokenSource(); progress.Value=0; SetBusy(true);
                worker.RunWorkerAsync(new SetupOptions(source.Text.Trim(),target,build.Text.Trim(),extra.Text.Trim(),updateOnly.Checked));
            }
            catch(Exception error){if(log!=null){log.Dispose();log=null;}SetBusy(false);if(smokeReport!=null){File.WriteAllText(smokeReport,error.ToString());Environment.ExitCode=1;Close();}else ShowError(error.Message);}
        }
        void Work(object sender,DoWorkEventArgs e)
        {
            var options=(SetupOptions)e.Argument;
            try
            {
                if(options.Executable.Length==0)
                {
                    temporaryBuild=Path.Combine(Path.GetTempPath(),"halo3ds-win7-"+Guid.NewGuid().ToString("N")+".3dsx");
                    using(var resource=Assembly.GetExecutingAssembly().GetManifestResourceStream("HaloSetup.Halo3DS.3dsx"))
                    using(var output=new FileStream(temporaryBuild,FileMode.CreateNew,FileAccess.Write,FileShare.None))
                    { byte[] buffer=new byte[65536];int n;while((n=resource.Read(buffer,0,buffer.Length))>0)output.Write(buffer,0,n); }
                    options.Executable=temporaryBuild;
                }
                e.Result=Installer.Run(options,this,cancellation.Token);
            }
            finally { if(temporaryBuild!=null){try{File.Delete(temporaryBuild);}catch(IOException){}temporaryBuild=null;} }
        }
        public void Report(SetupProgress value)
        {
            BeginInvoke((MethodInvoker)delegate { progress.Value=Math.Max(0,Math.Min(100,(int)value.Percent));status.Text=value.Message;log.WriteLine(value.Percent.ToString("F1")+"% "+value.Message); });
        }
        void Finished(object sender,RunWorkerCompletedEventArgs e)
        {
            SetBusy(false);cancellation.Dispose();
            if(e.Error!=null)
            {
                log.WriteLine(e.Error);log.Dispose();log=null;
                if(smokeReport!=null){File.WriteAllText(smokeReport,e.Error.ToString());Environment.ExitCode=1;Close();return;}
                status.Text=e.Error is HaloSetup.OperationCanceledException ? "Cancelled. Run setup again to finish preparing the card." : "Setup could not finish. Your log is available with Open logs.";
                if(!(e.Error is HaloSetup.OperationCanceledException))ShowError(e.Error.Message+"\r\n\r\nLog: "+logPath);
                return;
            }
            var result=(SetupResult)e.Result;log.WriteLine("Completed: "+result.Destination);log.Dispose();log=null;
            progress.Value=100;status.Text="Ready! Safely eject the SD card, then open Halo3DS in the Homebrew Launcher.";
            string message="Your Halo3DS launcher is ready."+(result.Maps>0 ? "\r\nMaps prepared: "+result.Prepared+". Already up to date: "+result.AlreadyCorrect+"." : "")+"\r\n\r\nSafely eject the card, then open Halo3DS in the Homebrew Launcher.";
            if(!result.DspPresent)message+="\r\n\r\nFor sound, dump DSP firmware once on your console: press L + D-pad Down + SELECT, then Miscellaneous options > Dump DSP firmware.";
            if(smokeReport!=null){File.WriteAllText(smokeReport,"PASS UI install; prepared="+result.Prepared+"; CLR="+Environment.Version+"; bundled launcher="+Data.HashFile(Path.Combine(result.Destination,"3ds\\Halo3DS\\Halo3DS.3dsx")));Close();return;}
            MessageBox.Show(this,message,"Ready to play",MessageBoxButtons.OK,MessageBoxIcon.Information);
        }
    }
}
