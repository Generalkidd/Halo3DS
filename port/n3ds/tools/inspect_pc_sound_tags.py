# New3DS integration additions by Kevin Boggs.
"""Inspect owner-supplied PC cache sound references without modifying the map."""
import argparse
import struct
from pathlib import Path

BASE = 0x40440000


# Kevin Boggs: inspect - New3DS integration.
def inspect(path: Path, query: str):
    data = path.read_bytes()
    u32 = lambda at: struct.unpack_from("<I", data, at)[0]
    if data[:4] != b"daeh" or u32(4) != 7:
        raise ValueError("Expected a PC retail cache")
    start, size = struct.unpack_from("<II", data, 16)
    end = start + size
    count = u32(start + 12)
    instances = start + u32(start) - BASE

    # Kevin Boggs: ptr - New3DS integration.
    def ptr(address, length):
        offset = start + address - BASE
        if address < BASE or offset < start or offset + length > end:
            raise ValueError("Tag pointer outside cache")
        return offset

    if not 0 < count <= 65536 or instances < start + 40 or instances + 32 * count > end:
        raise ValueError("Invalid tag index")
    for i in range(count):
        at = instances + 32 * i
        name_at = ptr(u32(at + 16), 1)
        name_end = data.find(b"\0", name_at, min(name_at + 256, end))
        if name_end < 0:
            raise ValueError("Unterminated tag name")
        name = data[name_at:name_end].decode("latin-1")
        if query.lower() not in name.lower():
            continue
        group = data[at:at + 4][::-1].decode("latin-1")
        root = ptr(u32(at + 20), 0x54) if u32(at + 20) else 0
        print(f"{i} {group} {name} root={root:#x}")
        if group == "lsnd":
            n = u32(root + 0x3c)
            tracks = ptr(u32(root + 0x40), n * 0xa0) if n else 0
            print(f"  tracks={n}")
            for j in range(n):
                track = tracks + j * 0xa0
                print("  track", j, "refs", [(hex(u32(track + k)), hex(u32(track + k + 12))) for k in (0x30, 0x40, 0x50, 0x80, 0x90)])
        if group == "snd!":
            n = u32(root + 0x98)
            ranges = ptr(u32(root + 0x9c), n * 0x48) if n else 0
            print(f"  class={struct.unpack_from('<h', data, root + 4)[0]} sample_rate={struct.unpack_from('<h', data, root + 6)[0]} ranges={n}")
            for j in range(n):
                item = ranges + j * 0x48
                p_count = u32(item + 0x3c)
                perms = ptr(u32(item + 0x40), p_count * 0x7c) if p_count else 0
                for k in range(p_count):
                    perm = perms + k * 0x7c
                    pname = data[perm:perm + 32].split(b"\0")[0].decode("latin-1")
                    compression = struct.unpack_from("<h", data, perm + 0x28)[0]
                    print(f"  permutation {j}:{k} {pname} compression={compression} size={u32(perm + 0x40)} flags={u32(perm + 0x44):#x} offset={u32(perm + 0x48):#x}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("map", type=Path)
    parser.add_argument("query")
    args = parser.parse_args()
    inspect(args.map, args.query)
