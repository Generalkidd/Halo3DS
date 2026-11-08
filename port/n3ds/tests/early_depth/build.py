#!/usr/bin/env python3
"""Build private early-depth diagnostic from an explicitly chosen frozen mirror build.
No production source is changed. Compiler invocations are sequential (<=4 jobs).
"""
import argparse
import hashlib
import json
import os
import signal
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--base', type=Path, required=True, help='frozen matrix directory containing source/ and build-results.json')
p.add_argument('--reference-only', action='store_true', help='omit enabled early-depth phases; emulator harness validation only')
p.add_argument('--configuration', default='mirrors-probe')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
base, out = a.base.resolve(), a.output.resolve()
source, build = base / 'source', base / a.configuration
fixture = Path(__file__).resolve().parent
out.mkdir(parents=True, exist_ok=True)
records = json.loads((base / 'build-results.json').read_text())
record = next(r for r in records if r['name'] == a.configuration and r['status'] == 'pass')
commands = record['command']
def arg(name):
    return commands[commands.index(name) + 1]
def run(command, log, timeout):
    with log.open('w') as stream:
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = child.wait(timeout=timeout)
        except BaseException:
            try:
                os.killpg(child.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pass
            finally:
                # Driver exit does not prove its cc1/as/ld descendants exited.
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait()
            raise
    if code:
        print(log.read_text()[-10000:])
        raise subprocess.CalledProcessError(code, command)

# Reuse pinned checked enqueue and callback-retirement adapter; never a generic PPF fence.
assert '--texture-vram-mirrors' in commands
adapter = source / 'port/n3ds/src/engine_citro3d_frame.c'
assert 'queue->numEntries!=1' in adapter.read_text()
assert 'queue_callback_pending' in adapter.read_text()
fixture_code = (fixture / 'fixture.inc').read_text()
identity = hashlib.sha256(fixture_code.encode()).hexdigest()
frontend = 'port/n3ds/src/engine_frontend_boot.c'
renderer = 'port/n3ds/src/engine_gpu_renderer.c'
code = (source / frontend).read_text()
anchor = 'n3ds_log("STARTUP: rasterizer initialize");CHECK(rasterizer_initialize());'
assert code.count(anchor) == 1
code = code.replace(anchor, anchor + '\n    extern int n3ds_early_depth_fixture(void);\n    CHECK(n3ds_early_depth_fixture());\n    return 0;\n')
renderer_code = (source / renderer).read_text() + '\n#define ED_REFERENCE_ONLY ' + str(int(a.reference_only)) + '\n#define ED_SOURCE_SHA256 "' + identity + '"\n' + fixture_code
cc = str(Path(arg('--devkitarm')) / 'bin/arm-none-eabi-gcc')
main = 'port/n3ds/src/engine_main.c'
main_code = (source / main).read_text()
assert main_code.count('while (result && aptMainLoop())') == 1
main_code = main_code.replace('while (result && aptMainLoop())', 'while (0 && result && aptMainLoop())')
objects = {}
for rel, text, compiler in [(frontend, code, arg('--clang')), (renderer, renderer_code, cc), (main, main_code, cc)]:
    scratch = out / Path(rel).name
    scratch.write_text(text)
    obj = scratch.with_suffix('.o')
    args = shlex.split((build / 'compile' / (rel + '.rsp')).read_text())
    args[args.index('-MF') + 1] = str(scratch.with_suffix('.d'))
    args[args.index('-c') + 1] = str(scratch)
    args[args.index('-o') + 1] = str(obj)
    args += ['-I', str(Path(arg('--citro3d')) / 'source')]
    run([compiler, *args], scratch.with_suffix('.log'), 60)
    objects[str(build / 'objects' / (rel + '.o'))] = str(obj)
args = shlex.split((build / 'link.rsp').read_text())
for old, new in objects.items():
    args[args.index(old)] = new
args[args.index('-Wl,-Map=' + str(build / 'Halo3DS.map'))] = '-Wl,-Map=' + str(out / 'early-depth.map')
args[args.index('-o') + 1] = str(out / 'early-depth.elf')
run([cc, *args], out / 'link.log', 60)
run([arg('--packer'), str(out / 'early-depth.elf'), str(out / 'early-depth.3dsx')], out / 'pack.log', 15)
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
(out / 'build.json').write_text(json.dumps(dict(
    base=str(base), configuration=a.configuration, base_binary_sha256=record['binary_sha256'],
    reference_only=a.reference_only, fixture_sha256=identity, builder_sha256=sha(Path(__file__).resolve()),
    inventory_sha256=sha(base / 'source-inventory.json'), adapter_sha256=sha(adapter),
    binary_sha256=sha(out / 'early-depth.3dsx'), elf_sha256=sha(out / 'early-depth.elf'),
    runtime_validation='NOT RUN; native console required', jobs=1), indent=2) + '\n')
print(out / 'early-depth.3dsx')
