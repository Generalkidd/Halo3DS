// New3DS integration additions by Kevin Boggs.
using System.Threading;

namespace HaloSetup;

// Optional Original-model render data. Source maps, skeletons and collision
// geometry are never changed. Retained vertices keep their exact UVs/weights.
// The bounded pack stores vertex remaps and triangle lists, not duplicate assets.
// Kevin Boggs: ModelLods
public static class ModelLods
{
    public const uint Magic = 0x31444c48; // HLD1
    public const int MaximumBytes = 512 * 1024;
    const int EntryBytes = 40;

    // Kevin Boggs: Read
    static byte[] Read(Stream stream, long offset, int length)
    {
        Data.Require(offset >= 0 && length >= 0 && offset <= stream.Length - length, "Incomplete model data.");
        stream.Position = offset; byte[] bytes = new byte[length];
        for (int n = 0; n < length;) { int got = stream.Read(bytes, n, length - n); Data.Require(got > 0, "Incomplete model data."); n += got; }
        return bytes;
    }

    // Kevin Boggs: Pointer
    static int Pointer(byte[] tags, uint pointer, int bytes)
    {
        long offset = (long)pointer - 0x803a6000;
        Data.Require(offset >= 0 && bytes >= 0 && offset <= tags.Length - bytes, "Model pointer is outside its map.");
        return (int)offset;
    }

    // Kevin Boggs: SourceHash
    public static uint SourceHash(byte[] vertices, byte[] indices)
    {
        uint hash = Textures.Hash(vertices);
        foreach (byte value in indices) hash = unchecked((hash ^ value) * 16777619u);
        return hash;
    }

    // Kevin Boggs: Edge
    static ulong Edge(int a, int b) { return ((ulong)(uint)Math.Min(a, b) << 32) | (uint)Math.Max(a, b); }

    // Kevin Boggs: Normal
    static double[] Normal(float[][] positions, int a, int b, int c)
    {
        double x = positions[b][0] - positions[a][0], y = positions[b][1] - positions[a][1], z = positions[b][2] - positions[a][2];
        double u = positions[c][0] - positions[a][0], v = positions[c][1] - positions[a][1], w = positions[c][2] - positions[a][2];
        return new[] { y*w-z*v, z*u-x*w, x*v-y*u };
    }

    // Kevin Boggs: Compatible
    static bool Compatible(byte[] vertices, int a, int b)
    {
        // Never collapse across bone palettes, substantial weight gradients or
        // UV discontinuities. Position-duplicate seams are locked separately.
        a *= 32; b *= 32;
        return vertices[a+28] == vertices[b+28] && vertices[a+29] == vertices[b+29] &&
            Math.Abs((int)Data.U16(vertices,a+30) - Data.U16(vertices,b+30)) <= 2048 &&
            Math.Abs((short)Data.U16(vertices,a+24) - (short)Data.U16(vertices,b+24)) <= 4096 &&
            Math.Abs((short)Data.U16(vertices,a+26) - (short)Data.U16(vertices,b+26)) <= 4096;
    }

    // Kevin Boggs: Collapse
    static bool Collapse(int a, int b, float[][] positions, List<int[]> triangles,
        HashSet<int>[] faces, bool[] locked, double maxDistance)
    {
        if (locked[a] || locked[b] || faces[a].Count == 0 || faces[b].Count == 0) return false;
        double distance=0; for(int k=0;k<3;k++) { double d=positions[a][k]-positions[b][k];distance+=d*d; }
        if(distance > maxDistance) return false;
        var neighborsA=new HashSet<int>();var neighborsB=new HashSet<int>();int sharedFaces=0;
        foreach(int f in faces[a])foreach(int v in triangles[f])if(v!=a)neighborsA.Add(v);
        foreach(int f in faces[b]) { foreach(int v in triangles[f])if(v!=b)neighborsB.Add(v);if(faces[a].Contains(f))sharedFaces++; }
        neighborsA.IntersectWith(neighborsB);
        // Link condition: preserve manifold topology and avoid closing holes.
        if(sharedFaces!=2 || neighborsA.Count!=2)return false;
        foreach(int f in faces[b]) {
            int[] t=triangles[f];if(t[0]==a || t[1]==a || t[2]==a)continue;
            double[] before=Normal(positions,t[0],t[1],t[2]);
            double[] after=Normal(positions,t[0]==b?a:t[0],t[1]==b?a:t[1],t[2]==b?a:t[2]);
            double dot=0,aa=0,bb=0;for(int k=0;k<3;k++){dot+=before[k]*after[k];aa+=before[k]*before[k];bb+=after[k]*after[k];}
            if(bb<1e-20 || dot<=0 || dot*dot<.64*aa*bb)return false;
        }
        foreach(int f in faces[b].ToArray()) {
            int[] t=triangles[f];foreach(int v in t)faces[v].Remove(f);
            for(int k=0;k<3;k++)if(t[k]==b)t[k]=a;
            if(t[0]==t[1] || t[1]==t[2] || t[0]==t[2])t[0]=-1;
            else foreach(int v in t)faces[v].Add(f);
        }
        return true;
    }

