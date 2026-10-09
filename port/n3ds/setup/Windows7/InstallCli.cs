// New3DS integration additions by Kevin Boggs.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using HaloSetup;

// Console companion to the Windows 7 setup UI. The dual-ISO script uses this
// exact shared converter, so Xbox and PC installation can run in one flow.
// Kevin Boggs: InstallCli integrates owner supplied game data on Windows 7.
static class InstallCli
{
    sealed class Progress : IProgress<SetupProgress>
    {
        public void Report(SetupProgress value)
        { Console.WriteLine(value.Percent.ToString("F1") + "% " + value.Message); }
    }
    static int Main(string[] args)
    {
        if ((args.Length != 3 && args.Length != 4) ||
            (args[0] != "--validate" && args[0] != "--install"))
        {
            Console.Error.WriteLine("Usage: Halo3DS-Xbox-Install.exe --validate|--install <Xbox XISO or maps folder> <SD root> [existing HUD/compatibility file]");
            return 2;
        }
        string source=args[1], destination=args[2], extra=args.Length==4?args[3]:null;
        string temporary=null;
        try
        {
            destination=Installer.NormalizeDestination(destination);
            Data.Require(Directory.Exists(destination),"Choose an existing SD card root or output folder.");
            var files=Sources.Discover(source);
            var names=Data.Catalog.Keys.Where(n=>files.ContainsKey(n+".map")).ToList();
            Data.Require(names.Contains("ui") && names.Count>=2,"The Xbox source needs ui.map and a game map.");
            foreach(string name in names) Installer.CheckMap(files[name+".map"],name,Data.Catalog[name]);
            Hud.Find(files,extra,destination);
            Loading.Find(files,extra,destination);
            Console.WriteLine("Xbox source preflight passed: "+names.Count+" supported maps and HUD data.");
            if(args[0]=="--validate") return 0;
            temporary=Path.Combine(Path.GetTempPath(),"halo3ds-xbox-launcher-"+Guid.NewGuid().ToString("N")+".3dsx");
            using(Stream resource=Assembly.GetExecutingAssembly().GetManifestResourceStream("HaloSetup.Halo3DS.3dsx"))
            {
                Data.Require(resource!=null,"The Xbox 3DS launcher is missing from setup.");
                using(var output=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None))
                { byte[] buffer=new byte[65536];int read;while((read=resource.Read(buffer,0,buffer.Length))>0)output.Write(buffer,0,read); }
            }
            var result=Installer.Run(new SetupOptions(source,destination,temporary,extra),new Progress());
            Console.WriteLine("Xbox installation complete: "+result.Prepared+" maps prepared at "+result.Destination);
            return 0;
        }
        catch(Exception error)
        {
            Console.Error.WriteLine("Xbox installation failed: "+error.Message);
            return 1;
        }
        finally
        {
            if(temporary!=null && File.Exists(temporary)) File.Delete(temporary);
        }
    }
}
