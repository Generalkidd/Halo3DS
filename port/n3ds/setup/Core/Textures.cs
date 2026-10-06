using System.Text;

namespace HaloSetup;

// Optional, derived GPU data. Maps remain authoritative and the game falls
// back to decoding them if this file is absent, incomplete or incompatible.
public static class Textures
{
    const uint Magic = 0x31585448;
    public static uint Hash(byte[] bytes)
    { uint h = 2166136261; foreach (byte b in bytes) h = unchecked((h ^ b) * 16777619); return h; }
    public static bool Current(string map,string archive,CancellationToken ct=default)
    {
        if(!File.Exists(archive)) return false;
        try {
            using(var input=File.OpenRead(map)) using(var packed=File.OpenRead(archive)) {
                byte[] h=Read(packed,0,32);uint count=Data.U32(h,12);
                if(Data.U32(h,0)!=Magic||Data.U32(h,4)!=1||Data.U32(h,8)!=Hash(Read(input,0,2048))||count==0||count>16384||Data.U32(h,20)!=packed.Length||Data.U32(h,24)!=0||Data.U32(h,28)!=0)return false;
                byte[] index=Read(packed,32,checked((int)count*32));if(Hash(index)!=Data.U32(h,16))return false;
                for(int i=0;i<count;i++) {
                    ct.ThrowIfCancellationRequested();int at=i*32;uint wh=Data.U32(index,at+16),w=wh&65535,hh=wh>>16,bytes=Data.U32(index,at+24),offset=Data.U32(index,at+20);
                    if(w<8||hh<8||w>512||hh>512||(w&(w-1))!=0||(hh&(hh-1))!=0||bytes!=w*hh*4||offset<32+count*32||Hash(Read(packed,offset,(int)bytes))!=Data.U32(index,at+28))return false;
                }
                return true;
            }
        } catch(SetupException) {return false;} catch(IOException) {return false;} catch(OverflowException) {return false;}
    }
    static byte[] Read(Stream s, long offset, int length)
    {
        Data.Require(offset >= 0 && length >= 0 && offset <= s.Length - length, "Texture data exceeds its map.");
        s.Position = offset; byte[] b = new byte[length];
        for (int n = 0; n < length;) { int got = s.Read(b, n, length - n); Data.Require(got > 0, "Incomplete texture data."); n += got; }
        return b;
    }
    static int Pointer(byte[] tags, uint address, int length)
    {
        long p = (long)address - 0x803a6000;
        Data.Require(p >= 0 && length >= 0 && p <= tags.Length - length, "Texture pointer exceeds tag data."); return (int)p;
    }
    static int Swizzle(int x, int y, int w, int h)
    {
        int index = 0, dest = 1;
        for (int bit = 1; bit < w || bit < h; bit <<= 1)
        { if (bit < w) { if ((x & bit) != 0) index |= dest; dest <<= 1; } if (bit < h) { if ((y & bit) != 0) index |= dest; dest <<= 1; } }
        return index;
    }
    static uint Raw(byte[] p, int n, int f)
    {
        uint a=255,r=255,g=255,b=255,v;
        switch(f) {
        case 0:a=p[n];break; case 1:r=g=b=p[n];break; case 2:a=r=g=b=p[n];break;
        case 3:r=g=b=p[n];a=p[n+1];break;
        case 6:v=(uint)(p[n]|p[n+1]<<8);r=(v>>11)&31;r=(r<<3)|(r>>2);g=(v>>5)&63;g=(g<<2)|(g>>4);b=v&31;b=(b<<3)|(b>>2);break;
        case 8:v=(uint)(p[n]|p[n+1]<<8);a=(v&32768)!=0?255u:0;r=(v>>10)&31;r=(r<<3)|(r>>2);g=(v>>5)&31;g=(g<<3)|(g>>2);b=v&31;b=(b<<3)|(b>>2);break;
        case 9:v=(uint)(p[n]|p[n+1]<<8);a=((v>>12)&15)*17;r=((v>>8)&15)*17;g=((v>>4)&15)*17;b=(v&15)*17;break;
        case 10:case 11:b=p[n];g=p[n+1];r=p[n+2];if(f==11)a=p[n+3];break;
        }
        return r<<24|g<<16|b<<8|a;
    }
    static void Dxt(byte[] p, int start, int f, uint[] colors)
    {
        int c=start+(f==14?0:8),c0=Data.U16(p,c),c1=Data.U16(p,c+2);
        bool four=f!=14||c0>c1;
        for(int i=0;i<2;i++) {uint v=(uint)(i==0?c0:c1);colors[i]=(((v>>11)&31)*255/31)<<24|(((v>>5)&63)*255/63)<<16|((v&31)*255/31)<<8|255;}
        colors[2]=255;colors[3]=four?255u:0;
        for(int sh=8;sh<=24;sh+=8) {uint a=(colors[0]>>sh)&255,b=(colors[1]>>sh)&255;colors[2]|=(four?(2*a+b)/3:(a+b)/2)<<sh;colors[3]|=(four?(a+2*b)/3:0)<<sh;}
    }
    // Exact same nearest sampling, DXT rounding and vertical/tile layout as
    // the runtime decoder. No ETC recompression or loss of texture detail.
    public static byte[] Convert(byte[] src,int f,int w,int h,int tw,int th)
    {
        Data.Require(w>=4&&h>=4&&w<=1024&&h<=1024&&(w&(w-1))==0&&(h&(h-1))==0&&tw>=8&&th>=8&&tw<=512&&th<=512,"Invalid texture dimensions.");
        bool raw=f<14; int stride=f<=2?1:f>=10?4:2,block=f==14?8:16;
        Data.Require((f>=0&&f<=3)||f==6||(f>=8&&f<=11)||(f>=14&&f<=16),"Unsupported texture format.");
        Data.Require(src.Length >= (raw?w*h*stride:((w+3)/4)*((h+3)/4)*block),"Incomplete texture pixels.");
        byte[] output=new byte[tw*th*4];uint[] colors=new uint[4];int last=-1;ulong alpha=0;uint bits=0;byte[] av=new byte[8];
        int[] spread={0,1,4,5,16,17,20,21};
        for(int y=0;y<th;y++) for(int x=0;x<tw;x++) {
            int sx=x*w/tw,sy=y*h/th;uint color;
            if(raw) color=Raw(src,Swizzle(sx,sy,w,h)*stride,f);
            else {
                int at=((sy/4)*((w+3)/4)+sx/4)*block,pixel=(sy%4)*4+sx%4;
                if(at!=last) {
                    Dxt(src,at,f,colors);bits=Data.U32(src,at+(f==14?4:12));alpha=0;
                    if(f==15) for(int i=0;i<8;i++) alpha|=(ulong)src[at+i]<<(8*i);
                    if(f==16) {av[0]=src[at];av[1]=src[at+1];int den=av[0]>av[1]?7:5;for(int i=1;i<den;i++)av[i+1]=(byte)(((den-i)*av[0]+i*av[1])/den);if(den==5){av[6]=0;av[7]=255;}for(int i=0;i<6;i++)alpha|=(ulong)src[at+2+i]<<(8*i);}
                    last=at;
                }
                color=colors[(bits>>(pixel*2))&3];
                if(f==15) color=(color&0xffffff00)|(uint)(((alpha>>(pixel*4))&15)*17);
                if(f==16) color=(color&0xffffff00)|av[(alpha>>(pixel*3))&7];
            }
            int ny=th-1-y,n=(((ny/8)*(tw/8)+x/8)*64+spread[x&7]+2*spread[ny&7])*4;
            output[n]=(byte)color;output[n+1]=(byte)(color>>8);output[n+2]=(byte)(color>>16);output[n+3]=(byte)(color>>24);
        }
        return output;
    }
    public static void Generate(string map,string output,CancellationToken ct=default)
    {
        using(var input=File.OpenRead(map)) {
            byte[] header=Read(input,0,2048);
            Data.Require(Data.U32(header,0)==0x68656164&&Data.U32(header,4)==5&&Data.U32(header,20)>=36&&Data.U32(header,20)<=0x1600000,"Invalid expanded Xbox map header.");
            byte[] tags=Read(input,Data.U32(header,16),checked((int)Data.U32(header,20)));
            int count=checked((int)Data.U32(tags,12));Data.Require(count>0&&count<=65535,"Invalid tag directory.");
            int directory=Pointer(tags,Data.U32(tags,0),checked(count*32));
            var records=new List<uint[]>();
            // Reserve a bounded index; compact it after generation. Each map
            // uses one seekable archive, not thousands of individual SD files.
            using(var target=new FileStream(output,FileMode.Create,FileAccess.ReadWrite,FileShare.None)) using(var writer=new BinaryWriter(target)) {
                const int capacity=16384;target.Position=32+capacity*32;
                for(int i=0;i<count;i++) {
                    ct.ThrowIfCancellationRequested();int tag=directory+i*32;if(Data.U32(tags,tag)!=0x6269746d)continue;
                    int group=Pointer(tags,Data.U32(tags,tag+20),108),np=Pointer(tags,Data.U32(tags,tag+16),1),end=Array.IndexOf(tags,(byte)0,np);
                    Data.Require(end>=np&&end-np<256,"Invalid bitmap name.");string name=Encoding.ASCII.GetString(tags,np,end-np);
                    int n=checked((int)Data.U32(tags,group+96));if(n==0)continue;
                    int baseAt=Pointer(tags,Data.U32(tags,group+100),checked(n*48));
                    for(int k=0;k<n;k++) {
                        ct.ThrowIfCancellationRequested();
                        int at=baseAt+k*48,f=Data.U16(tags,at+12),flags=Data.U16(tags,at+14),w0=Data.U16(tags,at+4),h0=Data.U16(tags,at+6),mips=Data.U16(tags,at+20);
                        bool raw=(f<=3||f==6||(f>=8&&f<=11));
                        if(Data.U32(tags,at)!=0x6269746d||Data.U16(tags,at+10)!=0||Data.U16(tags,at+8)!=1||!(raw||(f>=14&&f<=16))||(flags&0x80)==0||(flags&20)!=0||
                           (raw?((flags&8)==0||(flags&2)!=0):(flags&8)!=0)||w0<4||h0<4||w0>2048||h0>2048||(w0&(w0-1))!=0||(h0&(h0-1))!=0||mips>10)continue;
                        var resolutions=new HashSet<int>();
                        foreach(int modelLimit in new[]{32,64,128}) {
                            int limit=modelLimit;if(name.StartsWith("ui\\",StringComparison.Ordinal)&&limit<64)limit=64;
                            if(Data.U16(tags,group)==4||name.StartsWith("ui\\hud\\",StringComparison.Ordinal))limit=128;
                            if(System.IO.Path.GetFileNameWithoutExtension(map)=="ui"&&name.StartsWith("ui\\shell\\",StringComparison.Ordinal)) {
                                if(name.Contains("\\header_")||name.StartsWith("ui\\shell\\main_menu\\menu_",StringComparison.Ordinal))limit=256;
                                else if(name=="ui\\shell\\main_menu\\halo_logo")limit=512;
                            }
                            if(!resolutions.Add(limit))continue;
                            int w=w0,h=h0,skip=0,level=0,stride=f<=2?1:f>=10?4:2,block=f==14?8:16;
                            while((w>limit||h>limit)&&w>=16&&h>=16&&level<mips){skip+=raw?w*h*stride:((w+3)/4)*((h+3)/4)*block;w>>=1;h>>=1;level++;}
                            if(w>1024||h>1024)continue;
                            int bytes=raw?w*h*stride:((w+3)/4)*((h+3)/4)*block,tw=Math.Max(8,Math.Min(w,limit)),th=Math.Max(8,Math.Min(h,limit));
                            uint pixels=Data.U32(tags,at+24),size=Data.U32(tags,at+28);Data.Require(pixels>=2048&&skip<=size&&bytes<=size-skip,"Invalid bitmap extent.");
                            byte[] tiled=Convert(Read(input,(long)pixels+skip,bytes),f,w,h,tw,th);
                            Data.Require(records.Count<capacity,"Too many native textures.");
                            records.Add(new[]{(uint)at,(uint)limit,pixels+(uint)skip,(uint)bytes,(uint)(tw|th<<16),(uint)target.Position,(uint)tiled.Length,Hash(tiled)});writer.Write(tiled);
                        }
                    }
                }
                records.Sort((a,b)=>a[0]!=b[0]?a[0].CompareTo(b[0]):a[1].CompareTo(b[1]));
                // The reserved index is only 512 KiB per map; keeping offsets
                // fixed avoids rereading/repacking every generated pixel.
                byte[] indexBytes;
                using(var ms=new MemoryStream()) {using(var iw=new BinaryWriter(ms))foreach(var row in records)foreach(uint v in row)iw.Write(v);indexBytes=ms.ToArray();}
                long sizeFile=target.Length;target.Position=0;writer.Write(Magic);writer.Write(1u);writer.Write(Hash(header));writer.Write((uint)records.Count);writer.Write(Hash(indexBytes));writer.Write((uint)sizeFile);writer.Write(0u);writer.Write(0u);writer.Write(indexBytes);writer.Flush();
            }
        }
    }
}
