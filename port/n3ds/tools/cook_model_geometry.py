#!/usr/bin/env python3
"""Cook bounded integer compaction plans from one prepared Xbox Halo map."""
import argparse
from pathlib import Path
import struct
import tempfile
import zlib

MAP_HEADER_BYTES = 2048
MAX_TAG_BYTES = 0x1600000
ARCHIVE_HEADER = struct.Struct('<9I')
ARCHIVE_RECORD = struct.Struct('<8I')
ARCHIVE_MAGIC = 0x31474d4e  # NMG1
ARCHIVE_VERSION = 1
MAX_ARCHIVE_RECORDS = 512
MAX_ARCHIVE_BYTES = 256 * 1024
MIN_GPU_SAVING = 4096


def fourcc(value):
    return int.from_bytes(value.encode('ascii'), 'big')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def read_map(path):
    with path.open('rb') as file:
        header = file.read(MAP_HEADER_BYTES)
        require(len(header) == MAP_HEADER_BYTES and u32(header, 0) == fourcc('head') and
                u32(header, 4) == 5 and u32(header, 2044) == fourcc('foot'), 'Unsupported Xbox map header')
        length, _, offset, size = struct.unpack_from('<4I', header, 8)
        require(MAP_HEADER_BYTES <= offset <= length and 36 <= size <= MAX_TAG_BYTES and
                offset + size <= length and length == path.stat().st_size, 'Invalid map tag range')
        file.seek(offset)
        tags = file.read(size)
        require(len(tags) == size and u32(tags, 32) == fourcc('tags'), 'Invalid Xbox tag region')
    return header, tags


def read_relocations(path, header, tags):
    with path.open('rb') as file:
        fields = file.read(32)
        require(len(fields) == 32, 'Truncated relocation header')
        magic, version, size, base, crc, count, records_crc, header_crc = struct.unpack('<8I', fields)
        require((magic, version, base) == (0x314c524e, 1, 0x803a6000) and size == len(tags) and
                crc == zlib.crc32(tags) and header_crc == zlib.crc32(header) and 0 < count <= size // 4,
                'Relocation archive does not match the map')
        data = file.read(count * 12)
        require(len(data) == count * 12 and not file.read(1) and zlib.crc32(data) == records_crc,
                'Invalid relocation records')
    records = {}
    previous = -1
    for offset, expected, target in struct.iter_unpack('<3I', data):
        require(previous < offset <= size - 4 and not offset % 4 and u32(tags, offset) == expected and
                (target < size or target == 0xffffffff), 'Invalid relocation for map tag data')
        records[offset] = target
        previous = offset
    return records


def ordered_corners(raw_indices, triangles, triangle_type, vertices):
    indices = [value[0] for value in struct.iter_unpack('<H', raw_indices)]
    require(len(indices) == (triangles + 2 if triangle_type else triangles * 3) and
            all(index < vertices for index in indices), 'Invalid model triangle buffer')
    output = []
    for triangle in range(triangles):
        start = triangle if triangle_type else triangle * 3
        a, b, c = indices[start:start+3]
        if a == b or b == c or a == c:
            continue
        if triangle_type and triangle & 1:
            a, b = b, a
        output.extend((a, b, c))
    return output


def vertex_stride(raw_vertices):
    active_nodes = set()
    for start in range(0, len(raw_vertices), 32):
        first, second, weight = struct.unpack_from('<BBh', raw_vertices, start + 28)
        if weight < 0 or any(node != 253 and (node % 3 or node >= 192) for node in (first, second)):
            return 0
        if (weight and first == 253) or (weight != 32767 and second == 253):
            return 0
        if weight:
            active_nodes.add(first)
        if weight != 32767:
            active_nodes.add(second)
        if len(active_nodes) > 29:
            return 0
    return 32 if len(active_nodes) == 1 else 48


def compact_plan(raw_vertices, corners, exact):
    if not exact:
        representatives = sorted(set(corners))
        remap = {source: slot for slot, source in enumerate(representatives)}
        return representatives, [remap[source] for source in corners]
    representatives, remap, tuples = [], {}, {}
    output = []
    for source in corners:
        if source not in remap:
            vertex = raw_vertices[source*32:source*32+32]
            slot = tuples.get(vertex)
            if slot is None:
                slot = len(representatives)
                tuples[vertex] = slot
                representatives.append(source)
            remap[source] = slot
        output.append(remap[source])
    return representatives, output


