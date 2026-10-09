"""Audit a PC tag's dependency closure against an expanded Xbox campaign map.

This is a read-only planning aid. A matching name does not prove that two tag
definitions are byte-compatible, and the resulting list is not an import pack.
"""

from __future__ import annotations

import argparse
import json
import mmap
import struct
from pathlib import Path


def u32(data: mmap.mmap, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def inventory(data: mmap.mmap, version: int, base: int) -> tuple[int, int, list[dict]]:
    if len(data) < 2048 or data[:4] != b"daeh" or data[2044:2048] != b"toof":
        raise ValueError("Not a complete Halo cache")
    if u32(data, 4) != version:
        raise ValueError(f"Expected cache version {version}")
    offset, size = struct.unpack_from("<II", data, 16)
    if size < 40 or size > 64 * 1024 * 1024 or offset > len(data) - size:
        raise ValueError("Tag arena is out of bounds")
    directory = u32(data, offset) - base
    count = u32(data, offset + 12)
    if not 0 < count < 32768 or directory < (40 if version == 7 else 36) or directory > size - count * 32:
        raise ValueError("Tag directory is out of bounds")
    result = []
    for index in range(count):
        at = offset + directory + index * 32
        group, _, _, datum, name_pointer, body_pointer = struct.unpack_from("<IIIIII", data, at)
        name_at = name_pointer - base
        if name_at < 0 or name_at >= size:
            raise ValueError(f"Tag {index} name is out of bounds")
        end = data.find(b"\0", offset + name_at, min(offset + size, offset + name_at + 256))
        if end < 0:
            raise ValueError(f"Tag {index} name is unterminated")
        body_at = body_pointer - base
        if body_pointer and not 0 <= body_at < size:
            raise ValueError(f"Tag {index} body is out of bounds")
        result.append({
            "index": index,
            "group": group,
            "datum": datum,
            "name": data[offset + name_at : end].decode("latin-1"),
            "body_offset": body_at if body_pointer else None,
        })
    return offset, size, result


def closure(data: mmap.mmap, arena_offset: int, arena_size: int,
            tags: list[dict], root: int) -> tuple[set[int], dict[int, set[int]], dict[int, int]]:
    by_datum = {tag["datum"]: tag["index"] for tag in tags}
    starts = sorted((tag["body_offset"], tag["index"]) for tag in tags if tag["body_offset"] is not None)
    ends = {index: starts[n + 1][0] if n + 1 < len(starts) else arena_size
            for n, (_, index) in enumerate(starts)}
    references: dict[int, set[int]] = {}
    found = {root}
    queue = [root]
    while queue:
        index = queue.pop()
        start = tags[index]["body_offset"]
        end = ends.get(index, start)
        if start is None or end is None or end < start or end - start > 2_000_000:
            raise ValueError(f"Tag {index} has an implausible body extent")
        refs = {by_datum[word] for at in range(start, end - 3, 4)
                if (word := u32(data, arena_offset + at)) in by_datum}
        references[index] = refs
        unseen = refs - found
        found.update(unseen)
        queue.extend(unseen)
    return found, references, ends


def survey(pc_path: Path, xbox_path: Path, group_name: str, tag_name: str) -> dict:
    with pc_path.open("rb") as pc_file, xbox_path.open("rb") as xbox_file, \
            mmap.mmap(pc_file.fileno(), 0, access=mmap.ACCESS_READ) as pc, \
            mmap.mmap(xbox_file.fileno(), 0, access=mmap.ACCESS_READ) as xbox:
        pc_offset, pc_size, pc_tags = inventory(pc, 7, 0x40440000)
        _, _, xbox_tags = inventory(xbox, 5, 0x803A6000)
        group = int.from_bytes(group_name.encode("ascii"), "big")
        roots = [tag["index"] for tag in pc_tags if tag["group"] == group and
                 tag["name"].casefold() == tag_name.casefold()]
        if len(roots) != 1:
            raise ValueError(f"Expected one root tag, found {len(roots)}")
        found, references, ends = closure(pc, pc_offset, pc_size, pc_tags, roots[0])
        xbox_by_name = {(tag["group"], tag["name"].casefold()): tag for tag in xbox_tags}
        imported = []
        reused = 0
        for index in sorted(found):
            tag = pc_tags[index]
            # The ARM PC converter regroups the PC gbxmodel tag to the Xbox
            # model group before the runtime binds either tag directory.
            group = (int.from_bytes(b"mode", "big") if tag["group"] == int.from_bytes(b"mod2", "big")
                     else tag["group"])
            if (group, tag["name"].casefold()) in xbox_by_name:
                reused += 1
                continue
            imported.append({
                "pc_index": index,
                "group": group.to_bytes(4, "big").decode("ascii", "replace"),
                "name": tag["name"],
                "body_bytes": ends[index] - tag["body_offset"],
                "reference_count": len(references[index]),
            })
        return {
            "pc_cache": pc_path.name,
            "xbox_cache": xbox_path.name,
            "root": tag_name,
            "pc_tag_count": len(pc_tags),
            "xbox_tag_count": len(xbox_tags),
            "closure_count": len(found),
            "reused_by_name_count": reused,
            "missing_count": len(imported),
            "missing_body_bytes": sum(tag["body_bytes"] for tag in imported),
            "missing": imported,
            "warning": "Datum-word scanning is a conservative planning heuristic; validate typed references before import.",
        }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pc_map", type=Path)
    parser.add_argument("expanded_xbox_map", type=Path)
    parser.add_argument("--group", default="vehi")
    parser.add_argument("--name", default=r"vehicles\rwarthog\rwarthog")
    args = parser.parse_args()
    if len(args.group) != 4:
        parser.error("Group must have four ASCII characters")
    print(json.dumps(survey(args.pc_map, args.expanded_xbox_map,
                            args.group, args.name), indent=2))


if __name__ == "__main__":
    main()
