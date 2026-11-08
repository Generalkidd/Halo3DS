"""Check assembled UV lane equivalence: PICASSO=/path/to/picasso python3 -m unittest discover -s tools -p test_pica_uv_outputs.py.

This proves the local instruction/layout change, not PICA arithmetic, image
parity, or a scene performance improvement. Temporary build products are removed.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest


PORT = Path(__file__).resolve().parents[1] / 'port/n3ds'
SPEC = importlib.util.spec_from_file_location('pica_shaders', PORT / 'tools/shaders.py')
SHADERS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SHADERS)


def program(data):
    magic, entries, dvle = struct.unpack_from('<4sII', data)
    if magic != b'DVLB' or entries != 1:
        raise ValueError('Expected a single vertex program')
    dvlp = (2 + entries) * 4
    magic, _, code, count, descriptors, desc_count = struct.unpack_from('<4s5I', data, dvlp)
    if magic != b'DVLP':
        raise ValueError('Missing program')
    instructions = struct.unpack_from(f'<{count}I', data, dvlp + code)
    operands = [struct.unpack_from('<I', data, dvlp + descriptors + 8 * i)[0] for i in range(desc_count)]
    # Only the entry-end address is expected to move in DVLE metadata.
    return instructions, operands, data[dvle:dvle + 12] + data[dvle + 16:]


def lanes(word, descriptor):
    mask = descriptor & 15
    src1, src2 = (descriptor >> 4) & 511, (descriptor >> 13) & 511
    return {i: (word & ~127, src1 & 1, (src1 >> (7 - 2 * i)) & 3,
                src2 & 1, (src2 >> (7 - 2 * i)) & 3)
            for i in range(4) if mask & (8 >> i)}


class UVOutputs(unittest.TestCase):
    def test_rejects_changed_or_duplicate_sequences(self):
        source = (PORT / 'shaders/skin_lit.v.pica').read_text()
        with self.assertRaises(ValueError):
            SHADERS._uv_output_variant(source.replace('options.xyxy', 'options.zwzw'), 'texcoord')
        with self.assertRaises(ValueError):
            SHADERS._uv_output_variant(source + source, 'texcoord')

    def test_assembled_lane_routing_and_unaffected_programs(self):
        picasso = os.environ.get('PICASSO') or shutil.which('picasso')
        if not picasso:
            self.skipTest('Set PICASSO to assemble both variants')
        with tempfile.TemporaryDirectory(prefix='halo-pica-uv-') as directory:
            root = Path(directory)
            reference, candidate = root / 'reference', root / 'candidate'
            reference.mkdir()
            candidate.mkdir()
            SHADERS.generate(PORT, reference, picasso)
            SHADERS.generate(PORT, candidate, picasso, uv_outputs=True)
            changed = set()
            for path in reference.glob('*.shbin'):
                before, after = path.read_bytes(), (candidate / path.name).read_bytes()
                if before == after:
                    continue
                changed.add(path.stem)
                with self.subTest(program=path.stem):
                    old, old_desc, old_meta = program(before)
                    new, new_desc, new_meta = program(after)
                    self.assertEqual(old_meta, new_meta)
                    self.assertEqual(len(old), len(new) + 1)
                    self.assertEqual(old[:-3], new[:-2])
                    self.assertEqual(old[-1], new[-1])
                    for op in old[:-3]:
                        if op >> 26 < 32 or op >> 26 in (46, 47):
                            index = op & 127
                            relevant = 0x1fff if op >> 26 in (18, 19) else 0xffffffff
                            self.assertEqual(old_desc[index] & relevant, new_desc[index] & relevant)
                    expected = {}
                    for op in old[-3:-1]:
                        self.assertEqual(op >> 26, 8)  # MUL
                        output = lanes(op, old_desc[op & 127])
                        self.assertFalse(expected.keys() & output.keys())
                        expected.update(output)
                    self.assertEqual(len(expected), 4)
                    self.assertEqual(expected, lanes(new[-2], new_desc[new[-2] & 127]))
            self.assertEqual(changed, {'rigid_lit', 'rigid_reflect', 'skin_lit', 'skin_reflect'})


if __name__ == '__main__':
    unittest.main()