def model_buffers(tags, relocations):
    def target(offset, size):
        address = relocations.get(offset)
        require(address is not None and 0 <= size <= len(tags)-address, 'Invalid model tag pointer')
        return address

    count = u32(tags, 12)
    require(0 < count <= 65535, 'Invalid map tag count')
    table = target(0, count * 32)
    for entry in range(table, table+count*32, 32):
        if u32(tags, entry) != fourcc('mode'):
            continue
        model = target(entry+20, 232)
        geometry_count, shader_count = u32(tags, model+208), u32(tags, model+220)
        if not geometry_count or not shader_count:
            continue
        geometries = target(model+212, geometry_count * 48)
        shaders = target(model+224, shader_count * 32)
        for geometry in range(geometries, geometries+geometry_count*48, 48):
            part_count = u32(tags, geometry+36)
            if not part_count:
                continue
            parts = target(geometry+40, part_count * 104)
            for part in range(parts, parts+part_count*104, 104):
                shader = struct.unpack_from('<h', tags, part+4)[0]
                if u32(tags, part) & 1 or not 0 <= shader < shader_count:
                    continue
                handle = u32(tags, shaders+shader*32+12)
                shader_id = handle & 0xffff
                if shader_id >= count:
                    continue
                shader_entry = table+shader_id*32
                if u32(tags, shader_entry+12) != handle or u32(tags, shader_entry) not in (fourcc('soso'), fourcc('senv')):
                    continue
                triangle_type, triangles = u32(tags, part+68) & 0xffff, u32(tags, part+72)
                vertex_type, vertices = u32(tags, part+84) & 0xffff, u32(tags, part+88)
                if vertex_type != 5 or triangle_type not in (0, 1) or not (
                        0 < triangles <= 65535 and 0 < vertices <= 65535):
                    continue
                index_bytes = (triangles+2)*2 if triangle_type else triangles*6
                indices = target(part+76, index_bytes)
                vertex = target(part+96, vertices*32)
                key = (vertex, indices, vertices, triangles, triangle_type)
                yield key, tags[vertex:vertex+vertices*32], tags[indices:indices+index_bytes]


def cook(header, tags, buffers, *, exact=False, limit=MAX_ARCHIVE_BYTES):
    require(ARCHIVE_HEADER.size <= limit <= MAX_ARCHIVE_BYTES, 'Invalid cooked geometry archive limit')
    candidates = {}
    for key, vertices, indices in buffers:
        if key in candidates:
            continue
        stride = vertex_stride(vertices)
        if not stride or key[2] * stride < MIN_GPU_SAVING:
            continue
        corners = ordered_corners(indices, key[3], key[4], key[2])
        if not corners:
            continue
        representatives, remapped = compact_plan(vertices, corners, exact)
        gpu_bytes = len(representatives)*stride + len(remapped)*2
        saving = key[2]*stride-gpu_bytes
        if gpu_bytes > MAX_ARCHIVE_BYTES or saving < MIN_GPU_SAVING:
            continue
        candidates[key] = (saving, len(corners), len(representatives))

    selected = {}
    remaining = limit-ARCHIVE_HEADER.size
    for key, info in sorted(candidates.items(), key=lambda item: (-item[1][0], item[0])):
        bytes_required = ARCHIVE_RECORD.size + 2*(info[1]+info[2])
        if len(selected) == MAX_ARCHIVE_RECORDS:
            break
        if bytes_required <= remaining:
            remaining -= bytes_required
            selected[key] = info

    entries, plans = bytearray(), bytearray()
    for key, (_, corners, used) in sorted(selected.items()):
        entries += ARCHIVE_RECORD.pack(*key, corners, used, len(selected)*ARCHIVE_RECORD.size + len(plans))
        vertex, index, vertices, triangles, triangle_type = key
        index_bytes = (triangles+2)*2 if triangle_type else triangles*6
        source = ordered_corners(tags[index:index+index_bytes], triangles, triangle_type, vertices)
        reps, remapped = compact_plan(tags[vertex:vertex+vertices*32], source, exact)
        plans += struct.pack(f'<{len(reps)+len(remapped)}H', *reps, *remapped)
    payload = entries + plans
    return ARCHIVE_HEADER.pack(ARCHIVE_MAGIC, ARCHIVE_VERSION, exact, len(tags), zlib.crc32(tags),
                               zlib.crc32(header), len(selected), len(payload), zlib.crc32(payload)) + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map', type=Path)
    parser.add_argument('--relocations', type=Path, help='Map .nrl file (default is beside the map)')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compact-mode', choices=('reference', 'exact'), default='reference')
    parser.add_argument('--max-bytes', type=int, default=MAX_ARCHIVE_BYTES)
    args = parser.parse_args()
    relocations = args.relocations or args.map.with_suffix('.nrl')
    if args.output.suffix != '.nmg' or args.output.resolve() in (args.map.resolve(), relocations.resolve()):
        parser.error('Output must be a separate .nmg file')
    try:
        header, tags = read_map(args.map)
        records = read_relocations(relocations, header, tags)
        archive = cook(header, tags, model_buffers(tags, records), exact=args.compact_mode == 'exact',
                       limit=args.max_bytes)
        with tempfile.TemporaryDirectory(prefix='halo-model-geometry-', dir=args.output.parent) as directory:
            stage = Path(directory) / 'archive.nmg'
            stage.write_bytes(archive)
            stage.replace(args.output)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'Geometry cook failed: {error}\n')
    count = ARCHIVE_HEADER.unpack_from(archive)[6]
    print(f'Cooked {count} model plans into {args.output} ({len(archive)} bytes)')


if __name__ == '__main__':
    main()
