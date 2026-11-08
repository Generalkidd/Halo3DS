"""Ensure unavailable/unsafe experiment combinations fail before producing a build."""
from pathlib import Path
import importlib.util
import itertools
import os
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'port/n3ds/build.py'


def identity_module():
    spec = importlib.util.spec_from_file_location('image_identity', ROOT / 'port/n3ds/tools/identity.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BuildControls(unittest.TestCase):
    def test_image_offset_checkpoints_distinguish_layout_inputs(self):
        identity = identity_module().checkpoint_identity
        baseline = identity('source-and-shader-hash', ['MODE=0'], '1234', {'compiler': 'one'})
        self.assertNotEqual(baseline, 0)
        self.assertEqual(baseline, identity('source-and-shader-hash', ['MODE=0'], '1234', {'compiler': 'one'}))
        for source, defines, epoch, dependencies in [
            ('changed-shader', ['MODE=0'], '1234', {'compiler': 'one'}),
            ('source-and-shader-hash', ['MODE=1'], '1234', {'compiler': 'one'}),
            ('source-and-shader-hash', ['MODE=0'], '5678', {'compiler': 'one'}),
            ('source-and-shader-hash', ['MODE=0'], '1234', {'compiler': 'two'}),
        ]:
            self.assertNotEqual(baseline, identity(source, defines, epoch, dependencies))

    def test_tree_fingerprint_uses_relative_paths_and_content(self):
        fingerprint = identity_module().tree_fingerprint
        with tempfile.TemporaryDirectory(prefix='halo-identity-tree-') as directory:
            one, two = (Path(directory) / name for name in ('one', 'two'))
            for path, names in ((one, ('a.h', 'b.h')), (two, ('b.h', 'a.h'))):
                path.mkdir()
                for name in names:
                    (path / name).write_bytes(name.encode())
            baseline = fingerprint(one)
            self.assertEqual(baseline, fingerprint(two))
            file = two / 'b.h'
            stamps = file.stat()
            os.utime(file, ns=(stamps.st_atime_ns, stamps.st_mtime_ns - 1_000_000_000))
            self.assertEqual(baseline, fingerprint(two))
            file.write_bytes(b'c.h')
            os.utime(file, ns=(stamps.st_atime_ns, stamps.st_mtime_ns))
            self.assertNotEqual(baseline, fingerprint(two))
            file.write_bytes(b'b.h')
            file.rename(two / 'c.h')
            self.assertNotEqual(baseline, fingerprint(two))

    def test_tree_fingerprint_follows_directory_aliases_and_rejects_cycles(self):
        fingerprint = identity_module().tree_fingerprint
        with tempfile.TemporaryDirectory(prefix='halo-identity-symlinks-') as directory:
            tree = Path(directory) / 'tree'
            target = Path(directory) / 'target'
            tree.mkdir()
            target.mkdir()
            try:
                (tree / 'alias').symlink_to(target, target_is_directory=True)
            except OSError as error:
                self.skipTest(f'Directory symlinks are unavailable: {error}')
            (target / 'value.h').write_bytes(b'1')
            baseline = fingerprint(tree)
            self.assertEqual(baseline['files'], 1)
            (target / 'value.h').write_bytes(b'2')
            self.assertNotEqual(baseline, fingerprint(tree))
            (target / 'cycle').symlink_to(tree, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, 'directory cycle'):
                fingerprint(tree)

    def test_invalid_cli_combinations_do_not_create_output(self):
        cases = [
            ['--geometry-source', 'cooked-unknown'],
            ['--vertex-layout', 'packed-unknown'],
            ['--algo-root-decode', 'candidate'],
            ['--algo-water-base', 'verify'],
            ['--algo-uv-outputs', 'verify'],
            ['--algo-boolean-selectors', 'verify'],
            ['--algo-midpoint-particles', 'candidate'],
            ['--allocation-profile'],
            ['--action-replay-fixture-ticks', '100'],
            ['--algo-prepared-view', 'candidate'],
            ['--algo-midpoint-quaternions', 'verify'],
            ['--midpoint-capture-tick', '-1'],
            ['--midpoint-capture-tick', '2147483648'],
            ['--midpoint-capture-tick', '300'],
            ['--present-60', '--midpoint-capture-tick', '300', '--dependency-trace'],
            ['--present-60', '--midpoint-capture-tick', '300', '--performance-profile'],
            ['--present-60', '--midpoint-capture-tick', '300', '--test-frames', '30'],
            ['--action-replay', 'replay', '--dependency-trace'],
            ['--action-replay', 'record', '--performance-profile'],
            ['--action-replay', 'record', '--test-frames', '30'],
            ['--action-replay', 'record', '--present-60', '--midpoint-capture-tick', '300'],
            ['--unused-texture-units'],
            ['--unused-texture-units', '--fixed-world-width', '320'],
            ['--unused-texture-units', '--fixed-world-width', '400', '--gpu-probe', '--skip-opaque-color-reads'],
            ['--unused-texture-units', '--fixed-world-width', '400', '--gpu-probe', '--framebuffer-reuse'],
        ]
        candidates = ('--framebuffer-reuse', '--skip-opaque-color-reads', '--unused-texture-units',
                      '--texture-vram-mirrors')
        for first, second in itertools.combinations(candidates, 2):
            cases.append(['--gpu-probe', '--fixed-world-width', '400', first, second])
        cases.extend([['--texture-vram-mirrors'], ['--texture-vram-mirrors', '--fixed-world-width', '320']])
        for option in ('--gpu-probe', *candidates):
            for extra in (['--action-replay', 'record'], ['--texture-lod-fixture', 'capture'],
                          ['--texture-lod-fixture', 'timing'], ['--vertex-layout', 'packed'],
                          ['--geometry-source', 'cooked'], ['--algo-root-pose', 'candidate']):
                cases.append([option, '--fixed-world-width', '400', *extra])
        with tempfile.TemporaryDirectory(prefix='halo-build-guards-') as directory:
            output = Path(directory) / 'must-not-exist'
            for args in cases:
                with self.subTest(args=args):
                    result = subprocess.run([sys.executable, str(BUILD), '--output', str(output), *args],
                                            capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn('error:', result.stderr)
                    self.assertFalse(output.exists())

    def test_texture_candidates_accept_independent_probe_and_combined_standalone(self):
        # Stop at the first missing dependency: this checks public option
        # compatibility without requiring SDK inputs or compiling a launcher.
        with tempfile.TemporaryDirectory(prefix='halo-texture-build-guards-') as directory:
            missing = Path(directory) / 'missing-devkitarm'
            output = Path(directory) / 'build'
            for option in ('--unused-texture-units', '--texture-vram-mirrors'):
                for extra in ([], ['--gpu-probe'], ['--skip-opaque-color-reads'], ['--framebuffer-reuse'],
                              ['--unused-texture-units', '--texture-vram-mirrors']):
                    with self.subTest(option=option, extra=extra):
                        result = subprocess.run([sys.executable, str(BUILD), '--output', str(output),
                                                 '--devkitarm', str(missing), option,
                                                 '--fixed-world-width', '400', *extra],
                                                capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, 2, result.stderr)
                        self.assertIn('Missing dependency:', result.stderr)
                        self.assertIn(str(missing), result.stderr)

    def test_direct_compiler_capture_guards(self):
        compiler = os.environ.get('CC') or shutil.which('cc')
        if not compiler:
            self.skipTest('A C compiler is required for header guards')
        common = ['HALO_N3DS_PRESENT_INTERPOLATION=1', 'HALO_N3DS_MISSION_TICKS=0']
        cases = [
            (common + ['HALO_N3DS_MIDPOINT_CAPTURE_TICK=300'], True),
            (common + ['HALO_N3DS_MIDPOINT_CAPTURE_TICK=-1'], False),
            (common + ['HALO_N3DS_MIDPOINT_CAPTURE_TICK=2147483648'], False),
            (common + ['HALO_N3DS_MIDPOINT_CAPTURE_TICK=TYPO'], False),
            (common + ['HALO_N3DS_MIDPOINT_CAPTURE_TICK=300', 'HALO_N3DS_PERFORMANCE_PROFILE=1'], False),
            (common + ['HALO_N3DS_MIDPOINT_CAPTURE_TICK=300', 'HALO_N3DS_DEPENDENCY_TRACE=1'], False),
            (common + ['HALO_N3DS_MIDPOINT_CAPTURE_TICK=300', 'HALO_N3DS_LOW_MEMORY_TEST=1'], False),
            (['HALO_N3DS_PRESENT_INTERPOLATION=1', 'HALO_N3DS_MISSION_TICKS=30',
              'HALO_N3DS_MIDPOINT_CAPTURE_TICK=300'], False),
        ]
        for option in ('HALO_N3DS_GPU_PROBE', 'HALO_N3DS_FRAMEBUFFER_REUSE', 'HALO_N3DS_OPAQUE_COLOR_READS',
                       'HALO_N3DS_UNUSED_TEXTURE_UNITS', 'HALO_N3DS_TEXTURE_VRAM_MIRRORS'):
            regular = [option+'=1', 'HALO_N3DS_MISSION_TICKS=0', 'HALO_N3DS_FIXED_WORLD_WIDTH=400']
            cases.append((regular, True))
            for extra in ('HALO_N3DS_ACTION_REPLAY=1', 'HALO_N3DS_TEXTURE_LOD_FIXTURE=1',
                          'HALO_N3DS_TEXTURE_COMPARE_FIXTURE=1', 'HALO_N3DS_VERTEX_LAYOUT=1'):
                cases.append((regular+[extra], False))
        unused = ['HALO_N3DS_UNUSED_TEXTURE_UNITS=1', 'HALO_N3DS_MISSION_TICKS=0',
                  'HALO_N3DS_FIXED_WORLD_WIDTH=400']
        cases.append((unused + ['HALO_N3DS_GPU_PROBE=1'], True))
        for companion in ('HALO_N3DS_OPAQUE_COLOR_READS=1', 'HALO_N3DS_FRAMEBUFFER_REUSE=1'):
            cases.append((unused + [companion], True))
            cases.append((unused + [companion, 'HALO_N3DS_GPU_PROBE=1'], False))
        mirrors = ['HALO_N3DS_TEXTURE_VRAM_MIRRORS=1', 'HALO_N3DS_MISSION_TICKS=0',
                   'HALO_N3DS_FIXED_WORLD_WIDTH=400']
        cases.append((mirrors + ['HALO_N3DS_GPU_PROBE=1'], True))
        for companion in ('HALO_N3DS_OPAQUE_COLOR_READS=1', 'HALO_N3DS_FRAMEBUFFER_REUSE=1',
                          'HALO_N3DS_UNUSED_TEXTURE_UNITS=1'):
            cases.append((mirrors + [companion], True))
            cases.append((mirrors + [companion, 'HALO_N3DS_GPU_PROBE=1'], False))
        for defines, valid in cases:
            with self.subTest(defines=defines):
                result = subprocess.run([compiler, '-x', 'c', '-fsyntax-only', '-', '-I',
                                         str(ROOT / 'port/n3ds/include'), *['-D' + d for d in defines]],
                                        input='#include "engine_renderer_features.h"\n',
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode == 0, valid, result.stderr)


if __name__ == '__main__':
    unittest.main()
