import json
from pathlib import Path
import struct
import tempfile
import unittest

from compare_midpoint_capture import compare


def pair(directory, capture_id, change_depth=False):
    prefix = f'halo-pair-{capture_id}-300'
    manifest = directory / f'{prefix}-complete.json'
    manifest.write_text(json.dumps(dict(schema=1, capture_id=capture_id, tick=300, width=240, height=400,
        rgba_bytes=384000, d24s8_bytes=384000, camera_bytes=260, views=['midpoint', 'regular'])))
    for view in ('midpoint', 'regular'):
        (directory / f'{prefix}-{view}.camera').write_bytes(
            b'H2DSCAM1' + struct.pack('<4I', 300, view == 'midpoint', 240, 400) + bytes(236))
        (directory / f'{prefix}-{view}.rgba').write_bytes(bytes(384000))
        first_byte = b'\x01' if change_depth and view == 'midpoint' else b'\x00'
        (directory / f'{prefix}-{view}.d24s8').write_bytes(first_byte + bytes(383999))
    return manifest


class MidpointCaptureTests(unittest.TestCase):
    def test_reports_raw_depth_difference_and_rejects_input_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            reference = pair(path, '0'*16)
            candidate = pair(path, '1'*16, change_depth=True)
            result = compare(reference, candidate)
            self.assertFalse(result['exact_match'])
            self.assertEqual(result['views']['midpoint']['d24s8']['different_words'], 1)
            self.assertEqual(result['views']['regular']['d24s8']['different_words'], 0)
            camera = path / f'halo-pair-{"1"*16}-300-regular.camera'
            camera.write_bytes(camera.read_bytes()[:-1]+b'\x01')
            with self.assertRaisesRegex(ValueError, 'camera'):
                compare(reference, candidate)

    def test_requires_all_payloads(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            reference = pair(path, '0'*16)
            candidate = pair(path, '1'*16)
            (path / f'halo-pair-{"1"*16}-300-regular.d24s8').unlink()
            with self.assertRaises(FileNotFoundError):
                compare(reference, candidate)
