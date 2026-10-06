using System.Buffers.Binary;

namespace HaloSetup;

internal sealed record Field(int Offset, string Name, string Type, string? Struct);
internal sealed record Layout(int Size, List<Field> Fields);
internal sealed record Schemas(Dictionary<string, Layout> Types, Dictionary<uint, string> Roots);
internal sealed record Bsp(uint Address, uint FileOffset, uint Bytes);

/// <summary>Native C# translation of the port's audited relocate_cache.py.
/// Schemas describe file formats, not game content. Expected words are read
/// from the user's map; game-specific relocation records are never bundled.</summary>
public static class Relocation
{
    static readonly Schemas schemas = Data.Resource<Schemas>("schemas.json");
    static readonly uint[] crcTable = Enumerable.Range(0, 256).Select(i =>
    {
        uint crc = (uint)i;
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320u : 0u);
        return crc;
    }).ToArray();
    static uint Crc(byte[] data)
    {
        uint crc = 0xffffffff;
        foreach (byte b in data) crc = crcTable[(crc ^ b) & 255] ^ (crc >> 8);
        return crc ^ 0xffffffff;
    }
    sealed class Region(byte[] data, uint address, CancellationToken ct, Region? global = null)
    {
        public byte[] Bytes = data;
        public uint Base = address;
        public SortedDictionary<int, uint> Pointers = [];
        public List<Bsp> Deferred = [];
        readonly HashSet<(string, int)> visited = [], active = [];
        public uint U32(int offset)
        { Data.Require(offset >= 0 && offset <= Bytes.Length - 4, "Map word outside its region."); return Data.U32(Bytes, offset); }
        public int Resolve(uint pointer, long size)
        {
            long offset = (long)pointer - Base;
            Data.Require(offset >= 0 && size >= 0 && offset + size <= Bytes.Length, "Map pointer outside its region.");
            return (int)offset;
        }
        public int? Pointer(int offset, long size = 1, bool nullable = true)
        {
            uint value = U32(offset);
            if (value == 0 && nullable) return null;
            int target = Resolve(value, size);
            Data.Require((offset & 3) == 0, "Unaligned map pointer.");
            Pointers[offset] = (uint)target;
            return target;
        }
        public void Geometry(int countOffset, int pointerOffset)
        {
            uint count = U32(countOffset);
            Data.Require(count < 65536, "Too many geometry buffers.");
            int? descriptors = Pointer(pointerOffset, count * 12L, count == 0);
            if (descriptors is int start)
                for (int i = 0; i < count; i++) Pointer(start + i * 12 + 4, 1, false);
        }
        public void Walk(string name, int offset, int depth = 0)
        {
            ct.ThrowIfCancellationRequested();
            var key = (name, offset);
            Data.Require(depth <= 64 && !active.Contains(key), "Cyclic map structure.");
            if (visited.Contains(key)) return;
            var layout = schemas.Types[name];
            Resolve(checked(Base + (uint)offset), layout.Size);
            active.Add(key);
            try
            {
                foreach (var field in layout.Fields)
                {
                    int pos = checked(offset + field.Offset);
                    switch (field.Type)
                    {
                        case "TagReflexive":
                        {
                            uint count = U32(pos);
                            Data.Require(U32(pos + 8) == 0 && count <= 1000000, "Invalid cached map block.");
                            int size = schemas.Types[field.Struct!].Size;
                            int? target = Pointer(pos + 4, count * (long)size, count == 0);
                            if (target is int start)
                                for (int i = 0; i < count; i++) Walk(field.Struct!, checked(start + i * size), depth + 1);
                            break;
                        }
                        case "TagDependency":
                        {
                            uint length = U32(pos + 8), value = U32(pos + 4);
                            byte[] text = Bytes;
                            int? target;
                            if (value != 0 && global != null && value < Base)
                            {
                                target = global.Resolve(value, (long)length + 1);
                                Pointers[pos + 4] = (uint)target | 0x80000000;
                                text = global.Bytes;
                            }
                            else target = Pointer(pos + 4, (long)length + 1);
                            if (target is int start)
                            {
                                int end = Array.IndexOf(text, (byte)0, start, Math.Min(256, text.Length - start));
                                Data.Require(end >= 0 && (length == 0 || end - start == length), "Invalid dependency name.");
                            }
                            break;
                        }
                        case "TagDataOffset":
                        {
                            uint size = U32(pos), value = U32(pos + 12);
                            Data.Require(U32(pos + 16) == 0, "Invalid cached data schema.");
                            Pointer(pos + 12, size);
                            if (name == "Scenario" && field.Name == "script syntax data" && size != 0)
                            {
                                int target = Resolve(value, size);
                                int maximum = unchecked((short)Data.U16(Bytes, target + 32));
                                int stride = unchecked((short)Data.U16(Bytes, target + 34));
                                Data.Require(maximum >= 0 && stride == 20 && size == 56 + maximum * stride && U32(target + 40) == 0x64407440,
                                    "Invalid script datum array.");
                                Pointers[target + 52] = (uint)(target + 56);
                            }
                            break;
                        }
                        case "Pointer":
                            if (name == "BitmapData") Data.Require(U32(pos) == 0xffffffff, "Bitmap is already cached.");
                            else if (name != "ModelGeometryPart") Pointer(pos);
                            break;
                        default:
                            if (schemas.Types.ContainsKey(field.Type)) Walk(field.Type, pos, depth + 1);
                            break;
                    }
                }
                if (name == "BitmapData")
                { Pointers[offset + 40] = uint.MaxValue; Pointers[offset + 44] = uint.MaxValue; }
                else if (name == "ModelGeometryPart")
                {
                    uint triangles = U32(offset + 72), type = U32(offset + 68) & 0xffff;
                    Data.Require(type is 0 or 1, "Unknown triangle buffer format.");
                    Pointer(offset + 76, type == 0 ? triangles * 6L : (triangles + 2L) * 2, triangles == 0);
                    Pointer(offset + 80, 12, triangles == 0);
                    uint vertices = U32(offset + 88), vertexType = U32(offset + 84) & 0xffff;
                    Data.Require(vertexType is 4 or 5, "Unknown model vertex format.");
                    int? descriptor = Pointer(offset + 100, 12, vertices == 0);
                    if (descriptor is int d)
                        Pointers[offset + 96] = (uint)Resolve(U32(d + 4), vertices * (vertexType == 4 ? 68L : 32L));
                }
                else if (name == "ScenarioBSP") Deferred.Add(new(U32(offset + 8), U32(offset), U32(offset + 4)));
                else if (name == "ScenarioStructureBSPMaterial")
                {
                    foreach (var (start, type, stride) in new[] { (176, 1u, 32L), (196, 3u, 8L) })
                    {
                        uint count = U32(offset + start + 4);
                        if (count == 0) { Pointers[offset + start + 12] = uint.MaxValue; continue; }
                        Data.Require((U32(offset + start) & 0xffff) == type, "Unexpected Xbox BSP vertex format.");
                        int descriptor = Resolve(U32(offset + start + 16), 12);
                        Pointers[offset + start + 12] = (uint)Resolve(U32(descriptor + 4), count * stride);
                    }
                }
                visited.Add(key);
            }
            finally { active.Remove(key); }
        }
        public void Save(string path, byte[] header)
        {
            byte[] records = new byte[Pointers.Count * 12];
            int pos = 0;
            foreach (var (offset, target) in Pointers)
            {
                BinaryPrimitives.WriteUInt32LittleEndian(records.AsSpan(pos), (uint)offset);
                BinaryPrimitives.WriteUInt32LittleEndian(records.AsSpan(pos + 4), U32(offset));
                BinaryPrimitives.WriteUInt32LittleEndian(records.AsSpan(pos + 8), target);
                pos += 12;
            }
            using var stream = new FileStream(path, FileMode.CreateNew);
            using var writer = new BinaryWriter(stream);
            foreach (uint value in new uint[] { 0x314c524e, 1, (uint)Bytes.Length, Base, Crc(Bytes), (uint)Pointers.Count, Crc(records), Crc(header) })
                writer.Write(value);
            writer.Write(records); writer.Flush(); stream.Flush(true);
        }
    }
    public static void Generate(string map, string output, string name, CancellationToken ct = default)
    {
        using var file = File.OpenRead(map);
        byte[] header = new byte[2048]; file.ReadExactly(header);
        uint length = Data.U32(header, 8), offset = Data.U32(header, 16), size = Data.U32(header, 20);
        Data.Require(offset >= 2048 && size <= 0x1600000 && (long)offset + size <= length && length == file.Length, "Invalid map region.");
        file.Position = offset;
        byte[] data = new byte[size]; file.ReadExactly(data);
        var region = new Region(data, 0x803a6000, ct);
        uint count = region.U32(12);
        Data.Require(region.U32(32) == 0x74616773 && count is > 0 and <= 65535, "Invalid tag directory.");
        int table = region.Pointer(0, count * 32L, false)!.Value;
        region.Geometry(16, 20); region.Geometry(24, 28);
        for (int i = 0; i < count; i++)
        {
            ct.ThrowIfCancellationRequested();
            int entry = table + i * 32;
            uint group = region.U32(entry);
            Data.Require((region.U32(entry + 12) & 0xffff) == i, "Invalid tag handle.");
            int text = region.Pointer(entry + 16, 1, false)!.Value;
            Data.Require(Array.IndexOf(data, (byte)0, text, Math.Min(256, data.Length - text)) >= 0, "Invalid tag name.");
            if (group == 0x73627370) { Data.Require(region.U32(entry + 20) == 0, "Invalid streamed BSP."); continue; }
            Data.Require(schemas.Roots.TryGetValue(group, out string? root), "Unknown map tag type.");
            int body = region.Pointer(entry + 20, schemas.Types[root!].Size, false)!.Value;
            region.Walk(root!, body);
        }
        for (int i = 0; i < region.Deferred.Count; i++)
        {
            ct.ThrowIfCancellationRequested();
            var b = region.Deferred[i];
            Data.Require(b.FileOffset >= 2048 && (long)b.FileOffset + b.Bytes <= length && b.Bytes is >= 24 and <= 0x1600000,
                "Invalid BSP region.");
            file.Position = b.FileOffset;
            byte[] bytes = new byte[b.Bytes]; file.ReadExactly(bytes);
            var bsp = new Region(bytes, b.Address, ct, region);
            Data.Require(bsp.U32(20) == 0x73627370, "Invalid BSP signature.");
            int body = bsp.Pointer(0, schemas.Types["ScenarioStructureBSP"].Size, false)!.Value;
            bsp.Geometry(4, 8); bsp.Geometry(12, 16); bsp.Walk("ScenarioStructureBSP", body);
            bsp.Save(Path.Combine(output, $"{name}-bsp{i}.nrl"), header);
        }
        region.Save(Path.Combine(output, name + ".nrl"), header);
    }
}
