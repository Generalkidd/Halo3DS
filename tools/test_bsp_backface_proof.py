"""Check exact ordered block evidence and conservative uncertainty cases."""

from fractions import Fraction
from pathlib import Path
import struct
import tempfile
import unittest

import bsp_backface_proof as proof


def cook(positions, indices, eye=None):
    vertices = b''.join(proof.VERTEX.pack(*position, 0, 0, 0, 0, 0) for position in positions)
    return proof.cook(vertices, struct.pack(f'<{len(indices)}H', *indices), eye)


class BSPBackfaceProof(unittest.TestCase):
    def test_ordered_blocks_and_coplanar_boundary(self):
        positions = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        indices = [0, 1, 2] * 33
        for z, expected in [(1, 'positive'), (-1, 'negative'), (0, 'uncertain')]:
            blocks = cook(positions, indices, (0, 0, z))['blocks']
            self.assertEqual([(b['first_index'], b['index_count']) for b in blocks], [(0, 96), (96, 3)])
            for block in blocks:
                self.assertEqual(block['normal_bounds'], [['0', '0'], ['0', '0'], ['1', '1']])
                self.assertEqual(block['normal_cone']['cosine_squared_lower_bound'], '1')
                self.assertEqual(block['ideal_camera_side'], expected)
                self.assertEqual(block['ideal_side_bounds'], [str(z), str(z)])

    def test_broad_and_degenerate_blocks_stay_uncertain(self):
        positions = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        for other in [(0, 2, 1), (0, 0, 1)]:
            block, = cook(positions, [0, 1, 2, *other], (0, 0, 1))['blocks']
            self.assertIsNone(block['normal_cone'])
            self.assertEqual(block['ideal_camera_side'], 'uncertain')

    def test_exact_normal_cone_and_source_hashes(self):
        positions = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 1, .5)]
        result = cook(positions, [0, 1, 2, 0, 1, 3], (0, 0, 2))
        block, = result['blocks']
        self.assertEqual(block['normal_cone']['cosine_squared_lower_bound'], '4/5')
        self.assertEqual(block['normal_bounds'], [['0', '0'], ['-1/2', '0'], ['1', '1']])
        # Independent extrema over all stored geometry corners and normals.
        lower, upper = map(Fraction, block['ideal_side_bounds'])
        for nx, ny, nz in [(0, 0, 1), (0, Fraction(-1, 2), 1)]:
            for x, y, z in positions:
                side = nx * (-x) + ny * (-y) + nz * (2 - z)
                self.assertLessEqual(lower, side)
                self.assertGreaterEqual(upper, side)
        reversed_result = cook(positions, [0, 1, 3, 0, 1, 2], (0, 0, 2))
        self.assertEqual(result['source']['vertices_sha256'], reversed_result['source']['vertices_sha256'])
        self.assertNotEqual(result['source']['indices_sha256'], reversed_result['source']['indices_sha256'])

    def test_camera_record_and_invalid_geometry(self):
        payload = bytearray(260)
        payload[:8] = b'H2DSCAM1'
        struct.pack_into('<6I', payload, 8, 30, 1, 240, 400, 3, 0)
        struct.pack_into('<3f', payload, 21 * 4, .125, -.25, 3.5)
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / 'test.camera'
            path.write_bytes(payload)
            self.assertEqual(proof.read_eye(path), (Fraction(1, 8), Fraction(-1, 4), Fraction(7, 2)))
        with self.assertRaisesRegex(ValueError, 'outside'):
            cook([(0, 0, 0)], [0, 1, 0])
        with self.assertRaisesRegex(ValueError, 'non-finite'):
            cook([(float('nan'), 0, 0)], [0, 0, 0])


if __name__ == '__main__':
    unittest.main()
