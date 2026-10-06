"""Fresh PICA headers and linked-byte validation for incremental native builds."""
from pathlib import Path
import hashlib
import json
import re
import subprocess


def generate(port, out, picasso):
    shaders = {}
    for source in sorted((port/'shaders').glob('*.v.pica')):
        name = source.name[:-7]
        symbol = 'texture_test_shader' if name == 'texture_test' else 'engine_' + name + '_shader'
        binary = out / (name + '.shbin')
        subprocess.run([str(picasso), '-o', str(binary), str(source)], check=True)
        data = binary.read_bytes()
        (out / (symbol + '.h')).write_text(
            'static const unsigned char ' + symbol + '[] __attribute__((aligned(4))) = {'
            + ','.join(hex(x) for x in data) + '};\n')
        shaders[symbol] = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                           'binary': str(binary), 'sha256': hashlib.sha256(data).hexdigest()}
    return shaders


def verify_linked(shaders, elf, old_directories, report):
    image = elf.read_bytes()
    result = {}
    for symbol, entry in shaders.items():
        fresh = Path(entry['binary']).read_bytes()
        result[symbol] = dict(entry, linked=fresh in image)
        for folder in old_directories:
            header = folder / (symbol + '.h')
            if not header.exists():
                continue
            text = header.read_text().split('{', 1)[1].split('}', 1)[0]
            stale = bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', text))
            if stale and stale != fresh:
                assert stale not in image, 'Stale shader linked: ' + str(header)
    assert result['engine_scene_shader']['linked'], 'Correct loading shader absent from executable'
    report.write_text(json.dumps(result, indent=2))
    return result