    // Kevin Boggs: Simplify
    public static ushort[][] Simplify(byte[] vertices, ushort[] list, CancellationToken ct=default)
    {
        int count=vertices.Length/32;Data.Require(vertices.Length%32==0 && count>0 && count<=8192 && list.Length%3==0,"Invalid model mesh.");
        var positions=new float[count][];var locked=new bool[count];var faces=new HashSet<int>[count];
        var duplicates=new Dictionary<string,int>();var triangles=new List<int[]>();var edges=new Dictionary<ulong,int>();
        float[] lo={float.MaxValue,float.MaxValue,float.MaxValue},hi={float.MinValue,float.MinValue,float.MinValue};
        for(int i=0;i<count;i++) {
            faces[i]=new HashSet<int>();positions[i]=new float[3];
            for(int k=0;k<3;k++) {float x=BitConverter.ToSingle(vertices,i*32+k*4);Data.Require(!float.IsNaN(x)&&!float.IsInfinity(x),"Invalid model position.");positions[i][k]=x;lo[k]=Math.Min(lo[k],x);hi[k]=Math.Max(hi[k],x);}
            string key=positions[i][0].ToString("R",System.Globalization.CultureInfo.InvariantCulture)+":"+positions[i][1].ToString("R",System.Globalization.CultureInfo.InvariantCulture)+":"+positions[i][2].ToString("R",System.Globalization.CultureInfo.InvariantCulture);
            int previous;if(duplicates.TryGetValue(key,out previous)){locked[i]=locked[previous]=true;}else duplicates.Add(key,i);
        }
        for(int i=0;i<list.Length;i+=3) {
            int a=list[i],b=list[i+1],c=list[i+2];Data.Require(a<count&&b<count&&c<count,"Invalid model triangle.");
            if(a==b||b==c||a==c)continue;
            int f=triangles.Count;triangles.Add(new[]{a,b,c});faces[a].Add(f);faces[b].Add(f);faces[c].Add(f);
            for(int k=0;k<3;k++){ulong e=Edge(list[i+k],list[i+(k+1)%3]);int n;edges.TryGetValue(e,out n);edges[e]=n+1;}
        }
        foreach(var edge in edges)if(edge.Value!=2){locked[(int)(edge.Key>>32)]=true;locked[(int)(edge.Key&0xffffffff)]=true;}
        double diagonal=0;for(int k=0;k<3;k++)diagonal+=(hi[k]-lo[k])*(hi[k]-lo[k]);
        double maxDistance=diagonal*.0025;int remaining=triangles.Count,target=Math.Max(12,remaining*55/100);
        // Rebuild candidates after each pass; endpoint collapses retain all
        // authored attributes and avoid accumulating interpolation error.
        for(int pass=0;pass<3 && remaining>target;pass++) {
            ct.ThrowIfCancellationRequested();var candidates=new List<KeyValuePair<double,ulong>>();var known=new HashSet<ulong>();
            foreach(int[] t in triangles)if(t[0]>=0)for(int k=0;k<3;k++) {
                int a=t[k],b=t[(k+1)%3];ulong edge=Edge(a,b);
                if(locked[a]||locked[b]||!known.Add(edge)||!Compatible(vertices,a,b))continue;
                double d=0;for(int n=0;n<3;n++){double x=positions[a][n]-positions[b][n];d+=x*x;}
                candidates.Add(new KeyValuePair<double,ulong>(d,edge));
            }
            candidates.Sort((a,b)=>a.Key!=b.Key?a.Key.CompareTo(b.Key):a.Value.CompareTo(b.Value));
            foreach(var edge in candidates) {
                ct.ThrowIfCancellationRequested();if(remaining<=target)break;
                int a=(int)(edge.Value>>32),b=(int)(edge.Value&0xffffffff);
                if(Collapse(a,b,positions,triangles,faces,locked,maxDistance))remaining-=2;
            }
        }
        var remap=new List<ushort>();var output=new List<ushort>();int[] compact=new int[count];for(int i=0;i<count;i++)compact[i]=-1;
        foreach(int[] t in triangles)if(t[0]>=0)foreach(int v in t) {
            if(compact[v]<0){compact[v]=remap.Count;remap.Add((ushort)v);}output.Add((ushort)compact[v]);
        }
        return new[]{remap.ToArray(),output.ToArray()};
    }

