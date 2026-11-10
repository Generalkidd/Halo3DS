// New3DS integration additions by Kevin Boggs.
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using HaloSetup;
#if LEGACY
using Token = HaloSetup.CancellationToken;
#else
using Token = System.Threading.CancellationToken;
#endif
// Kevin Boggs: RevisionTests
static class RevisionTests
{
    // Kevin Boggs: Run
    public static void Run(string parent)
    {
        string root=Path.Combine(parent,"revisions");Directory.CreateDirectory(root);
        byte[] data=new byte[4096];Encoding.ASCII.GetBytes("daeh").CopyTo(data,0);Encoding.ASCII.GetBytes("toof").CopyTo(data,2044);
        BitConverter.GetBytes(5u).CopyTo(data,4);BitConverter.GetBytes(4096u).CopyTo(data,8);
        BitConverter.GetBytes(2048u).CopyTo(data,16);BitConverter.GetBytes(2048u).CopyTo(data,20);
        Encoding.ASCII.GetBytes("ui").CopyTo(data,32);Encoding.ASCII.GetBytes("different-build").CopyTo(data,64);
        for(int i=2048;i<data.Length;i++)data[i]=0xab;
        var row=new MapInfo("Test",4096,"",0,"",new Dictionary<string,string>());
        string source=Path.Combine(root,"ui.map"),target=Path.Combine(root,"expanded.map");File.WriteAllBytes(source,data);
        Installer.PrepareMap(SourceEntry.FromFile(source),target,"ui",row,default(Token),delegate(double p){});
        if(Data.HashFile(target)!=Data.Hash(data))throw new Exception("Different revision expanded copy changed");
        if(Data.DiscWarning(source,default(Token),delegate(string s){}).Length==0)throw new Exception("Unrecognized image lacks warning");
        byte[] compressed=Convert.FromBase64String("eJxbvXoUjIJRMApGwSgYBSMNAAD0zVhM");
        using(var f=new FileStream(source,FileMode.Create)){f.Write(data,0,2048);f.Write(compressed,0,compressed.Length);}
        File.Delete(target);Installer.PrepareMap(SourceEntry.FromFile(source),target,"ui",row,default(Token),delegate(double p){});
        if(Data.HashFile(target)!=Data.Hash(data))throw new Exception("Different revision decompression changed");
        // A header claiming less output must stop the decoder, not overrun the destination.
        data[8]=0;data[9]=12;data[20]=0;data[21]=4;
        using(var f=new FileStream(source,FileMode.Create)){f.Write(data,0,2048);f.Write(compressed,0,compressed.Length);}
        File.Delete(target);bool rejected=false;
        try {Installer.PrepareMap(SourceEntry.FromFile(source),target,"ui",row,default(Token),delegate(double p){});} catch(SetupException){rejected=true;}
        if(!rejected)throw new Exception("Oversized expansion accepted");
        Console.WriteLine("PASS other revision expanded/compressed maps, warning and expansion bounds");
    }
}
