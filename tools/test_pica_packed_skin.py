"""Assemble and check packed skin shader operand routing with PICASSO."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1] / 'port/n3ds'
sys.path.insert(0, str(PORT / 'tools'))
from packed_skin import WEIGHT_OPERANDS, packed_skin_source


def decode(data):
    magic, entries = struct.unpack_from('<4sI', data)
    if magic != b'DVLB' or entries != 1:
        raise ValueError('Expected a single vertex program')
    start = (2 + entries) * 4
    magic, _, code, count, descriptors = struct.unpack_from('<4s4I', data, start)
    if magic != b'DVLP':
        raise ValueError('Missing shader instructions')
    return [(word, struct.unpack_from('<I', data, start + descriptors + (word & 127) * 8)[0])
            for word in struct.unpack_from(f'<{count}I', data, start + code)]


def descriptor_mask(opcode, descriptor):
    def swizzle(lanes, start):
        return sum(3 << (start + (3 - lane) * 2) for lane in lanes)

    if opcode in (14, 15, 18, 19):
        lanes = (0,) if opcode in (14, 15) else tuple(lane for lane in range(4) if descriptor & (8 >> lane))
        return 31 | swizzle(lanes, 5)
    if opcode in (46, 47):
        return (1 << 4) | (1 << 13) | swizzle((0, 1), 5) | swizzle((0, 1), 14)
    return 0xffffffff


class PackedSkinShaders(unittest.TestCase):
    def test_other_programs_are_unchanged(self):
        for shader in (PORT / 'shaders').glob('*.v.pica'):
            name = shader.name[:-7]
            source = shader.read_text()
            if name not in WEIGHT_OPERANDS:
                self.assertEqual(source, packed_skin_source(source, name))

    def test_assembled_weight_lanes_and_instructions(self):
        picasso = os.environ.get('PICASSO') or shutil.which('picasso')
        if not picasso:
            self.skipTest('Set PICASSO to assemble the weighted layouts')
        with tempfile.TemporaryDirectory(prefix='halo-pica-packed-') as directory:
            root = Path(directory)
            for name, expected_moves in WEIGHT_OPERANDS.items():
                with self.subTest(name=name):
                    original = PORT / 'shaders' / f'{name}.v.pica'
                    packed = root / original.name
                    packed.write_text(packed_skin_source(original.read_text(), name))
                    code = []
                    for assembly in (original, packed):
                        binary = root / 'program.shbin'
                        subprocess.run([picasso, '-o', str(binary), str(assembly)], check=True, timeout=20)
                        code.append(decode(binary.read_bytes()))
                    self.assertEqual(len(code[0]), len(code[1]))
                    moved = 0
                    for (old, old_desc), (new, new_desc) in zip(*code):
                        opcode = old >> 26
                        if opcode == 8 and ((old >> 12) & 127) == 3:
                            register, width, swizzle = 12, 7, 5
                        elif opcode in (46, 47) and ((old >> 7) & 31) == 3:
                            register, width, swizzle = 7, 5, 14
                        else:
                            # Operand descriptors can be renumbered or differ
                            # in the unused second input of MOV/MOVA.
                            if opcode < 32 or opcode in (46, 47):
                                self.assertEqual(old & ~127, new & ~127)
                                relevant = descriptor_mask(opcode, old_desc)
                                self.assertEqual(old_desc & relevant, new_desc & relevant)
                            else:
                                self.assertEqual(old, new)
                            continue
                        register_mask = ((1 << width) - 1) << register
                        self.assertEqual(old & ~(register_mask | 127), new & ~(register_mask | 127))
                        self.assertEqual((new & register_mask) >> register, 4)
                        swizzle_mask = 255 << swizzle
                        self.assertEqual(old_desc & ~swizzle_mask, new_desc & ~swizzle_mask)
                        self.assertIn((old_desc & swizzle_mask) >> swizzle, (0xaa, 0xff))
                        self.assertEqual(((old_desc & swizzle_mask) >> swizzle) - 0xaa,
                                         (new_desc & swizzle_mask) >> swizzle)
                        moved += 1
                    self.assertEqual(moved, expected_moves)


if __name__ == '__main__':
    unittest.main()
