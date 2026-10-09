"""Read-only inventory of PC retail campaign object placements and tag assets.

The offsets are the retail v7 scenario layout documented in
port/n3ds/pc/cache_file_formats.c. This does not edit a map.
"""

import argparse
import json
import mmap
import struct
import zlib
from pathlib import Path


BASE = 0x40440000
CAMPAIGN = ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40")


def u32(data, pos):
    return struct.unpack_from("<I", data, pos)[0]


def bounded(data, start, size, low, high):
    if start < low or size < 0 or start + size > high:
        raise ValueError(f"out-of-range data at {start:#x} size {size:#x}")
    return start


def survey(path):
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
        data = mapped
        if len(data) < 2048 or data[:4] != b"daeh" or data[2044:2048] != b"toof" or u32(data, 4) not in (5, 7):
            raise ValueError(f"{path}: expected retail v5/v7 map")
        version = u32(data, 4)
        address_base = BASE if version == 7 else 0x803a6000
        if version == 5 and len(data) != u32(data, 8):
            expanded_size = u32(data, 8)
            if expanded_size > 512 * 1024 * 1024:
                raise ValueError("Xbox expanded map size exceeds limit")
            decoder = zlib.decompressobj()
            payload = decoder.decompress(data[2048:], expanded_size - 2048 + 1)
            payload += decoder.flush()
            if not decoder.eof or len(payload) + 2048 != expanded_size:
                raise ValueError("Xbox zlib stream did not match map header size")
            data = data[:2048] + payload
        base, size = struct.unpack_from("<II", data, 16)
        end = base + size
        bounded(data, base, size, 2048, len(data))
        count = u32(data, base + 12)
        instances = base + u32(data, base) - address_base
        bounded(data, instances, count * 32, base, end)
        tags = []
        for i in range(count):
            offset = instances + i * 32
            name_at = base + u32(data, offset + 16) - address_base
            bounded(data, name_at, 1, base, end)
            name_end = data.find(b"\0", name_at, min(end, name_at + 256))
            if name_end < 0:
                raise ValueError(f"unterminated tag name {i}")
            tags.append({
                "group": u32(data, offset).to_bytes(4, "big").decode("ascii", "replace"),
                "name": data[name_at:name_end].decode("latin-1"),
                "data": u32(data, offset + 20),
            })
        scenarios = [tag for tag in tags if tag["group"] == "scnr"]
        if len(scenarios) != 1:
            raise ValueError(f"expected one scenario, found {len(scenarios)}")
        scnr = base + scenarios[0]["data"] - address_base
        bounded(data, scnr, 0x550, base, end)

        def block(offset, stride):
            n, address = struct.unpack_from("<II", data, scnr + offset)
            if n > 65536:
                raise ValueError(f"excessive block size {n}")
            pos = base + address - address_base if n else base
            bounded(data, pos, n * stride, base, end)
            return [pos + i * stride for i in range(n)]

        def object_list(placement_offset, stride, palette_offset):
            palette = []
            for pos in block(palette_offset, 0x30):
                index = u32(data, pos + 12) & 0xffff
                palette.append(tags[index]["name"] if index < len(tags) else f"index:{index}")
            placements = []
            for pos in block(placement_offset, stride):
                slot = struct.unpack_from("<h", data, pos)[0]
                x, y, z = struct.unpack_from("<fff", data, pos + 8)
                placements.append({"name": palette[slot] if 0 <= slot < len(palette) else f"palette:{slot}",
                                   "xyz": [round(x, 2), round(y, 2), round(z, 2)],
                                   "bsp_mask": struct.unpack_from("<H", data, pos + 0x20)[0],
                                   "flags": struct.unpack_from("<H", data, pos + 4)[0]})
            return placements

        starts = []
        for pos in block(0x354, 0x34):
            starts.append([round(v, 2) for v in struct.unpack_from("<fff", data, pos)])
        netgame_equipment = []
        for pos in block(0x384, 0x90):
            index = u32(data, pos + 0x5c) & 0xffff
            xyz = [round(v, 2) for v in struct.unpack_from("<fff", data, pos + 0x40)]
            netgame_equipment.append({"collection": tags[index]["name"] if index < len(tags) else f"index:{index}",
                                      "xyz": xyz, "respawn": struct.unpack_from("<h", data, pos + 0xe)[0]})
        candidates = [t["name"] for t in tags if t["group"] in ("weap", "vehi") and
                      any(word in t["name"].lower() for word in ("flamethrower", "fuel rod", "fuel_rod", "rwarthog", "rocket warthog"))]
        return {"map": path.stem, "version": version, "tag_count": count,
                "pc_extra_tags": candidates,
                "player_starts": starts,
                "netgame_equipment": netgame_equipment,
                "vehicles": object_list(0x240, 0x78, 0x24c),
                "weapons": object_list(0x270, 0x5c, 0x27c)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("maps", type=Path)
    args = parser.parse_args()
    print(json.dumps([survey(args.maps / f"{name}.map") for name in CAMPAIGN], indent=2))
