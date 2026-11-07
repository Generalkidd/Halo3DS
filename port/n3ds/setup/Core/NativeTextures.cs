using System.Security.Cryptography;

namespace HaloSetup;

// Opt-in representation infrastructure only. Every payload is the same tiled
// RGBA8 image produced by the legacy archive; no smaller formats or new mips.
public static partial class Textures
{
    const uint CandidateMagic=0x34585448, CandidateSchema=1, CandidatePolicy=1;
    const int CandidateHeaderBytes=128, CandidateEntryBytes=96;
    const int CandidatePayloadStart=CandidateHeaderBytes+ArchiveCapacity*CandidateEntryBytes;
    static readonly uint[] candidateCrcTable=Enumerable.Range(0,256).Select(i=> {
        uint value=(uint)i;
        for(int bit=0;bit<8;++bit)value=(value>>1)^((value&1)!=0?0xedb88320u:0u);
        return value;
    }).ToArray();
    static uint CandidateCrc(byte[] data)
    {
        // Original, unrelocated tag bytes; same standard CRC32 as Relocation.
        uint crc=0xffffffff;
        foreach(byte value in data)crc=candidateCrcTable[(crc^value)&255]^(crc>>8);
        return crc^0xffffffff;
    }
    static void CandidateWord(byte[] data,int offset,uint value)
    {
        data[offset]=(byte)value;data[offset+1]=(byte)(value>>8);
        data[offset+2]=(byte)(value>>16);data[offset+3]=(byte)(value>>24);
    }
    static byte[] CandidateMapHash(FileStream input,string map,CancellationToken ct)
    {
        string name=System.IO.Path.GetFileNameWithoutExtension(map);
        Data.Require(Data.Catalog.TryGetValue(name,out MapInfo? expected),"Candidate textures require a catalogued retail map.");
        Data.Require(input.Length==expected!.Bytes && input.Length<=int.MaxValue,"Candidate textures require the exact expanded retail map.");
        input.Position=0;
        using var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer=new byte[Data.Chunk];int got;
        while((got=input.Read(buffer,0,buffer.Length))>0) {
            ct.ThrowIfCancellationRequested();hash.AppendData(buffer,0,got);
        }
        ct.ThrowIfCancellationRequested();
        byte[] identity=hash.GetHashAndReset();
        string actual=BitConverter.ToString(identity).Replace("-","").ToLowerInvariant();
        Data.Require(actual==expected.Sha256,"Candidate textures require a verified, unchanged expanded retail map.");
        return identity;
    }
    // Keep representation/provenance inputs in the sharing key. Runtime lookup
    // keys At/Limit can still share a payload when every conversion input agrees.
    sealed class CandidatePayloadComparer : IEqualityComparer<TexturePlan>
    {
        internal static readonly CandidatePayloadComparer Instance=new CandidatePayloadComparer();
        public bool Equals(TexturePlan? a,TexturePlan? b) => ReferenceEquals(a,b) ||
            (a!=null && b!=null && PayloadComparer.Instance.Equals(a,b) &&
             a.SourceMip==b.SourceMip && a.BitmapFingerprint==b.BitmapFingerprint);
        public int GetHashCode(TexturePlan p)
        { unchecked {return (PayloadComparer.Instance.GetHashCode(p)*31+p.SourceMip)*31+(int)p.BitmapFingerprint;} }
    }
    static long CandidateArchiveBytes(List<TexturePlan> plans) => CandidatePayloadStart+
        plans.Distinct(CandidatePayloadComparer.Instance).Sum(p=>(long)p.TargetWidth*p.TargetHeight*4);
    static void CandidateSortPlans(List<TexturePlan> plans)
    {
        plans.Sort((a,b)=>a.At!=b.At?a.At.CompareTo(b.At):a.Limit.CompareTo(b.Limit));
        for(int i=1;i<plans.Count;++i)
            Data.Require(plans[i-1].At!=plans[i].At || plans[i-1].Limit!=plans[i].Limit,
                "Candidate texture lookup keys must be unique.");
    }
    static uint[] CandidateEntry(TexturePlan plan,uint offset,uint payloadBytes,uint payloadHash)
    {
        uint resident=checked((uint)(plan.TargetWidth*plan.TargetHeight*4));
        uint scratch=checked((uint)plan.Bytes+3u)&~3u;
        if(plan.Lightmap)scratch=checked(scratch+resident/2);
        return new[]{(uint)plan.At,(uint)plan.Limit,plan.Source,(uint)plan.Bytes,
            (uint)plan.TargetWidth,(uint)plan.TargetHeight,1u,1u,1u,(uint)plan.SourceMip,
            CandidatePolicy,3u,resident,scratch,offset,payloadBytes,payloadHash,
            (uint)plan.Format,(uint)plan.Width,(uint)plan.Height,plan.BitmapFingerprint,
            plan.Lightmap?1u:0u,0u,0u};
    }
    public static long CandidateEstimatedBytes(string map,CancellationToken ct=default)
    {
        using var input=File.OpenRead(map);
        return CandidateArchiveBytes(Plans(input,map,ct,out _));
    }
    public static bool CandidateCurrent(string map,string archive,CancellationToken ct=default)
    {
        if(!File.Exists(archive))return false;
        try {
            using(var input=File.OpenRead(map)) using(var packed=File.OpenRead(archive)) {
                byte[] h=Read(packed,0,CandidateHeaderBytes);uint count=Data.U32(h,40);
                if(Data.U32(h,0)!=CandidateMagic || Data.U32(h,4)!=CandidateSchema ||
                   Data.U32(h,8)!=CandidateHeaderBytes || Data.U32(h,12)!=CandidateEntryBytes ||
                   Data.U32(h,16)!=CandidatePolicy || Data.U32(h,20)!=0x01020304 ||
                   count==0 || count>ArchiveCapacity || Data.U32(h,44)!=CandidateHeaderBytes ||
                   Data.U32(h,48)!=count*CandidateEntryBytes || Data.U32(h,52)!=CandidatePayloadStart ||
                   packed.Length>int.MaxValue || Data.U32(h,56)!=packed.Length)return false;
                for(int i=100;i<CandidateHeaderBytes;++i)if(h[i]!=0)return false;
                uint headerHash=Data.U32(h,96);CandidateWord(h,96,0);
                if(Hash(h)!=headerHash)return false;
                // A plausible header or copied digest does not authenticate the
                // installed map. Hash the same open input used by the selector.
                byte[] identity=CandidateMapHash(input,map,ct);
                for(int i=0;i<identity.Length;++i)if(h[64+i]!=identity[i])return false;
                var plans=Plans(input,map,ct,out byte[] mapHeader,out byte[] tags);
                if(Data.U32(h,24)!=input.Length || Data.U32(h,28)!=Hash(mapHeader) ||
                   Data.U32(h,32)!=tags.Length || Data.U32(h,36)!=CandidateCrc(tags) ||
                   count!=plans.Count || packed.Length!=CandidateArchiveBytes(plans))return false;
                CandidateSortPlans(plans);
                byte[] directory=Read(packed,CandidateHeaderBytes,checked((int)count*CandidateEntryBytes));
                if(Hash(directory)!=Data.U32(h,60))return false;
                var verified=new Dictionary<uint,long>();
                for(int i=0;i<plans.Count;++i) {
                    ct.ThrowIfCancellationRequested();int at=i*CandidateEntryBytes;
                    uint offset=Data.U32(directory,at+56),bytes=Data.U32(directory,at+60),hash=Data.U32(directory,at+64);
                    var expected=CandidateEntry(plans[i],offset,bytes,hash);
                    for(int word=0;word<expected.Length;++word)
                        if(Data.U32(directory,at+word*4)!=expected[word])return false;
                    if(bytes!=expected[12] || offset<CandidatePayloadStart || (offset&3)!=0 ||
                       offset>packed.Length-bytes)return false;
                    long signature=((long)bytes<<32)|hash;
                    if(verified.TryGetValue(offset,out long previous)) {if(previous!=signature)return false;}
                    else {
                        if(Hash(Read(packed,offset,checked((int)bytes)))!=hash)return false;
                        verified.Add(offset,signature);
                    }
                }
                // Aliases name the same whole span. Reject partial overlaps,
                // unexplained padding and trailing data as noncanonical output.
                long next=CandidatePayloadStart;
                foreach(var span in verified.OrderBy(pair=>pair.Key)) {
                    if(span.Key!=next)return false;
                    next+=span.Value>>32;
                }
                return next==packed.Length;
            }
        } catch(SetupException) {return false;} catch(IOException) {return false;} catch(OverflowException) {return false;}
    }
    public static void GenerateCandidate(string map,string output,CancellationToken ct=default)
    {
        using var input=File.OpenRead(map);
        byte[] identity=CandidateMapHash(input,map,ct);
        var plans=Plans(input,map,ct,out byte[] mapHeader,out byte[] tags);
        Data.Require(plans.Count>0,"No compatible candidate texture plans were found.");
        CandidateSortPlans(plans);
        uint tagCrc=CandidateCrc(tags);
        long archiveBytes=CandidateArchiveBytes(plans);
        Data.Require(archiveBytes<=int.MaxValue,"Candidate texture archive exceeds the console file-size limit.");
        string directory=Path.GetDirectoryName(Path.GetFullPath(output))!;
        string volume=OperatingSystem.IsWindows()?Path.GetPathRoot(directory)!:directory;
        long required=archiveBytes+128L*Data.Chunk;
        Data.Require(new DriveInfo(volume).AvailableFreeSpace>=required,
            $"Please free at least {required/1073741824.0:F1} GB for candidate texture preparation. Existing archives remain in place.");
        var records=new List<uint[]>();
        var payloads=new Dictionary<TexturePlan,uint[]>(CandidatePayloadComparer.Instance);
        using var target=new FileStream(output,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.None);
        using var writer=new BinaryWriter(target);
        target.Position=CandidatePayloadStart;
        foreach(var plan in plans) {
            ct.ThrowIfCancellationRequested();
            if(!payloads.TryGetValue(plan,out var payload)) {
                byte[] source=Read(input,plan.Source,plan.Bytes);
                byte[] tiled=plan.Lightmap?FilterLightmap(source,plan.Width,plan.Height,plan.TargetWidth,plan.TargetHeight):
                    Convert(source,plan.Format,plan.Width,plan.Height,plan.TargetWidth,plan.TargetHeight);
                payload=new[]{checked((uint)target.Position),(uint)tiled.Length,Hash(tiled)};
                writer.Write(tiled);payloads.Add(plan,payload);
            }
            records.Add(CandidateEntry(plan,payload[0],payload[1],payload[2]));
        }
        records.Sort((a,b)=>a[0]!=b[0]?a[0].CompareTo(b[0]):a[1].CompareTo(b[1]));
        byte[] index;
        using(var ms=new MemoryStream()) {
            using(var iw=new BinaryWriter(ms))foreach(var record in records)foreach(uint word in record)iw.Write(word);
            index=ms.ToArray();
        }
        Data.Require(target.Length==archiveBytes,"Candidate texture payload size did not match its plan.");
        byte[] header=new byte[CandidateHeaderBytes];
        uint[] words={CandidateMagic,CandidateSchema,CandidateHeaderBytes,CandidateEntryBytes,CandidatePolicy,0x01020304,
            checked((uint)input.Length),Hash(mapHeader),(uint)tags.Length,tagCrc,(uint)records.Count,CandidateHeaderBytes,
            (uint)index.Length,CandidatePayloadStart,checked((uint)target.Length),Hash(index)};
        for(int i=0;i<words.Length;++i)CandidateWord(header,i*4,words[i]);
        Array.Copy(identity,0,header,64,identity.Length);CandidateWord(header,96,Hash(header));
        target.Position=0;writer.Write(header);writer.Write(index);writer.Flush();target.Flush(true);
    }
}
