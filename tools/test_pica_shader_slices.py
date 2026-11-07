"""Check separately assembled PICA selector, skin blend, and water-base variants."""

import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest

from test_pica_uv_outputs import PORT, program


def assembled(directory, name):
    return program((directory / f'{name}.shbin').read_bytes())


def jmpu(word):
    return ((word >> 22) & 15, bool(word & 1), (word >> 10) & 4095)


def ordinary(word, descriptors):
    return word & ~127, descriptors[word & 127] & ((1 << 22) - 1)


class ShaderSlices(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        picasso = os.environ.get('PICASSO') or shutil.which('picasso')
        if not picasso:
            raise unittest.SkipTest('Set PICASSO to assemble the shader variants')
        cls.temp = tempfile.TemporaryDirectory(prefix='halo-pica-slices-')
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        cls.variants = {}
        for name, options in {
            'reference': {}, 'boolean': {'boolean_selectors': True}, 'mad': {'skin_mad': True},
            'water': {'water_base': True}, 'combined': {
                'uv_outputs': True, 'boolean_selectors': True, 'skin_mad': True,
                'water_base': True, 'packed_vertices': True,
            },
        }.items():
            directory = root / name
            directory.mkdir()
            cls.variants[name] = directory
            sys.path.insert(0, str(PORT))
            try:
                from tools import shaders
                shaders.generate(PORT, directory, picasso, **options)
            finally:
                sys.path.pop(0)

    def changed(self, variant):
        return {p.stem for p in self.variants['reference'].glob('*.shbin')
                if p.read_bytes() != (self.variants[variant] / p.name).read_bytes()}

    def test_boolean_slots_polarities_and_targets(self):
        expected = {
            'rigid_reflect': (2, ((0, True, 'diffuse_only'),)),
            'skin_reflect': (2, ((0, True, 'diffuse_only'),)),
            'water': (2, ((0, True, 'wave_ready'),)),
            'glass': (6, ((0, True, 'pose_ready'), (1, False, 'diffuse_color'))),
        }
        self.assertEqual(self.changed('boolean'), set(expected))
        for name, (saving, selectors) in expected.items():
            with self.subTest(program=name):
                before, _, _ = assembled(self.variants['reference'], name)
                after, _, _ = assembled(self.variants['boolean'], name)
                self.assertEqual(len(before) - len(after), saving)
                source = next(self.variants['boolean'].glob(f'{name}-*.v.pica')).read_text()
                addresses, instruction = {}, 0
                for line in source.splitlines():
                    line = line.partition(';')[0].strip()
                    if line.endswith(':'):
                        addresses[line[:-1]] = instruction
                    elif line and not line.startswith('.'):
                        instruction += 1
                self.assertEqual(instruction, len(after))
                self.assertEqual([jmpu(op) for op in after if op >> 26 == 45],
                                 [(slot, inverted, addresses[label]) for slot, inverted, label in selectors])

    def test_renderer_bool_locations_match_citro3d_contract(self):
        expected = {'MODEL_REFLECTION_BOOL': 0, 'WATER_WAVES_BOOL': 0, 'WATER_BASE_BOOL': 1,
                    'GLASS_RIGID_BOOL': 0, 'GLASS_DIFFUSE_BOOL': 1}
        # Pinned Citro3D accepts uniform locations (0x68 + bit), while PICA's
        # JMPU operand is the raw Boolean bit checked above.
        base = 0x68
        citro3d = Path(os.environ.get('CITRO3D', PORT.parents[1] / 'build/dependencies/citro3d-9f21cf7'))
        uniforms = citro3d / 'include/c3d/uniforms.h'
        if uniforms.is_file():
            setter = uniforms.read_text().split('void C3D_BoolUnifSet(', 1)[1]
            self.assertEqual(int(re.search(r'id\s*-=\s*(0x[0-9a-f]+);', setter)[1], 16), base)
        renderer = (PORT / 'src/engine_gpu_renderer.c').read_text()
        for name, slot in expected.items():
            location = re.search(rf'\b{name}\s*=\s*(0x[0-9a-f]+|\d+)', renderer)
            self.assertIsNotNone(location, name)
            self.assertEqual(int(location[1], 0), base + slot, name)

    def test_mad_preserves_weight_swizzles_and_first_product(self):
        expected = {'skin': 2, 'skin_lit': 2, 'skin_reflect': 2, 'skin_depth': 1, 'skin_chicago': 1}
        self.assertEqual(self.changed('mad'), set(expected))
        for name, count in expected.items():
            with self.subTest(program=name):
                old, old_desc, old_meta = assembled(self.variants['reference'], name)
                new, new_desc, new_meta = assembled(self.variants['mad'], name)
                self.assertEqual(new_meta, old_meta)
                self.assertEqual(len(old) - len(new), count)
                old_multiply = [
                    i for i, op in enumerate(old) if
                    (op >> 26, (op >> 21) & 31, (op >> 12) & 127, (op >> 7) & 31) == (8, 20, 3, 20)
                ]
                mad = [i for i, op in enumerate(new) if op >> 26 in range(56, 64)]
                self.assertEqual(len(old_multiply), count)
                self.assertEqual(len(mad), count)
                for i, j in zip(old_multiply, mad):
                    # The assembler encodes MAD's third source and descriptor
                    # in fields distinct from ordinary arithmetic.
                    word = new[j]
                    result = (old[i + 1] >> 21) & 31
                    self.assertEqual((word >> 24) & 31, result)
                    self.assertEqual(((word >> 17) & 31, (word >> 10) & 127, (word >> 5) & 31),
                                     (3, 20, result))
                    self.assertEqual(ordinary(old[i - 1], old_desc), ordinary(new[j - 1], new_desc))
                    old_operand = old_desc[old[i] & 127]
                    new_operand = new_desc[word & 31]
                    self.assertEqual(old_operand & ((1 << 22) - 1), new_operand & ((1 << 22) - 1))
                    # Third source is the unmodified first product, with
                    # the same identity swizzle as the reference ADD.
                    self.assertEqual((new_operand >> 22) & 511,
                                     (old_desc[old[i + 1] & 127] >> 4) & 511)

    def test_water_base_branches_after_projection_and_keeps_base_uv(self):
        self.assertEqual(self.changed('water'), {'water'})
        old, old_desc, old_meta = assembled(self.variants['reference'], 'water')
        new, new_desc, new_meta = assembled(self.variants['water'], 'water')
        self.assertEqual(old_meta, new_meta)
        self.assertEqual([ordinary(op, old_desc) for op in old[:6]],
                         [ordinary(op, new_desc) for op in new[:6]])
        self.assertEqual(new[6] >> 26, 45)
        self.assertEqual(jmpu(new[6]), (1, False, len(new) - 4))
        self.assertEqual(len(new), len(old) + 5)
        self.assertEqual(ordinary(old[-2], old_desc), ordinary(new[-2], new_desc))
        self.assertEqual(new[-1], old[-1])
        combined, _, _ = assembled(self.variants['combined'], 'water')
        self.assertEqual([jmpu(op)[:2] for op in combined if op >> 26 == 45],
                         [(1, False), (0, True)])


if __name__ == '__main__':
    unittest.main()
