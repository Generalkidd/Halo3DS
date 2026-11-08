using System.Text;

namespace HaloSetup;

// Optional, derived GPU data. Maps remain authoritative and the game falls
// back to decoding them if this file is absent, incomplete or incompatible.
public static partial class Textures
{
    const uint Magic = 0x31585448;
    // Same archive layout and filtered lightmaps; v3 regenerates older setup
    // output to include current display/menu plans and palettized bump maps.
    const uint Version = 3;
    // Exact ARGB values from source/bitmaps/bitmaps.c global_vector_palette.
    // Keep transparent/sentinel entries: normal reconstruction is not equivalent.
    static readonly uint[] VectorPalette = {
        0xFF7A19CC, 0xFF7E19CC, 0xFF8019CC, 0xFF8119CC, 0xFF8519CC, 0xFF742FE2, 0xFF7A2FE2, 0xFF7E2FE2,
        0xFF802FE2, 0xFF812FE2, 0xFF852FE2, 0xFF8B2FE2, 0xFF6B42ED, 0xFF7442EE, 0xFF7A42EF, 0xFF7E42EF,
        0xFF8042EF, 0xFF8142EF, 0xFF8542EF, 0xFF8B42EE, 0xFF9442ED, 0xFF6052F2, 0xFF6B52F5, 0xFF7452F6,
        0xFF7A52F7, 0xFF7E52F7, 0xFF8052F7, 0xFF8152F7, 0xFF8552F7, 0xFF8B52F6, 0xFF9452F5, 0xFF9F52F2,
        0xFF5260F2, 0xFF6060F7, 0xFF6B60F9, 0xFF7460FB, 0xFF7A60FB, 0xFF7E60FB, 0xFF8060FB, 0xFF8160FB,
        0xFF8560FB, 0xFF8B60FB, 0xFF9460F9, 0xFF9F60F7, 0xFFAD60F2, 0xFF426BED, 0xFF526BF5, 0xFF606BF9,
        0xFF6B6BFC, 0xFF746BFD, 0xFF7A6BFD, 0xFF7E6BFD, 0xFF806BFD, 0xFF816BFD, 0xFF856BFD, 0xFF8B6BFD,
        0xFF946BFC, 0xFF9F6BF9, 0xFFAD6BF5, 0xFFBD6BED, 0xFF2F74E2, 0xFF4274EE, 0xFF5274F6, 0xFF6074FB,
        0xFF6B74FD, 0xFF7474FE, 0xFF7A74FE, 0xFF7E74FE, 0xFF8074FE, 0xFF8174FE, 0xFF8574FE, 0xFF8B74FE,
        0xFF9474FD, 0xFF9F74FB, 0xFFAD74F6, 0xFFBD74EE, 0xFFD074E2, 0xFF197ACC, 0xFF2F7AE2, 0xFF427AEF,
        0xFF527AF7, 0xFF607AFB, 0xFF6B7AFD, 0xFF747AFE, 0xFF7A7AFF, 0xFF7E7AFF, 0xFF807AFF, 0xFF817AFF,
        0xFF857AFF, 0xFF8B7AFE, 0xFF947AFD, 0xFF9F7AFB, 0xFFAD7AF7, 0xFFBD7AEF, 0xFFD07AE2, 0xFFE57ACC,
        0xFF197ECC, 0xFF2F7EE2, 0xFF427EEF, 0xFF527EF7, 0xFF607EFB, 0xFF6B7EFD, 0xFF747EFE, 0xFF7A7EFF,
        0xFF7E7EFF, 0xFF807EFF, 0xFF817EFF, 0xFF857EFF, 0xFF8B7EFE, 0xFF947EFD, 0xFF9F7EFB, 0xFFAD7EF7,
        0xFFBD7EEF, 0xFFD07EE2, 0xFFE57ECC, 0xFF1980CC, 0xFF2F80E2, 0xFF4280EF, 0xFF5280F7, 0xFF6080FB,
        0xFF6B80FD, 0xFF7480FE, 0xFF7A80FF, 0xFF7E80FF, 0xFF8080FF, 0xFF8180FF, 0xFF8580FF, 0xFF8B80FE,
        0xFF9480FD, 0xFF9F80FB, 0xFFAD80F7, 0xFFBD80EF, 0xFFD080E2, 0xFFE580CC, 0xFF1981CC, 0xFF2F81E2,
        0xFF4281EF, 0xFF5281F7, 0xFF6081FB, 0xFF6B81FD, 0xFF7481FE, 0xFF7A81FF, 0xFF7E81FF, 0xFF8081FF,
        0xFF8181FF, 0xFF8581FF, 0xFF8B81FE, 0xFF9481FD, 0xFF9F81FB, 0xFFAD81F7, 0xFFBD81EF, 0xFFD081E2,
        0xFFE581CC, 0xFF1985CC, 0xFF2F85E2, 0xFF4285EF, 0xFF5285F7, 0xFF6085FB, 0xFF6B85FD, 0xFF7485FE,
        0xFF7A85FF, 0xFF7E85FF, 0xFF8085FF, 0xFF8185FF, 0xFF8585FF, 0xFF8B85FE, 0xFF9485FD, 0xFF9F85FB,
        0xFFAD85F7, 0xFFBD85EF, 0xFFD085E2, 0xFFE585CC, 0xFF2F8BE2, 0xFF428BEE, 0xFF528BF6, 0xFF608BFB,
        0xFF6B8BFD, 0xFF748BFE, 0xFF7A8BFE, 0xFF7E8BFE, 0xFF808BFE, 0xFF818BFE, 0xFF858BFE, 0xFF8B8BFE,
        0xFF948BFD, 0xFF9F8BFB, 0xFFAD8BF6, 0xFFBD8BEE, 0xFFD08BE2, 0xFF4294ED, 0xFF5294F5, 0xFF6094F9,
        0xFF6B94FC, 0xFF7494FD, 0xFF7A94FD, 0xFF7E94FD, 0xFF8094FD, 0xFF8194FD, 0xFF8594FD, 0xFF8B94FD,
        0xFF9494FC, 0xFF9F94F9, 0xFFAD94F5, 0xFFBD94ED, 0xFF529FF2, 0xFF609FF7, 0xFF6B9FF9, 0xFF749FFB,
        0xFF7A9FFB, 0xFF7E9FFB, 0xFF809FFB, 0xFF819FFB, 0xFF859FFB, 0xFF8B9FFB, 0xFF949FF9, 0xFF9F9FF7,
        0xFFAD9FF2, 0xFF60ADF2, 0xFF6BADF5, 0xFF74ADF6, 0xFF7AADF7, 0xFF7EADF7, 0xFF80ADF7, 0xFF81ADF7,
        0xFF85ADF7, 0xFF8BADF6, 0xFF94ADF5, 0xFF9FADF2, 0xFF6BBDED, 0xFF74BDEE, 0xFF7ABDEF, 0xFF7EBDEF,
        0xFF80BDEF, 0xFF81BDEF, 0xFF85BDEF, 0xFF8BBDEE, 0xFF94BDED, 0xFF74D0E2, 0xFF7AD0E2, 0xFF7ED0E2,
        0xFF80D0E2, 0xFF81D0E2, 0xFF85D0E2, 0xFF8BD0E2, 0xFF7AE5CC, 0xFF7EE5CC, 0xFF80E5CC, 0xFF81E5CC,
        0xFF85E5CC, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x008080FF,
    };
    public static uint Hash(byte[] bytes)
    { uint h = 2166136261; foreach (byte b in bytes) h = unchecked((h ^ b) * 16777619); return h; }
    public static bool Current(string map,string archive,CancellationToken ct=default)
    {
        if(!File.Exists(archive)) return false;
        try {
            using(var input=File.OpenRead(map)) using(var packed=File.OpenRead(archive)) {
                byte[] h=Read(packed,0,32);uint count=Data.U32(h,12);
                if(Data.U32(h,0)!=Magic||Data.U32(h,4)!=Version||Data.U32(h,8)!=Hash(Read(input,0,2048))||count==0||count>16384||Data.U32(h,20)!=packed.Length||Data.U32(h,24)!=0||Data.U32(h,28)!=0)return false;
                byte[] index=Read(packed,32,checked((int)count*32));if(Hash(index)!=Data.U32(h,16))return false;
                var verified=new Dictionary<uint,long>();
                for(int i=0;i<count;i++) {
                    ct.ThrowIfCancellationRequested();int at=i*32;uint wh=Data.U32(index,at+16),w=wh&65535,hh=wh>>16,bytes=Data.U32(index,at+24),offset=Data.U32(index,at+20);
                    if(w<8||hh<8||w>512||hh>512||(w&(w-1))!=0||(hh&(hh-1))!=0||bytes!=w*hh*4||offset<32+count*32||offset>packed.Length-bytes)return false;
                    uint hash=Data.U32(index,at+28);long signature=((long)bytes<<32)|hash;
                    // Several resolution keys may name one exact payload.
                    // Verify each distinct span/hash once, still checking every
                    // index entry and cancellation before reusing the result.
                    if(!verified.TryGetValue(offset,out long previous) || previous!=signature) {
                        if(Hash(Read(packed,offset,(int)bytes))!=hash)return false;
                        verified[offset]=signature;
                    }
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
    static int DisplayResolution(string name)
    {
        // Keep the authored-artwork list in engine_display_textures.inl exact.
        switch(name) {
        case "levels\\a10\\devices\\command display\\bitmaps\\command display":
        case "levels\\a10\\bitmaps\\monitors":
        case "levels\\a10\\bitmaps\\monitors glow":
        case "levels\\a10\\bitmaps\\monitor huge":
        case "levels\\a10\\bitmaps\\hushed casket display": return 256;
        case "levels\\a10\\devices\\command display\\bitmaps\\command nos":
        case "levels\\a10\\bitmaps\\monitor glass":
        case "vehicles\\fighterbomber\\bitmaps\\fb_monitor":
        case "levels\\b30\\devices\\interior tech objects\\holo control\\bitmaps\\holo control":
        case "levels\\a50\\devices\\interior tech objects\\holo control\\bitmaps\\holo control": return 128;
        default: return 0;
        }
    }
    static HashSet<int> ResolutionPlans(string name,int type,int usage,bool lightmap,bool menu)
    {
        var plans=new HashSet<int>();
        // Match bitmap_plan for both hardware families and the existing menu/
        // display baseline switches. Deduplicate equal effective resolutions.
        foreach(int modelLimit in new[]{32,64,128})
        foreach(bool original in new[]{true,false})
        foreach(bool menuBaseline in new[]{true,false})
        foreach(bool displayBaseline in new[]{true,false}) {
            int limit=modelLimit;
            if(menu && !menuBaseline)limit=Math.Max(limit,original?64:128);
            if(!original && !displayBaseline && type==0 && usage!=2 && !lightmap)
                limit=Math.Max(limit,DisplayResolution(name));
            if(name.StartsWith("ui\\",StringComparison.Ordinal))limit=Math.Max(limit,64);
            if(!menuBaseline && name.StartsWith("ui\\shell\\",StringComparison.Ordinal))limit=Math.Max(limit,128);
            if(type==4 || name.StartsWith("ui\\hud\\",StringComparison.Ordinal))limit=128;
            if(menu && name.StartsWith("ui\\shell\\",StringComparison.Ordinal)) {
                if(name.Contains("\\header_") || name.StartsWith("ui\\shell\\main_menu\\menu_",StringComparison.Ordinal))
                    limit=!original && !menuBaseline?512:256;
                else if(name=="ui\\shell\\main_menu\\halo_logo")limit=512;
            }
            plans.Add(limit);
        }
        return plans;
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
        case 17:v=VectorPalette[p[n]];return (v<<8)|(v>>24);
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
        bool raw=f<14||f==17; int stride=f<=2||f==17?1:f>=10?4:2,block=f==14?8:16;
        Data.Require((f>=0&&f<=3)||f==6||(f>=8&&f<=11)||(f>=14&&f<=17),"Unsupported texture format.");
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

    // Match engine_lightmap_filter.inl. Exclude Xbox's unused checker cells,
    // then extend the nearest lighting into empty cells in the same pass order.
    static byte[] FilterLightmap(byte[] source, int width, int height, int targetWidth, int targetHeight)
    {
        int[] xs = new int[width], ys = new int[height];
        for (int x = 0; x < width; ++x) xs[x] = Swizzle(x, 0, width, height);
        for (int y = 0; y < height; ++y) ys[y] = Swizzle(0, y, width, height);
        int cells = targetWidth * targetHeight;
        var distance = new ushort[cells];
        var pixels = new uint[cells];
        for (int y = 0; y < targetHeight; ++y)
        for (int x = 0; x < targetWidth; ++x)
        {
            int left = x * width / targetWidth, top = y * height / targetHeight;
            int right = Math.Max(left + 1, (x + 1) * width / targetWidth);
            int bottom = Math.Max(top + 1, (y + 1) * height / targetHeight);
            int red = 0, green = 0, blue = 0, samples = 0;
            for (int sy = top; sy < bottom; ++sy)
            for (int sx = left; sx < right; ++sx)
            {
                int texel = xs[sx] | ys[sy];
                int value = Data.U16(source, texel * 2);
                if ((value == 0xf800 || value == 0xffe0) &&
                    Data.U16(source, (texel ^ 1) * 2) == (value ^ 0x7e0) &&
                    Data.U16(source, (texel ^ 2) * 2) == (value ^ 0x7e0))
                    continue;
                int r = value >> 11, g = (value >> 5) & 63, b = value & 31;
                red += (r << 3) | (r >> 2);
                green += (g << 2) | (g >> 4);
                blue += (b << 3) | (b >> 2);
                ++samples;
            }
            int at = y * targetWidth + x;
            distance[at] = samples > 0 ? (ushort)0 : ushort.MaxValue;
            pixels[at] = samples > 0 ? (uint)((red + samples / 2) / samples) << 24 |
                (uint)((green + samples / 2) / samples) << 16 | (uint)((blue + samples / 2) / samples) << 8 | 255u : 255u;
        }
        for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < cells; ++i)
        {
            int at = pass == 0 ? i : cells - 1 - i;
            int x = at % targetWidth, y = at / targetWidth;
            int horizontal = pass == 0 ? (x > 0 ? at - 1 : at) : (x + 1 < targetWidth ? at + 1 : at);
            int vertical = pass == 0 ? (y > 0 ? at - targetWidth : at) : (y + 1 < targetHeight ? at + targetWidth : at);
            if (distance[horizontal] + 1 < distance[at])
            {
                distance[at] = (ushort)(distance[horizontal] + 1);
                pixels[at] = pixels[horizontal];
            }
            if (distance[vertical] + 1 < distance[at])
            {
                distance[at] = (ushort)(distance[vertical] + 1);
                pixels[at] = pixels[vertical];
            }
        }
        byte[] output = new byte[cells * 4];
        int[] spread = { 0, 1, 4, 5, 16, 17, 20, 21 };
        for (int y = 0; y < targetHeight; ++y)
        for (int x = 0; x < targetWidth; ++x)
        {
            int ny = targetHeight - 1 - y;
            int index = (((ny / 8) * (targetWidth / 8) + x / 8) * 64 + spread[x & 7] + 2 * spread[ny & 7]) * 4;
            uint color = pixels[y * targetWidth + x];
            output[index] = (byte)color;
            output[index + 1] = (byte)(color >> 8);
            output[index + 2] = (byte)(color >> 16);
            output[index + 3] = (byte)(color >> 24);
        }
        return output;
    }

    const int ArchiveCapacity=16384;
    internal sealed record TexturePlan(int At,int Limit,int Format,bool Lightmap,
        int Width,int Height,int TargetWidth,int TargetHeight,uint Source,int Bytes,
        int SourceMip,uint BitmapFingerprint);
    // Compare every input consumed by Convert/FilterLightmap, within this one
    // immutable map. At/Limit remain separate runtime lookup keys; they do
    // not alter pixels once source mip and output dimensions are selected.
    // An explicit comparer also works with Windows7's record-to-class adapter.
    sealed class PayloadComparer : IEqualityComparer<TexturePlan>
    {
        internal static readonly PayloadComparer Instance=new PayloadComparer();
        public bool Equals(TexturePlan? a,TexturePlan? b) => ReferenceEquals(a,b) ||
            (a!=null && b!=null && a.Format==b.Format && a.Lightmap==b.Lightmap &&
             a.Width==b.Width && a.Height==b.Height && a.TargetWidth==b.TargetWidth &&
             a.TargetHeight==b.TargetHeight && a.Source==b.Source && a.Bytes==b.Bytes);
        public int GetHashCode(TexturePlan p)
        {
            unchecked {
                int hash=p.Format;
                hash=hash*31+(p.Lightmap?1:0);hash=hash*31+p.Width;hash=hash*31+p.Height;
                hash=hash*31+p.TargetWidth;hash=hash*31+p.TargetHeight;
                hash=hash*31+(int)p.Source;return hash*31+p.Bytes;
            }
        }
    }
    static List<TexturePlan> Plans(FileStream input,string map,CancellationToken ct,out byte[] header)
        => Plans(input,map,ct,out header,out _);
    static List<TexturePlan> Plans(FileStream input,string map,CancellationToken ct,out byte[] header,out byte[] tags)
    {
        header=Read(input,0,2048);
        Data.Require(Data.U32(header,0)==0x68656164&&Data.U32(header,4)==5&&Data.U32(header,20)>=36&&Data.U32(header,20)<=0x1600000,"Invalid expanded Xbox map header.");
        tags=Read(input,Data.U32(header,16),checked((int)Data.U32(header,20)));
        int count=checked((int)Data.U32(tags,12));Data.Require(count>0&&count<=65535,"Invalid tag directory.");
        int directory=Pointer(tags,Data.U32(tags,0),checked(count*32));
        var plans=new List<TexturePlan>();
        for(int i=0;i<count;i++) {
            ct.ThrowIfCancellationRequested();int tag=directory+i*32;if(Data.U32(tags,tag)!=0x6269746d)continue;
            int group=Pointer(tags,Data.U32(tags,tag+20),108),np=Pointer(tags,Data.U32(tags,tag+16),1),end=Array.IndexOf(tags,(byte)0,np);
            Data.Require(end>=np&&end-np<256,"Invalid bitmap name.");string name=Encoding.ASCII.GetString(tags,np,end-np);
            int n=checked((int)Data.U32(tags,group+96));if(n==0)continue;
            int baseAt=Pointer(tags,Data.U32(tags,group+100),checked(n*48));
            for(int k=0;k<n;k++) {
                ct.ThrowIfCancellationRequested();
                int at=baseAt+k*48,f=Data.U16(tags,at+12),flags=Data.U16(tags,at+14),w0=Data.U16(tags,at+4),h0=Data.U16(tags,at+6),mips=Data.U16(tags,at+20);
                bool raw=(f<=3||f==6||(f>=8&&f<=11)||f==17);
                if(Data.U32(tags,at)!=0x6269746d||Data.U16(tags,at+10)!=0||Data.U16(tags,at+8)!=1||!(raw||(f>=14&&f<=16))||(flags&0x80)==0||(((flags&4)!=0)!=(f==17))||
                   (raw?((flags&8)==0||(flags&(2|16))!=0):(flags&8)!=0)||w0<4||h0<4||w0>2048||h0>2048||(w0&(w0-1))!=0||(h0&(h0-1))!=0||mips>10)continue;
                bool lightmap=f==6 && Data.U16(tags,group+4)==4;
                uint fingerprint=2166136261;
                for(int word=at;word<at+32;++word)fingerprint=unchecked((fingerprint^tags[word])*16777619);
                // Stable payload order across the modern and Windows7 CLRs;
                // HashSet enumeration order is not part of its contract.
                foreach(int limit in ResolutionPlans(name,Data.U16(tags,group),Data.U16(tags,group+4),lightmap,System.IO.Path.GetFileNameWithoutExtension(map)=="ui").OrderBy(value=>value)) {
                    int w=w0,h=h0,skip=0,level=0,stride=f<=2||f==17?1:f>=10?4:2,block=f==14?8:16;
                    while((w>limit||h>limit)&&w>=16&&h>=16&&level<mips){skip+=raw?w*h*stride:((w+3)/4)*((h+3)/4)*block;w>>=1;h>>=1;level++;}
                    if(w>1024||h>1024)continue;
                    int bytes=raw?w*h*stride:((w+3)/4)*((h+3)/4)*block,tw=Math.Max(8,Math.Min(w,limit)),th=Math.Max(8,Math.Min(h,limit));
                    uint pixels=Data.U32(tags,at+24),size=Data.U32(tags,at+28);Data.Require(pixels>=2048&&skip<=size&&bytes<=size-skip,"Invalid bitmap extent.");
                    Data.Require(plans.Count<ArchiveCapacity,"Too many native textures.");
                    plans.Add(new(at,limit,f,lightmap,w,h,tw,th,checked(pixels+(uint)skip),bytes,level,fingerprint));
                }
            }
        }
        return plans;
    }
    static long ArchiveBytes(List<TexturePlan> plans) => 32+ArchiveCapacity*32L+
        plans.Distinct(PayloadComparer.Instance).Sum(p=>(long)p.TargetWidth*p.TargetHeight*4);
    public static long EstimatedBytes(string map,CancellationToken ct=default)
    {
        using var input=File.OpenRead(map);
        return ArchiveBytes(Plans(input,map,ct,out _));
    }
    public static void Generate(string map,string output,CancellationToken ct=default)
    {
        using var input=File.OpenRead(map);
        var plans=Plans(input,map,ct,out byte[] header);
        // Coverage can grow with the runtime's requested plans. Check their
        // exact uncompressed archive size before creating a replacement.
        string directory=Path.GetDirectoryName(Path.GetFullPath(output))!;
        string volume=OperatingSystem.IsWindows()?Path.GetPathRoot(directory)!:directory;
        long archiveBytes=ArchiveBytes(plans);
        Data.Require(archiveBytes<=uint.MaxValue,"Native texture archive exceeds its format's size limit.");
        long required=archiveBytes+128L*Data.Chunk;
        Data.Require(new DriveInfo(volume).AvailableFreeSpace>=required,
            $"Please free at least {required/1073741824.0:F1} GB for texture preparation. Existing archives remain in place.");
        var records=new List<uint[]>();
        var payloads=new Dictionary<TexturePlan,uint[]>(PayloadComparer.Instance);
        using var target=new FileStream(output,FileMode.Create,FileAccess.ReadWrite,FileShare.None);
        using var writer=new BinaryWriter(target);
        target.Position=32+ArchiveCapacity*32;
        foreach(var plan in plans) {
            ct.ThrowIfCancellationRequested();
            if(!payloads.TryGetValue(plan,out var payload)) {
                byte[] source=Read(input,plan.Source,plan.Bytes);
                byte[] tiled=plan.Lightmap?FilterLightmap(source,plan.Width,plan.Height,plan.TargetWidth,plan.TargetHeight):
                    Convert(source,plan.Format,plan.Width,plan.Height,plan.TargetWidth,plan.TargetHeight);
                payload=new[]{checked((uint)target.Position),(uint)tiled.Length,Hash(tiled)};
                writer.Write(tiled);payloads.Add(plan,payload);
            }
            records.Add(new[]{(uint)plan.At,(uint)plan.Limit,plan.Source,(uint)plan.Bytes,
                (uint)(plan.TargetWidth|plan.TargetHeight<<16),payload[0],payload[1],payload[2]});
        }
        records.Sort((a,b)=>a[0]!=b[0]?a[0].CompareTo(b[0]):a[1].CompareTo(b[1]));
        // Keep the bounded reserved index so payloads never need repacking.
        byte[] indexBytes;
        using(var ms=new MemoryStream()) {using(var iw=new BinaryWriter(ms))foreach(var row in records)foreach(uint v in row)iw.Write(v);indexBytes=ms.ToArray();}
        long sizeFile=target.Length;target.Position=0;writer.Write(Magic);writer.Write(Version);writer.Write(Hash(header));writer.Write((uint)records.Count);writer.Write(Hash(indexBytes));writer.Write(checked((uint)sizeFile));writer.Write(0u);writer.Write(0u);writer.Write(indexBytes);writer.Flush();
    }
}