    // Kevin Boggs: Validate
    static bool Validate(byte[] pack,uint headerHash)
    {
        if(pack.Length<32 || pack.Length>MaximumBytes || Data.U32(pack,0)!=Magic || Data.U32(pack,4)!=1 || Data.U32(pack,8)!=headerHash || Data.U32(pack,20)!=pack.Length || Data.U32(pack,24)!=0 || Data.U32(pack,28)!=0)return false;
        uint count=Data.U32(pack,12);if(count>4096 || 32+count*EntryBytes>pack.Length)return false;
        byte[] index=new byte[count*EntryBytes];Array.Copy(pack,32,index,0,index.Length);if(Textures.Hash(index)!=Data.U32(pack,16))return false;
        uint previousVertex=0,previousStrip=0;
        for(int i=0;i<count;i++) {
            int e=32+i*EntryBytes;uint vertex=Data.U32(pack,e),strip=Data.U32(pack,e+4),sourceCount=Data.U32(pack,e+8),sourceTriangles=Data.U32(pack,e+12),nv=Data.U32(pack,e+16),ni=Data.U32(pack,e+20),offset=Data.U32(pack,e+24);
            if(sourceCount==0||sourceCount>8192||sourceTriangles>65535||nv==0||nv>sourceCount||ni<3||ni%3!=0||ni>sourceTriangles*3||offset<32+count*EntryBytes||offset>pack.Length||2*(nv+ni)>pack.Length-offset||Data.U32(pack,e+36)!=1)return false;
            if(i>0 && (vertex<previousVertex || (vertex==previousVertex && strip<=previousStrip)))return false;
            previousVertex=vertex;previousStrip=strip;byte[] payload=new byte[2*(nv+ni)];Array.Copy(pack,(int)offset,payload,0,payload.Length);
            if(Textures.Hash(payload)!=Data.U32(pack,e+32))return false;
            for(int k=0;k<nv;k++)if(Data.U16(payload,k*2)>=sourceCount)return false;
            for(int k=0;k<ni;k++)if(Data.U16(payload,((int)nv+k)*2)>=nv)return false;
        }
        return true;
    }

    // Kevin Boggs: Current
    public static bool Current(string map,string archive,CancellationToken ct=default)
    {
        if(!File.Exists(map)||!File.Exists(archive))return false;
        try {ct.ThrowIfCancellationRequested();using(var input=File.OpenRead(map))using(var pack=File.OpenRead(archive)) {
            if(pack.Length>MaximumBytes)return false;return Validate(Read(pack,0,(int)pack.Length),Textures.Hash(Read(input,0,2048)));
        }}catch(IOException){return false;}catch(SetupException){return false;}catch(OverflowException){return false;}
    }

