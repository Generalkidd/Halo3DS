# New3DS integration additions by Kevin Boggs.
"""Read-only view of PC retail UI widget event handlers used by Quit."""
import argparse
import struct
from pathlib import Path

BASE = 0x40440000


# Kevin Boggs: u32 - New3DS integration.
def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


# Kevin Boggs: main - New3DS integration.
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ui_map", type=Path)
    parser.add_argument("--extended", action="store_true", help="Show every widget using a function index absent from the Xbox table")
    parser.add_argument("--name", help="Print all handlers for widgets whose tag name contains this text")
    args = parser.parse_args()
    data = args.ui_map.read_bytes()
    if len(data) < 2048 or data[:4] != b"daeh" or u32(data, 4) != 7 or data[2044:2048] != b"toof":
        raise ValueError("Expected Halo CE PC retail ui.map")
    start, size = struct.unpack_from("<II", data, 16)
    end = start + size
    if size < 40 or end > len(data) or data[start + 36:start + 40] != b"sgat":
        raise ValueError("Invalid tag index")
    count = u32(data, start + 12)
    instances = start + u32(data, start) - BASE
    if not 0 < count <= 65536 or instances < start + 40 or instances + count * 32 > end:
        raise ValueError("Invalid tag instances")

    # Kevin Boggs: address - New3DS integration.
    def address(pointer, length):
        offset = start + pointer - BASE
        if pointer < BASE or offset < start or length < 0 or offset + length > end:
            raise ValueError(f"Out of bounds pointer 0x{pointer:08x} length {length}")
        return offset

    for index in range(count):
        at = instances + index * 32
        if u32(data, at) != int.from_bytes(b"DeLa", "big"):
            continue
        name_at = address(u32(data, at + 16), 1)
        name_end = data.find(b"\0", name_at, min(name_at + 256, end))
        if name_end < 0:
            raise ValueError("Unterminated tag name")
        name = data[name_at:name_end].decode("latin-1")
        root = address(u32(data, at + 20), 0x3EC)
        handler_count = u32(data, root + 0x54)
        if handler_count > 64:
            raise ValueError("Too many handlers")
        handlers = address(u32(data, root + 0x58), handler_count * 0x48) if handler_count else 0
        rows = []
        for h in range(handler_count):
            item = handlers + h * 0x48
            flags, event_type, function = struct.unpack_from("<Ihh", data, item)
            if args.name or not args.extended or (flags & 0x80 and function >= 102):
                rows.append((h, flags, event_type, function))
        if args.name:
            if args.name.lower() not in name.lower():
                continue
        elif args.extended and not rows or not args.extended and "quit" not in name.lower():
            continue
        print(f"{index} {name} handlers={handler_count}")
        for h, flags, event_type, function in rows:
            print(f"  {h}: flags=0x{flags:03x} event={event_type} function={function}")


if __name__ == "__main__":
    main()
