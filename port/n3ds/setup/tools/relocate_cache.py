"""Audit Xbox cache pointers using explicit tag schemas before native relocation.

The external --schemas directory is Invader's JSON format reference, not a
library linked into the game. Original maps are read-only. No address-looking
word is ever treated as a pointer without a declared field or Xbox layout rule.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

SIZES = {'Angle': 4, 'ColorARGB': 16, 'ColorARGBInt': 4, 'ColorRGB': 12,
         'Euler2D': 8, 'Euler3D': 12, 'Fraction': 4, 'Index': 2, 'Matrix': 36,
         'Plane2D': 12, 'Plane3D': 16, 'Point2D': 8, 'Point2DInt': 4, 'Point3D': 12,
         'Pointer': 4, 'Quaternion': 16, 'Rectangle2D': 8, 'ScenarioScriptNodeValue': 4,
         'TagDataOffset': 20, 'TagDependency': 16, 'TagFourCC': 4, 'TagID': 4,
         'TagReflexive': 12, 'TagString': 32, 'Vector2D': 8, 'Vector3D': 12,
         'float': 4, 'int16': 2, 'int32': 4, 'int8': 1, 'uint16': 2, 'uint32': 4, 'uint8': 1}


class Schemas:
    def __init__(self, directory):
        self.types = {}
        self.hashes = {}
        for file in sorted(directory.glob('*.json')):
            self.hashes[file.name] = hashlib.sha256(file.read_bytes()).hexdigest()
            for definition in json.loads(file.read_text()):
                assert definition['name'] not in self.types, definition['name']
                self.types[definition['name']] = definition
        self.layouts = {}
        for name, definition in self.types.items():
            if definition['type'] == 'struct':
                self.layout(name)
        classes = directory.parents[3] / 'include/invader/hek/fourcc.hpp'
        fourcc = {name.lower(): int(value, 16) for name, value in
                  re.findall(r'TAG_FOURCC_(\w+)\s*=\s*(0x[0-9a-fA-F]+)', classes.read_text())}
        self.roots = {fourcc[d['class']]: d['name'] for d in self.types.values()
                      if 'class' in d and d['class'] in fourcc}
        # Both collection groups have the same sequence of tag dependencies.
        self.roots[fourcc['ui_widget_collection']] = 'TagCollection'

    def size(self, name):
        if name in SIZES:
            return SIZES[name]
        definition = self.types[name]
        if definition['type'] == 'enum': return 2
        if definition['type'] == 'bitfield': return definition['width'] // 8
        return definition['size']

    def layout(self, name):
        if name in self.layouts: return self.layouts[name]
        definition = self.types[name]
        fields = []
        offset = 0
        if 'inherits' in definition:
            fields.extend(self.layout(definition['inherits']))
            offset = self.size(definition['inherits'])
        for field in definition['fields']:
            if field['type'] == 'pad':
                offset += field['size']
                continue
            size = self.size(field['type'])
            count = field.get('count', 1) * (2 if field.get('bounds') else 1)
            for i in range(count):
                fields.append((offset + i*size, field))
            offset += size*count
        assert offset == definition['size'], (name, offset, definition['size'])
        self.layouts[name] = fields
        return fields


class Region:
    def __init__(self, data, base, schemas, global_region=None):
        self.data, self.base, self.schemas = data, base, schemas
        self.pointers = {}
        self.visited = set()
        self.active = set()
        self.counts = Counter()
        self.deferred = []
        self.normalized = Counter()
        self.global_region = global_region

    def u32(self, offset):
        if offset < 0 or offset + 4 > len(self.data): raise ValueError(f'word outside region: {offset:x}')
        return struct.unpack_from('<I', self.data, offset)[0]

    def resolve(self, address, size):
        offset = address - self.base
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f'extent outside region: {address:08x}+{size:x}')
        return offset

    def pointer(self, offset, size=1, nullable=True):
        address = self.u32(offset)
        if not address and nullable: return None
        target = self.resolve(address, size)
        if offset & 3: raise ValueError(f'unaligned pointer field: {offset:x}')
        self.pointers[offset] = target
        return target

    def walk(self, name, offset, depth=0):
        key = name, offset
        if depth > 64 or key in self.active: raise ValueError(f'cyclic/deep schema traversal: {name}')
        if key in self.visited: return
        self.resolve(self.base + offset, self.schemas.size(name))
        self.active.add(key)
        try:
            for relative, field in self.schemas.layout(name):
                pos = offset + relative
                kind = field['type']
                label = name + '.' + field['name']
                try:
                    if kind == 'TagReflexive':
                        count, address, definition = struct.unpack_from('<III', self.data, pos)
                        if definition: raise ValueError(f'cached block schema not null: {definition:08x}')
                        if count > 1000000: raise ValueError(f'excessive block count: {count}')
                        target = self.pointer(pos+4, count*self.schemas.size(field['struct']), not count)
                        if target is not None:
                            for i in range(count):
                                self.walk(field['struct'], target+i*self.schemas.size(field['struct']), depth+1)
                    elif kind == 'TagDependency':
                        length = self.u32(pos+8)
                        address = self.u32(pos+4)
                        name_data = self.data
                        if address and self.global_region is not None and address < self.base:
                            target = self.global_region.resolve(address, max(length+1, 1))
                            self.pointers[pos+4] = target | 0x80000000
                            name_data = self.global_region.data
                        else:
                            target = self.pointer(pos+4, max(length+1, 1))
                        if target is not None:
                            end = name_data.find(b'\0', target, target+256)
                            # Cached references retain their name but set length
                            # to zero; see actual source tag_reference consumers.
                            if end < 0 or (length and end-target != length):
                                raise ValueError('invalid dependency name')
                    elif kind == 'TagDataOffset':
                        size, external, file_offset, address, definition = struct.unpack_from('<IIIII', self.data, pos)
                        if definition: raise ValueError(f'cached data schema not null: {definition:08x}')
                        # Streamed sound/bitmap data can have size with no resident
                        # pointer; runtime cache services load it from file_offset.
                        self.pointer(pos+12, size)
                        if label == 'Scenario.script syntax data' and size:
                            target = self.resolve(address, size)
                            maximum, stride = struct.unpack_from('<hh', self.data, target+32)
                            if maximum < 0 or stride != 20 or size != 56+maximum*stride or self.u32(target+40) != 0x64407440:
                                raise ValueError('invalid script datum array')
                            # Same rebinding performed by hs_scenario_postprocess().
                            self.pointers[target+52] = target+56
                            self.normalized['script_datum_array'] += 1
                    elif kind == 'Pointer':
                        if name == 'BitmapData':
                            # Xbox bitmap_data offset 36 is cache_block_index,
                            # not the PC pointer described by this schema.
                            if self.u32(pos) != 0xffffffff: raise ValueError('bitmap already cached')
                        elif name == 'ModelGeometryPart':
                            # Reconstruct the CPU vertex pointer from the real
                            # descriptor below; its serialized value is stale.
                            pass
                        else:
                            self.pointer(pos)
                    elif kind in self.schemas.types and self.schemas.types[kind]['type'] == 'struct':
                        self.walk(kind, pos, depth+1)
                except ValueError as error:
                    raise ValueError(f'{label} @ {pos:x}: {error}') from error
            if name == 'BitmapData':
                # Same reset as texture_cache_bitmap_new() in the game source.
                self.pointers[offset+40] = None
                self.pointers[offset+44] = None
                self.normalized['bitmap_runtime_addresses'] += 1
            elif name == 'ModelGeometryPart':
                triangles = self.u32(offset+72)
                triangle_type = self.u32(offset+68) & 0xffff
                triangle_size = triangles*6 if triangle_type == 0 else (triangles+2)*2
                if triangle_type not in (0, 1): raise ValueError('unknown triangle buffer format')
                self.pointer(offset+76, triangle_size, not triangles)
                self.pointer(offset+80, 12, not triangles)
                vertex_count = self.u32(offset+88)
                vertex_type = self.u32(offset+84) & 0xffff
                if vertex_type not in (4, 5): raise ValueError('unknown model vertex format')
                descriptor = self.pointer(offset+100, 12, not vertex_count)
                if descriptor is not None:
                    target = self.resolve(self.u32(descriptor+4), vertex_count*(68 if vertex_type == 4 else 32))
                    self.pointers[offset+96] = target
                    self.normalized['model_cpu_vertex_pointer'] += 1
            elif name == 'ScenarioBSP':
                self.deferred.append({'field': 'ScenarioBSP.bsp address', 'offset': offset+8,
                                      'address': self.u32(offset+8), 'file_offset': self.u32(offset),
                                      'bytes': self.u32(offset+4), 'handle': self.u32(offset+28)})
            elif name == 'ScenarioStructureBSPMaterial':
                # The two embedded vertex_buffer objects use resident D3D
                # descriptors. Reconstruct their stale CPU addresses too.
                for start, expected_type, stride in [(176, 1, 32), (196, 3, 8)]:
                    count = self.u32(offset+start+4)
                    if not count:
                        self.pointers[offset+start+12] = None
                        continue
                    if self.u32(offset+start) & 0xffff != expected_type:
                        raise ValueError('unexpected Xbox BSP vertex format')
                    descriptor = self.resolve(self.u32(offset+start+16), 12)
                    target = self.resolve(self.u32(descriptor+4), count*stride)
                    self.pointers[offset+start+12] = target
                    self.normalized['bsp_cpu_vertex_pointer'] += 1
            self.visited.add(key)
            self.counts[name] += 1
        finally:
            self.active.remove(key)


def write_package(region, header, path):
    records = b''.join(struct.pack('<III', pos, region.u32(pos),
                       0xffffffff if target is None else target)
                       for pos, target in sorted(region.pointers.items()))
    package_header = struct.pack('<8I', 0x314c524e, 1, len(region.data), region.base,
                                 zlib.crc32(region.data), len(region.pointers),
                                 zlib.crc32(records), zlib.crc32(header))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(package_header + records)


def geometry_headers(region, count_offsets):
    for count_offset, pointer_offset in count_offsets:
        buffers = region.u32(count_offset)
        assert buffers < 65536
        descriptors = region.pointer(pointer_offset, buffers*12, not buffers)
        if descriptors is not None:
            for i in range(buffers): region.pointer(descriptors+i*12+4, 1, False)


def audit(path, schemas, package_dir=None):
    with path.open('rb') as stream:
        header = stream.read(2048)
        signature, version, length = struct.unpack_from('<III', header)
        assert signature == 0x68656164 and version == 5 and struct.unpack_from('<I', header, 2044)[0] == 0x666f6f74
        offset, size = struct.unpack_from('<II', header, 16)
        assert offset >= 2048 and size <= 0x1600000 and offset+size <= length <= path.stat().st_size
        stream.seek(offset)
        data = stream.read(size)
    region = Region(data, 0x803a6000, schemas)
    directory, scenario, checksum, count, vertex_count, vertices, index_count, indices, sig = struct.unpack_from('<9I', data)
    assert sig == 0x74616773 and 0 < count <= 65535
    table = region.pointer(0, count*32, False)
    # Xbox D3D vertex/index headers are three words, with resident data in the
    # second word. Their Common/Lock words are graphics state, not pointers.
    geometry_headers(region, [(16, 20), (24, 28)])
    errors, tags = [], []
    for i in range(count):
        entry = table+i*32
        group, parent, grandparent, handle, name, body, unused0, unused1 = struct.unpack_from('<8I', data, entry)
        assert handle & 0xffff == i
        name_offset = region.pointer(entry+16, 1, False)
        end = data.find(b'\0', name_offset, name_offset+256)
        assert end != -1
        tag_name = data[name_offset:end].decode('ascii')
        row = {'index': i, 'group': group.to_bytes(4, 'big').decode('ascii'), 'name': tag_name}
        tags.append(row)
        if group == 0x73627370:
            assert body == 0  # streamed independently at BSP transition
            continue
        try:
            root = schemas.roots[group]
            location = region.pointer(entry+20, schemas.size(root), False)
            region.walk(root, location)
        except (ValueError, KeyError) as error:
            errors.append({**row, 'error': str(error)})
    package = None
    map_name = header[32:64].split(b'\0')[0].decode('ascii')
    assert re.fullmatch(r'[a-zA-Z0-9_-]+', map_name)
    bsps = []
    with path.open('rb') as stream:
        for i, ref in enumerate(region.deferred):
            assert 2048 <= ref['file_offset'] <= length-ref['bytes'] and 24 <= ref['bytes'] <= 0x1600000
            stream.seek(ref['file_offset'])
            bsp = Region(stream.read(ref['bytes']), ref['address'], schemas, region)
            bsp_errors = []
            try:
                if bsp.u32(20) != 0x73627370: raise ValueError('invalid BSP signature')
                body = bsp.pointer(0, schemas.size('ScenarioStructureBSP'), False)
                geometry_headers(bsp, [(4, 8), (12, 16)])
                bsp.walk('ScenarioStructureBSP', body)
            except ValueError as error:
                bsp_errors.append(str(error))
                errors.append({'bsp': i, 'error': str(error)})
            bsp_package = None
            if package_dir is not None and not bsp_errors:
                bsp_package = package_dir / f'{map_name}-bsp{i}.nrl'
                write_package(bsp, header, bsp_package)
            bsps.append({'index': i, 'bytes': len(bsp.data), 'pointers': len(bsp.pointers),
                         'errors': bsp_errors, 'normalized': dict(bsp.normalized),
                         'sha256': hashlib.sha256(bsp.data).hexdigest(),
                         'package': str(bsp_package) if bsp_package else None})
    if package_dir is not None and not errors:
        package = package_dir / (map_name + '.nrl')
        write_package(region, header, package)
    return {'map': str(path), 'tag_bytes': size, 'tag_count': count,
            'tag_sha256': hashlib.sha256(data).hexdigest(), 'tag_crc32': zlib.crc32(data),
            'schema_structures': dict(region.counts), 'pointer_count': len(region.pointers),
            'deferred': region.deferred, 'errors': errors, 'tags': tags,
            'normalized': dict(region.normalized),
            'bsps': bsps,
            'package': str(package) if package else None,
            'status': 'tag-region relocation prepared; BSPs load separately; runtime behavior still needs verification'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schemas', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--package-dir', type=Path)
    parser.add_argument('maps', type=Path, nargs='+')
    args = parser.parse_args()
    schemas = Schemas(args.schemas)
    reports = [audit(path, schemas, args.package_dir) for path in args.maps]
    args.output.write_bytes((json.dumps({'schemas': schemas.hashes, 'maps': reports}, indent=2)+'\n').encode())
    for report in reports:
        print({key: report[key] for key in ['map', 'tag_count', 'pointer_count', 'status']})
        print('Errors:', len(report['errors']))
        for error in report['errors'][:12]: print(error)
    if any(report['errors'] for report in reports): raise SystemExit(1)


if __name__ == '__main__': main()