    // Kevin Boggs: Generate
    public static void Generate(string map,string output,CancellationToken ct=default)
    {
        using(var input=File.OpenRead(map)) {
            byte[] header=Read(input,0,2048);Data.Require(Data.U32(header,4)==5,"Original model packs require expanded Xbox maps.");
            byte[] tags=Read(input,Data.U32(header,16),checked((int)Data.U32(header,20)));
            int count=checked((int)Data.U32(tags,12)),directory=Pointer(tags,Data.U32(tags,0),checked(count*32));
            var entries=new List<uint[]>();var payloads=new Dictionary<uint[],byte[]>();var keys=new HashSet<ulong>();
            for(int i=0;i<count;i++) {
                ct.ThrowIfCancellationRequested();int tag=directory+i*32;if(Data.U32(tags,tag)!=0x6d6f6465)continue;
                int model=Pointer(tags,Data.U32(tags,tag+20),220),ng=checked((int)Data.U32(tags,model+208));
                int geometries=ng==0?0:Pointer(tags,Data.U32(tags,model+212),checked(ng*48));
                for(int g=0;g<ng;g++) {
                    int geometry=geometries+g*48,np=checked((int)Data.U32(tags,geometry+36));
                    int parts=np==0?0:Pointer(tags,Data.U32(tags,geometry+40),checked(np*104));
                    for(int p=0;p<np;p++) {
                        int part=parts+p*104;uint nv=Data.U32(tags,part+88),nt=Data.U32(tags,part+72);
                        if(Data.U16(tags,part+84)!=5 || Data.U16(tags,part+68)!=1 || nv<32 || nv>8192 || nt<32 || nt>65535)continue;
                        int descriptor=Pointer(tags,Data.U32(tags,part+100),12);
                        int v=Pointer(tags,Data.U32(tags,descriptor+4),checked((int)nv*32));
                        int t=Pointer(tags,Data.U32(tags,part+76),checked(((int)nt+2)*2));
                        if(!keys.Add(((ulong)(uint)v<<32)|(uint)t))continue;
                        byte[] vertices=new byte[nv*32],strip=new byte[(nt+2)*2];Array.Copy(tags,v,vertices,0,vertices.Length);Array.Copy(tags,t,strip,0,strip.Length);
                        var list=new List<ushort>();for(int k=0;k<nt;k++) {
                            ushort a=Data.U16(strip,k*2),b=Data.U16(strip,(k+1)*2),c=Data.U16(strip,(k+2)*2);
                            Data.Require(a<nv&&b<nv&&c<nv,"Invalid model index.");if(a==b||b==c||a==c)continue;
                            list.Add((k&1)==0?a:b);list.Add((k&1)==0?b:a);list.Add(c);
                        }
                        ushort[][] simplified=Simplify(vertices,list.ToArray(),ct);
                        if(simplified[0].Length>=nv*9/10 || simplified[1].Length>=list.Count*9/10)continue;
                        byte[] payload;using(var memory=new MemoryStream())using(var writer=new BinaryWriter(memory)) {
                            foreach(var set in simplified)foreach(ushort value in set)writer.Write(value);payload=memory.ToArray();
                        }
                        uint[] entry={(uint)v,(uint)t,nv,nt,(uint)simplified[0].Length,(uint)simplified[1].Length,0,SourceHash(vertices,strip),Textures.Hash(payload),1};
                        entries.Add(entry);payloads.Add(entry,payload);
                    }
                }
            }
            // Prefer the most geometry saved per resident byte if a map exceeds
            // the Original-model memory budget. Remaining meshes use stock data.
            entries.Sort((a,b)=>((double)(b[2]-b[4])/(payloads[b].Length+EntryBytes)).CompareTo((double)(a[2]-a[4])/(payloads[a].Length+EntryBytes)));
            int size=32;var selected=new List<uint[]>();foreach(var entry in entries)if(size+EntryBytes+payloads[entry].Length<=MaximumBytes){selected.Add(entry);size+=EntryBytes+payloads[entry].Length;}
            selected.Sort((a,b)=>a[0]!=b[0]?a[0].CompareTo(b[0]):a[1].CompareTo(b[1]));
            using(var target=new MemoryStream())using(var writer=new BinaryWriter(target)) {
                target.SetLength(32+selected.Count*EntryBytes);target.Position=target.Length;
                foreach(var entry in selected){entry[6]=(uint)target.Position;writer.Write(payloads[entry]);}
                target.Position=32;foreach(var entry in selected)foreach(uint word in entry)writer.Write(word);
                byte[] pack=target.ToArray(),index=new byte[selected.Count*EntryBytes];Array.Copy(pack,32,index,0,index.Length);
                target.Position=0;foreach(uint word in new uint[]{Magic,1,Textures.Hash(header),(uint)selected.Count,Textures.Hash(index),(uint)pack.Length,0,0})writer.Write(word);
                pack=target.ToArray();Data.Require(Validate(pack,Textures.Hash(header)),"Model pack failed verification.");
                using(var file=new FileStream(output,FileMode.Create,FileAccess.Write,FileShare.None))file.Write(pack,0,pack.Length);
            }
        }
    }
}
