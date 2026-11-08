"""Check the model geometry cooker against a small Xbox tag and strip fixture."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'port/n3ds/tools'))
from cook_model_geometry import (ARCHIVE_HEADER, ARCHIVE_RECORD, cook, fourcc, model_buffers,
                                 read_map, read_relocations)


def xbox_fixture(directory):
    # A model tag, opaque shader, and sparse strip with two parity connectors.
    table, model, geometry, part, shader, vertex = 64, 128, 360, 408, 512, 544
    index = vertex + 256 * 32
    tags = bytearray(index + 16)
    xbox_base = 0x803a6000
    records = {}

    def put(offset, value):
        struct.pack_into('<I', tags, offset, value)

    def pointer(offset, destination, *, pending=False):
        value = 0 if pending else xbox_base + destination
        put(offset, value)
        records[offset] = (value, destination)

    pointer(0, table)
    put(12, 2)
    put(32, fourcc('tags'))
    put(table, fourcc('mode'))
    pointer(table+20, model)
    put(table+32, fourcc('soso'))
    put(table+32+12, 0x12340001)
    put(model+208, 1)
    pointer(model+212, geometry)
    put(model+220, 1)
    pointer(model+224, shader)
    put(shader+12, 0x12340001)
    put(geometry+36, 1)
    pointer(geometry+40, part)
    put(part+68, 1)
    put(part+72, 5)
    pointer(part+76, index)
    put(part+84, 5)
    put(part+88, 256)
    pointer(part+96, vertex, pending=True)
    for source in range(256):
        struct.pack_into('<BBh', tags, vertex+source*32+28, 0, 253, 32767)
    put(vertex+2*32+20, 1)
    put(vertex+3*32+16, 2)
    struct.pack_into('<7H', tags, index, 7, 2, 1, 1, 3, 2, 0)
    header = bytearray(2048)
    struct.pack_into('<6I', header, 0, fourcc('head'), 5, len(header)+len(tags), 0, len(header), len(tags))
    struct.pack_into('<I', header, 2044, fourcc('foot'))
    relocations = b''.join(struct.pack('<3I', offset, *records[offset]) for offset in sorted(records))
    map_path, nrl_path = directory / 'fixture.map', directory / 'fixture.nrl'
    map_path.write_bytes(header+tags)
    nrl_path.write_bytes(struct.pack('<8I', 0x314c524e, 1, len(tags), xbox_base, zlib.crc32(tags),
                                     len(records), zlib.crc32(relocations), zlib.crc32(header))+relocations)
    return map_path, nrl_path, (vertex, index, 256, 5, 1)


class CookedModelGeometry(unittest.TestCase):
    def test_cook_preserves_strip_corners_and_reference_policy(self):
        with tempfile.TemporaryDirectory(prefix='halo-cooked-geometry-') as temporary:
            map_path, nrl_path, key = xbox_fixture(Path(temporary))
            header, tags = read_map(map_path)
            relocations = read_relocations(nrl_path, header, tags)
            buffers = list(model_buffers(tags, relocations))
            self.assertEqual([buffer[0] for buffer in buffers], [key])
            for exact, representatives, indices in [
                (False, (0,1,2,3,7), (4,2,1,3,1,2,3,2,0)),
                (True, (7,2,3), (0,1,0,2,0,1,2,1,0)),
            ]:
                archive = cook(header, tags, buffers, exact=exact)
                fields = ARCHIVE_HEADER.unpack_from(archive)
                self.assertEqual(fields[2:7], (exact,len(tags),zlib.crc32(tags),zlib.crc32(header),1))
                payload = archive[ARCHIVE_HEADER.size:]
                self.assertEqual(fields[7:], (len(payload),zlib.crc32(payload)))
                self.assertEqual(ARCHIVE_RECORD.unpack_from(payload), (*key,9,len(representatives),ARCHIVE_RECORD.size))
                expected = representatives + indices
                self.assertEqual(struct.unpack_from(f'<{len(expected)}H', payload, ARCHIVE_RECORD.size), expected)
                self.assertEqual(len(cook(header, tags, buffers, exact=exact, limit=ARCHIVE_HEADER.size)), ARCHIVE_HEADER.size)

    def test_rejects_relocations_for_different_map_bytes(self):
        with tempfile.TemporaryDirectory(prefix='halo-cooked-geometry-') as temporary:
            map_path, nrl_path, _ = xbox_fixture(Path(temporary))
            header, tags = read_map(map_path)
            changed = bytearray(tags)
            changed[-1] ^= 1
            with self.assertRaisesRegex(ValueError, 'does not match'):
                read_relocations(nrl_path, header, changed)


if __name__ == '__main__':
    unittest.main()
