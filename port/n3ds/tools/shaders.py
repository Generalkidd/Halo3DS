"""Fresh PICA headers and linked-byte validation for incremental native builds."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess


UV_OUTPUT_PROGRAMS = {
    'rigid_lit': 'texcoord',
    'skin_lit': 'texcoord',
    'rigid_reflect': 'maps',
    'skin_reflect': 'maps',
}
BOOLEAN_PROGRAMS = {'rigid_reflect', 'skin_reflect', 'water', 'glass'}
SKIN_MAD_PROGRAMS = {
    'skin': (1, 2), 'skin_lit': (1, 2), 'skin_reflect': (1, 2),
    'skin_depth': (1,), 'skin_chicago': (1,),
}


def _replace_once(source, old, new, description):
    if source.count(old) != 1:
        raise ValueError(f'Expected one {description} sequence')
    return source.replace(old, new)


def _uv_output_variant(source, target):
    # Both halves write the same products of the x/y lanes. Match the exact
    # pair so independently scaled material outputs cannot enter this slice.
    original = f'    mul {target}.xy, options, v2\n    mul {target}.zw, options.xyxy, v2.xyxy\n'
    combined = f'    mul {target}, options.xyxy, v2.xyxy\n'
    return _replace_once(source, original, combined, f'UV output instruction pair for {target}')


def _boolean_variant(source, name):
    if name in {'rigid_reflect', 'skin_reflect'}:
        source = _replace_once(source, '.proc main\n',
                               '.alias reflection_enabled b0\n.proc main\n', 'main entry')
        return _replace_once(source,
                             '    mov r3.y, options.w\n    cmp constants.x, ge, ge, r3.y\n'
                             '    jmpc cmp.x, diffuse_only\n',
                             '    jmpu !reflection_enabled, diffuse_only\n', 'model reflection selector')
    if name == 'water':
        source = _replace_once(source, '.fvec wave_switch\n',
                               '.alias waves_enabled b0\n', 'water wave uniform')
        return _replace_once(source,
                             '    mov r1.w, wave_switch.x\n    cmp constants.x, ge, ge, r1.w\n'
                             '    jmpc cmp.x, wave_ready\n',
                             '    jmpu !waves_enabled, wave_ready\n', 'water wave selector')
    source = _replace_once(source, '.proc main\n',
                           '.alias rigid_enabled b0\n.alias diffuse_enabled b1\n.proc main\n', 'main entry')
    source = _replace_once(source,
                           '    mov r1.w, options.y\n    cmp constants.x, lt, lt, r1.w\n'
                           '    jmpc cmp.x, rigid_pose\n    cmp r1.w, eq, eq, r1.w\n'
                           '    jmpc cmp.x, pose_ready\nrigid_pose:\n',
                           '    jmpu !rigid_enabled, pose_ready\n', 'glass rigid pose selector')
    return _replace_once(source,
                         '    mov r1.w, options.x\n    cmp constants.x, lt, lt, r1.w\n'
                         '    jmpc cmp.x, diffuse_color\n',
                         '    jmpu diffuse_enabled, diffuse_color\n', 'glass diffuse selector')


def _skin_mad_variant(source, name):
    # Preserve the first product and all DP3/DP4, weight and degeneracy tests.
    # Apply the position change to color, depth/cloak and Chicago together.
    for register in SKIN_MAD_PROGRAMS[name]:
        result = f'r{register}'
        source = _replace_once(source,
                               f'    mul {result}.xyz, v3.z, {result}\n'
                               f'    mul r4.xyz, v3.w, r4\n    add {result}.xyz, {result}, r4\n',
                               f'    mul {result}.xyz, v3.z, {result}\n'
                               f'    mad {result}.xyz, v3.w, r4, {result}\n',
                               f'{name} weighted blend for {result}')
    return source


def _water_base_variant(source):
    source = _replace_once(source, '.proc main\n',
                           '.alias base_enabled b1\n.proc main\n', 'main entry')
    source = _replace_once(source, '    dp4 position.w, projection[3], r0\n',
                           '    dp4 position.w, projection[3], r0\n'
                           '    jmpu base_enabled, base_only\n', 'water projection tail')
    # The multiply TEV pass reads texture 1 only. Keep all enabled output
    # lanes initialized and preserve position/texture 1 inputs exactly.
    return _replace_once(source, '    mov maps, v2\n    end\n.end\n',
                         '    mov maps, v2\n    end\nbase_only:\n'
                         '    mov color, constants.yyyy\n    mov cube, constants.xxyx\n'
                         '    mov maps, v2\n    end\n.end\n', 'water output tail')


def _instruction_count(data):
    magic, entries = struct.unpack_from('<4sI', data)
    if magic != b'DVLB':
        raise ValueError('Invalid assembled shader header')
    program = (2 + entries) * 4
    magic, _, code_offset, count = struct.unpack_from('<4sIII', data, program)
    if magic != b'DVLP' or program + code_offset + count * 4 > len(data):
        raise ValueError('Invalid assembled shader program')
    return count


def generate(port, out, picasso, *, uv_outputs=False, boolean_selectors=False,
             skin_mad=False, water_base=False, packed_vertices=False):
    if packed_vertices:
        from .packed_skin import packed_skin_source
    shaders = {}
    for source in sorted((port/'shaders').glob('*.v.pica')):
        name = source.name[:-7]
        symbol = 'texture_test_shader' if name == 'texture_test' else 'engine_' + name + '_shader'
        binary = out / (name + '.shbin')
        assembly_source = source
        original = text = source.read_text()
        transforms = []
        if uv_outputs and name in UV_OUTPUT_PROGRAMS:
            text = _uv_output_variant(text, UV_OUTPUT_PROGRAMS[name])
            transforms.append('uv-outputs')
        if boolean_selectors and name in BOOLEAN_PROGRAMS:
            text = _boolean_variant(text, name)
            transforms.append('boolean-selectors')
        if skin_mad and name in SKIN_MAD_PROGRAMS:
            text = _skin_mad_variant(text, name)
            transforms.append('skin-mad')
        if water_base and name == 'water':
            text = _water_base_variant(text)
            transforms.append('water-base')
        if packed_vertices:
            packed = packed_skin_source(text, name)
            if packed != text:
                transforms.append('packed-vertices')
                text = packed
        if text != original:
            assembly_source = out / (name + '-' + '-'.join(transforms) + '.v.pica')
            assembly_source.write_text(text)
        subprocess.run([str(picasso), '-o', str(binary), str(assembly_source)], check=True)
        data = binary.read_bytes()
        (out / (symbol + '.h')).write_text(
            'static const unsigned char ' + symbol + '[] __attribute__((aligned(4))) = {'
            + ','.join(hex(x) for x in data) + '};\n')
        shaders[symbol] = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                           'binary': str(binary), 'sha256': hashlib.sha256(data).hexdigest()}
        if name in UV_OUTPUT_PROGRAMS or name in BOOLEAN_PROGRAMS or name in SKIN_MAD_PROGRAMS:
            shaders[symbol]['instruction_count'] = _instruction_count(data)
        if assembly_source != source:
            shaders[symbol]['transforms'] = transforms
            shaders[symbol]['assembly_source'] = str(assembly_source)
            shaders[symbol]['assembly_source_sha256'] = hashlib.sha256(assembly_source.read_bytes()).hexdigest()
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
