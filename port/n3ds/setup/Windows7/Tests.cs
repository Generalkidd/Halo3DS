using System;
using System.IO;
using System.Linq;
using System.Text;
using HaloSetup;

static class Tests
{
    static int count;
    static void Pass(string text) { Console.WriteLine("PASS "+text);count++; }
    static void Reject(string text,Action action) { try { action(); } catch(SetupException) { Pass(text);return; }throw new Exception("Unexpected success: "+text); }
    static void Check(bool good,string text) { if(!good)throw new Exception(text);Pass(text); }
    static void Copy(Stream input,string output)
    { using(var f=new FileStream(output,FileMode.CreateNew)){byte[] bytes=new byte[65536];int n;while((n=input.Read(bytes,0,bytes.Length))>0)f.Write(bytes,0,n);} }
    [STAThread]
    static int Main(string[] args)
    {
        try
        {
            Console.WriteLine("Runtime: "+Environment.Version+"; process bits: "+(IntPtr.Size*8));
            string root=Path.GetFullPath(args[args.Length-1]);
            if(Directory.Exists(root))throw new Exception("Tests need a fresh output folder");
            Directory.CreateDirectory(root);
            if(args[0]=="package")
            {
                var app=System.Reflection.Assembly.LoadFile(Path.GetFullPath(args[1]));
                Check(app.ImageRuntimeVersion=="v2.0.50727","CLR 2 target");
                var title=(System.Reflection.AssemblyTitleAttribute)Attribute.GetCustomAttribute(app,typeof(System.Reflection.AssemblyTitleAttribute));
                Check(title.Title=="Halo3DS Setup - Windows 7","readable assembly title");
                using(var form=(System.Windows.Forms.Form)Activator.CreateInstance(app.GetType("HaloSetup.Windows7.SetupForm"),true))
                {
                    Check(form.Text=="Halo3DS Setup - Windows 7","readable window title");
                    var labels=new System.Collections.Generic.List<string>();CollectText(form,labels);
                    Check(labels.All(t=>t.All(c=>c<128)),"UI text contains no corrupted punctuation");
                    Check(labels.Contains("Choose XISO...") && labels.Contains("Choose folder...") && labels.Contains("Browse..."),"readable browse buttons in compiled app");
                }
                foreach(var reference in app.GetReferencedAssemblies())
                    Check(reference.Version.Major<=3,"legacy reference "+reference.FullName);
                var resources=app.GetManifestResourceNames();
                Check(resources.Length==4 && resources.Contains("HaloSetup.Source.zip") && resources.Contains("HaloSetup.Halo3DS.3dsx"),"only schemas, catalog, source and launcher embedded");
                using(var resource=app.GetManifestResourceStream("HaloSetup.Halo3DS.3dsx"))Copy(resource,Path.Combine(root,"Halo3DS.3dsx"));
                Check(Data.HashFile(Path.Combine(root,"Halo3DS.3dsx"))==Data.HashFile(args[2]),"embedded latest launcher");
                using(var resource=app.GetManifestResourceStream("HaloSetup.Source.zip"))Copy(resource,Path.Combine(root,"Source.zip"));
            }
            else if(args[0]=="textures")
            {
                string archive=Path.Combine(root,"test.ntx");
                Textures.Generate(args[1],archive);
                Check(Textures.Current(args[1],archive),"legacy texture archive read-back");
                Check(Data.HashFile(archive)==Data.HashFile(args[2]),"legacy and modern archives are identical");
                using(var cts=new CancellationTokenSource()) {
                    cts.Cancel();try {Textures.Current(args[1],archive,cts.Token);throw new Exception("Cancellation ignored");}
                    catch(HaloSetup.OperationCanceledException){Pass("texture verification cancellation");}
                }
                using(var file=new FileStream(archive,FileMode.Open,FileAccess.ReadWrite)) {file.Position=file.Length-1;int value=file.ReadByte();file.Position--;file.WriteByte((byte)(value^1));}
                Check(!Textures.Current(args[1],archive),"damaged texture payload rejected");
                File.WriteAllBytes(archive,new byte[12]);
                Check(!Textures.Current(args[1],archive),"truncated texture archive rejected");
                File.Delete(archive);Check(!Textures.Current(args[1],archive),"missing texture archive detected");
            }
            else if(args[0]=="loading")
            {
                byte[] art=Loading.Find(Sources.Discover(args[1]),null,root);
                Check(art!=null && Data.Hash(art)==Loading.AssetHash,"loading artwork from XISO");
                File.WriteAllBytes(Path.Combine(root,Loading.Name),art);
                Check(Loading.Extract(art)!=null,"loading artwork reuse");
                art[100]^=1;Check(Loading.Extract(art)==null,"damaged loading artwork rejected");
                Check(Loading.Extract(new byte[5])==null,"truncated loading artwork rejected");
            }
            else if(args[0]=="links")
            {
                string linked=Path.GetFullPath(args[1]);
                foreach(string selected in new[]{linked,linked+"\\",Path.Combine(linked,"ordinary"),Path.Combine(linked,"ordinary")+"\\"})
                    Check(Installer.SafePath(selected,"halo-source/ui.map")==Path.Combine(selected,"halo-source\\ui.map"),"selected link or linked ancestor allowed: "+selected);
                Reject("child directory link",delegate{Installer.SafePath(linked,"escape/ui.map");});
                Reject("child link itself",delegate{Installer.SafePath(linked,"escape");});
                Reject("dangling child link",delegate{Installer.SafePath(linked,"dangling/ui.map");});
                Reject("traversal beneath linked root",delegate{Installer.SafePath(linked,"../outside/file");});
            }
            else if(args[0]=="audit")
            {
                foreach(var pair in Data.Catalog)
                {
                    Relocation.Generate(Path.Combine(args[1],pair.Key+".map"),root,pair.Key);
                    foreach(var sidecar in pair.Value.Sidecars)Check(Data.HashFile(Path.Combine(root,sidecar.Key))==sidecar.Value,sidecar.Key+" matches catalog");
                }
            }
            else if(args[0]=="exercise") Exercise(args[1],args[2],args[3],root);
            else Safety(root);
            Console.WriteLine("All "+count+" checks passed.");return 0;
        }
        catch(Exception e){Console.Error.WriteLine(e);return 1;}
    }
    static void CollectText(System.Windows.Forms.Control control,System.Collections.Generic.List<string> labels)
    {
        labels.Add(control.Text);
        foreach(System.Windows.Forms.Control child in control.Controls)CollectText(child,labels);
    }
    static void Safety(string root)
    {
        Check(Data.Catalog.Count==24,"embedded catalog loads");
        Reject("path traversal",delegate { Installer.SafePath(root,"../escape"); });
        Reject("sibling prefix escape",delegate { Installer.SafePath(root,"../"+Path.GetFileName(root)+"-other/file"); });
        Reject("absolute destination child",delegate { Installer.SafePath(root,Path.Combine(root,"absolute")); });
        Reject("empty destination",delegate { Installer.NormalizeDestination(" "); });
        foreach(string drive in new[]{"H:","D:","Z:"})foreach(string form in new[]{drive,drive+"\\",drive+"/"})
            Check(Installer.SafePath(form,"3ds/Halo3DS/Halo3DS.3dsx")==drive+"\\3ds\\Halo3DS\\Halo3DS.3dsx","drive path "+form+" (no writes)");
        Reject("invalid HUD",delegate { Hud.Extract(new byte[6928]); });
        Reject("empty maps folder",delegate { Sources.Discover(root); });
        string bad=Path.Combine(root,"bad.iso");File.WriteAllBytes(bad,new byte[2048]);
        Reject("invalid XISO",delegate { new Xiso(bad); });
        byte[] iso=new byte[0x14000];
        Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA").CopyTo(iso,0x10000);Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA").CopyTo(iso,0x107ec);
        BitConverter.GetBytes(34u).CopyTo(iso,0x10014);BitConverter.GetBytes(2048u).CopyTo(iso,0x10018);
        BitConverter.GetBytes(36u).CopyTo(iso,0x11004);BitConverter.GetBytes(4u).CopyTo(iso,0x11008);
        iso[0x1100d]=6;Encoding.ASCII.GetBytes("ui.map").CopyTo(iso,0x1100e);Encoding.ASCII.GetBytes("test").CopyTo(iso,0x12000);File.WriteAllBytes(bad,iso);
        Check(new Xiso(bad).Files["ui.map"].Small().SequenceEqual(Encoding.ASCII.GetBytes("test")),"bounded XISO entry");
        using(var slice=new Xiso(bad).Files["ui.map"].Open()) { byte[] buffer=new byte[10];Check(slice.Read(buffer,0,10)==4 && slice.Read(buffer,0,10)==0,"XISO cannot read past entry"); }
        iso[0x11000]=1;File.WriteAllBytes(bad,iso);Reject("damaged tree",delegate { new Xiso(bad); });
        iso[0x11000]=0;Encoding.ASCII.GetBytes("../bad").CopyTo(iso,0x1100e);File.WriteAllBytes(bad,iso);Reject("unsafe XISO filename",delegate { new Xiso(bad); });
        using(var cts=new CancellationTokenSource()){cts.Cancel();try{Data.HashFile(bad,cts.Token);throw new Exception("Cancellation ignored");}catch(HaloSetup.OperationCanceledException){Pass("hash cancellation");}}
        string legacy=Path.Combine(root,"3ds\\HaloSourceEngine\\HaloSourceEngine.3dsx");Directory.CreateDirectory(Path.GetDirectoryName(legacy));File.WriteAllBytes(legacy,new byte[]{1,2,3});File.WriteAllText(Path.Combine(Path.GetDirectoryName(legacy),"keep.txt"),"keep");
        Installer.RetireLegacyLauncher(root);Installer.RetireLegacyLauncher(root);
        Check(!File.Exists(legacy) && File.Exists(Path.Combine(Path.GetDirectoryName(legacy),"keep.txt")) && Directory.GetFiles(Path.Combine(root,"halo-source\\launcher-backups"),"*.3dsx",SearchOption.AllDirectories).Length==1,"legacy launcher backup preserves unrelated files");
    }
    static void Exercise(string imagePath,string expanded,string executable,string root)
    {
        var files=Sources.Discover(imagePath);
        string input=Path.Combine(root,"input"),output=Path.Combine(root,"output");Directory.CreateDirectory(input);Directory.CreateDirectory(output);
        foreach(string name in new[]{"ui.map","bloodgulch.map","default.xbe"})using(var stream=files[name].Open())Copy(stream,Path.Combine(input,name));
        Check(files["ui.map"].Length==Data.Catalog["ui"].SourceBytes,"real XISO compressed input");
        var options=new SetupOptions(input,output,executable);
        using(var cts=new CancellationTokenSource())
        {
            try { Installer.Run(options,new Callback(p=>{if(p.Percent>3)cts.Cancel();}),cts.Token);throw new Exception("Cancellation ignored"); }
            catch(HaloSetup.OperationCanceledException) { Pass("conversion cancellation"); }
        }
        Check(Directory.GetDirectories(Path.Combine(output,"halo-source"),".halo-setup-*").Length==0,"cancelled staging cleaned");
        var first=Installer.Run(options);Check(first.Prepared==2,"compressed maps folder installation");
        foreach(string name in new[]{"ui","bloodgulch"})Check(Textures.Current(Path.Combine(output,"halo-source/"+name+".map"),Path.Combine(output,"halo-source/"+name+".ntx")),"prepared textures "+name);
        string state=Path.Combine(output,"halo-source\\state");Directory.CreateDirectory(state);File.WriteAllText(Path.Combine(state,"keep.txt"),"save data");
        File.WriteAllBytes(Path.Combine(output,"halo-source\\ui.nrl"),new byte[]{0});
        var repaired=Installer.Run(options);Check(repaired.Prepared==1 && repaired.AlreadyCorrect==1,"targeted sidecar repair");
        Check(Installer.Run(options).Prepared==0,"repeat run skips correct files");
        string texture=Path.Combine(output,"halo-source/ui.ntx");
        File.WriteAllBytes(texture,new byte[8]);
        Check(Installer.Run(options).Prepared==0 && Textures.Current(Path.Combine(output,"halo-source/ui.map"),texture),"damaged texture repair preserves maps");
        Installer.Run(new SetupOptions("",output,executable,null,true));
        Check(File.ReadAllText(Path.Combine(state,"keep.txt"))=="save data","launcher update preserves saves");
        Check(Data.HashFile(Path.Combine(output,"3ds\\Halo3DS\\Halo3DS.3dsx"))==Data.HashFile(executable),"launcher copied exactly");
        // A small valid image containing actual game entries exercises the entire XISO install path.
        string smallIso=Path.Combine(root,"two-map.iso");MakeIso(input,smallIso);
        string isoOutput=Path.Combine(root,"iso-output");Directory.CreateDirectory(isoOutput);
        Check(Installer.Run(new SetupOptions(smallIso,isoOutput,executable)).Prepared==2,"real compressed XISO installation");
        foreach(string name in new[]{"ui","bloodgulch"})
        {
            string target=Path.Combine(root,name+"-expanded.map");
            Installer.PrepareMap(SourceEntry.FromFile(Path.Combine(expanded,name+".map")),target,name,Data.Catalog[name],default(CancellationToken),delegate{});
            Check(Data.HashFile(target)==Data.Catalog[name].Sha256,"expanded input "+name);
        }
        using(var f=new FileStream(Path.Combine(input,"bloodgulch.map"),FileMode.Open)){f.Position=4096;f.WriteByte((byte)(f.ReadByte()^255));}
        File.Delete(Path.Combine(output,"halo-source\\bloodgulch.nrl"));
        Reject("corrupt source",delegate{Installer.Run(options);});
        Check(Data.HashFile(Path.Combine(output,"halo-source\\bloodgulch.map"))==Data.Catalog["bloodgulch"].Sha256,"good map survives failed repair");
        string naked=Path.Combine(root,"maps-only");Directory.CreateDirectory(naked);File.Copy(Path.Combine(input,"ui.map"),Path.Combine(naked,"ui.map"));
        Reject("missing HUD explanation",delegate{Hud.Find(Sources.Discover(naked),null,root);});
    }
    static void MakeIso(string input,string path)
    {
        byte[] head=new byte[0x11800];Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA").CopyTo(head,0x10000);Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA").CopyTo(head,0x107ec);
        BitConverter.GetBytes(34u).CopyTo(head,0x10014);BitConverter.GetBytes(2048u).CopyTo(head,0x10018);
        uint sector=35;string[] names={"bloodgulch.map","default.xbe","ui.map"};
        for(int i=0;i<names.Length;i++)
        {
            int pos=0x11000+i*64;long bytes=new FileInfo(Path.Combine(input,names[i])).Length;
            if(i+1<names.Length)BitConverter.GetBytes((ushort)((i+1)*16)).CopyTo(head,pos+2);
            BitConverter.GetBytes(sector).CopyTo(head,pos+4);BitConverter.GetBytes((uint)bytes).CopyTo(head,pos+8);
            head[pos+13]=(byte)names[i].Length;Encoding.ASCII.GetBytes(names[i]).CopyTo(head,pos+14);sector+=(uint)((bytes+2047)/2048);
        }
        using(var f=new FileStream(path,FileMode.CreateNew))
        {
            f.Write(head,0,head.Length);byte[] buffer=new byte[65536];
            foreach(string name in names){using(var source=File.OpenRead(Path.Combine(input,name))){int n;while((n=source.Read(buffer,0,buffer.Length))>0)f.Write(buffer,0,n);}long padded=(f.Position+2047)/2048*2048;f.SetLength(padded);f.Position=padded;}
        }
    }
    sealed class Callback : IProgress<SetupProgress>
    { readonly Action<SetupProgress> action;public Callback(Action<SetupProgress> action){this.action=action;}public void Report(SetupProgress p){action(p);} }
}
