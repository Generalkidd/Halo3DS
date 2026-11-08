#!/usr/bin/env python3
"""Run and inspect the native 400x240 texture LOD fixture in private Azahar profiles."""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import subprocess

import emulator_smoke
from emulator_texture_compare import percentiles


MODES = {0: '64 RGB8 single', 1: '64 RGB8 mips', 2: '32 RGB8 mips'}
MODE_SHAPES = {0: (64, 1), 1: (64, 4), 2: (32, 3)}
WIDTH, HEIGHT = 400, 240
CAPTURE_FRAMES, TIMING_FRAMES, CAPTURE_FPS = 60, 240, 30
SNAPSHOTS = {0: 'Near approach', 30: 'Distant repeating surfaces', 59: 'Alternate close perspective'}
PREFIX = 'halo-texture-lod'


def asset_manifest(text, mode):
    shape, levels = MODE_SHAPES[mode]
    size = sum(3*(shape >> level)**2 for level in range(levels))
    assets = []
    for matched in re.finditer(r'TEXTURE LOD ASSET: mode=(\d+) label=(.*?) bitmap_offset=([0-9a-fA-F]{8})'
            r' source_mip=(\d+) size=(\d+) levels=(\d+) resident_bytes=(\d+) chain_fnv=([0-9a-fA-F]{8})\b', text):
        native_mode, label, offset, mip, side, count, bytes_used, digest = matched.groups()
        if tuple(map(int, (native_mode, side, count, bytes_used))) != (mode, shape, levels, size):
            raise ValueError(f'Wrong native texture shape or payload in asset {label}')
        assets.append({'label': label, 'bitmap_offset': offset, 'source_mip': int(mip),
            'size': shape, 'levels': levels, 'resident_bytes': size, 'chain_fnv': digest})
    if len(assets) != 3 or len({asset['bitmap_offset'] for asset in assets}) != 3:
        raise ValueError('Expected three distinct validated retail texture assets')
    packs = re.findall(r'TEXTURE LOD PACK: mode=(\d+) textures=(\d+) resident_bytes=(\d+)\b', text)
    if len(packs) != 1 or tuple(map(int, packs[0])) != (mode, 3, 3*size):
        raise ValueError('Missing or inconsistent validated texture pack total')
    evidence = {'assets': assets, 'resident_bytes': 3*size}
    allocation = re.findall(r'TEXTURE LOD LINEAR: mode=(\d+) allocation_bytes=(\d+)\b', text)
    if allocation:
        if len(allocation) != 1 or int(allocation[0][0]) != mode or int(allocation[0][1]) < 3*size:
            raise ValueError('Inconsistent linear allocator byte count')
        evidence['linear_allocation_bytes'] = int(allocation[0][1])
    return evidence


def capture_manifest(sd):
    files = [sd / f'{PREFIX}-{frame:03d}.rgba' for frame in range(CAPTURE_FRAMES)]
    if set(sd.glob(f'{PREFIX}-*.rgba')) != set(files):
        raise ValueError(f'Expected exactly {CAPTURE_FRAMES} indexed readbacks')
    result = []
    for frame, path in enumerate(files):
        if path.stat().st_size != WIDTH * HEIGHT * 4:
            raise ValueError(f'Incorrect 400x240 readback size: {path.name}')
        result.append({'frame': frame, 'path': str(path), 'sha256': emulator_smoke.sha(path)})
    if len({image['sha256'] for image in result}) != len(result):
        raise ValueError('Motion readbacks contain repeated frames')
    return result


def timing_manifest(sd):
    path = sd / f'{PREFIX}.csv'
    with path.open(newline='') as stream:
        rows = csv.reader(stream)
        if next(rows, None) != ['frame', 'work_us', 'interval_us']:
            raise ValueError('Unexpected timing CSV columns')
        samples = [tuple(map(int, row)) for row in rows]
    if len(samples) != TIMING_FRAMES or any(len(row) != 3 for row in samples):
        raise ValueError(f'Expected {TIMING_FRAMES} timing samples')
    start = samples[0][0]
    if start not in (0, 1) or any(frame != start+i or not 0 <= work <= interval
            for i, (frame, work, interval) in enumerate(samples)):
        raise ValueError('Timing CSV has inconsistent frame indices or intervals')
    return {'path': str(path), 'sha256': emulator_smoke.sha(path), 'samples': len(samples),
        'work_us': percentiles(work for _, work, _ in samples),
        'interval_us': percentiles(interval for _, _, interval in samples),
        'interval_ge_50ms': sum(interval >= 50_000 for _, _, interval in samples),
        'interval_ge_100ms': sum(interval >= 100_000 for _, _, interval in samples)}


def run_case(args, mode, label, pack, mode_file):
    out = args.output / label
    result = {'label': label, 'mode': mode, 'title': MODES[mode], 'profile': str(out)}
    try:
        status = emulator_smoke.main(['--emulator', str(args.emulator), '--binary', str(args.binary),
            '--assets', str(args.assets), '--dsp', str(args.dsp), '--output', str(out),
            '--timeout', str(args.timeout), '--renderer', args.renderer,
            '--sd-file', str(pack), '--sd-file', str(mode_file),
            '--require-log', f'TEXTURE LOD FIXTURE: pass mode={mode}'])
        run = json.loads((out / 'result.json').read_text())
        result.update(run_status=run['status'], renderer=run['renderer_observed'],
            cleanup_complete=run['cleanup_complete'], binary_sha256=run['binary_sha256'],
            emulator_sha256=run['emulator_sha256'], sd_inputs=run['sd_inputs'])
        if status or run['status'] != 'pass' or not run['cleanup_complete']:
            raise ValueError(f'Azahar run did not complete: {run["status"]}')
        if run['renderer_observed'] != args.renderer:
            raise ValueError(f'Unverified or unexpected renderer: {run["renderer_observed"]}')
        sd = out / 'data/azahar-emu/sdmc'
        log = sd / 'halo-source-engine.log'
        result['log'] = str(log)
        text = log.read_text(errors='replace')
        result['lod_log'] = [line for line in text.splitlines() if 'TEXTURE LOD' in line]
        result['payload'] = asset_manifest(text, mode)
        bindings = re.findall(r'NATIVE SAMPLERS: requests=(\d+) descriptor_updates=(\d+)\b', text)
        minimum = (CAPTURE_FRAMES if args.phase == 'capture' else TIMING_FRAMES)*3
        if len(bindings) != 1 or not minimum <= int(bindings[0][1]) <= int(bindings[0][0]):
            raise ValueError('Missing or insufficient native texture bind evidence')
        requests, updates = map(int, bindings[0])
        result['bindings'] = {'requests': requests, 'descriptor_updates': updates,
                              'expected_surface_binds': minimum}
        if args.phase == 'capture':
            if (sd / f'{PREFIX}.csv').exists():
                raise ValueError('Capture fixture produced a timing CSV')
            result['captures'] = capture_manifest(sd)
        else:
            if any(sd.glob(f'{PREFIX}-*.rgba')):
                raise ValueError('Timing fixture produced GPU readbacks')
            result['timing'] = timing_manifest(sd)
        result['status'] = 'pass'
    except (OSError, KeyError, ValueError, SystemExit) as error:
        result.update(status='fail', error=str(error))
    return result


def readback_image(Image, capture):
    raw = Path(capture['path']).read_bytes()
    # PICA target readback has swapped dimensions and little-endian RGBA8 bytes.
    return Image.frombytes('RGB', (HEIGHT, WIDTH), raw, 'raw', 'XBGR').transpose(Image.Transpose.ROTATE_90)


def encode_clip(ffmpeg, path, frames, width, height):
    data = b''.join(frame.tobytes() for frame in frames)
    command = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-n', '-f', 'rawvideo',
        '-pixel_format', 'rgb24', '-video_size', f'{width}x{height}', '-framerate', str(CAPTURE_FPS),
        '-i', 'pipe:0', '-an', '-c:v', 'libx264', '-preset', 'fast', '-crf', '15',
        '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(path)]
    subprocess.run(command, input=data, check=True, timeout=60)


def render_visuals(cases, out, ffmpeg):
    from PIL import Image, ImageDraw, ImageFont

    def font(size, bold=False):
        name = 'Arial Bold.ttf' if bold else 'Arial.ttf'
        try:
            return ImageFont.truetype(f'/System/Library/Fonts/Supplemental/{name}', size)
        except OSError:
            return ImageFont.load_default()

    out.mkdir()
    background, white, muted, accent = '#111821', '#f2f5fa', '#b2bfd0', '#82c9ff'
    title_font, label_font = font(28, True), font(18, True)
    small_font = font(15)
    modes = [case['mode'] for case in cases]
    sequences, screenshots, clips = {}, {}, {}
    for case in cases:
        mode = case['mode']
        frames = [readback_image(Image, capture) for capture in case['captures']]
        sequences[mode] = frames
        for index in SNAPSHOTS:
            path = out / f'{chr(65+mode)}-{index:03d}.png'
            frames[index].save(path)
            screenshots[f'{mode}:{index}'] = str(path)
        clip = out / f'{chr(65+mode)}-motion.mp4'
        encode_clip(ffmpeg, clip, frames, WIDTH, HEIGHT)
        clips[str(mode)] = str(clip)

    margin, gutter, row_gap, top = 28, 16, 33, 126
    width = 2*margin + len(modes)*WIDTH + (len(modes)-1)*gutter
    height = top + len(SNAPSHOTS)*(HEIGHT+row_gap) + 5
    sheet = Image.new('RGB', (width, height), background)
    draw = ImageDraw.Draw(sheet)
    draw.text((margin, 20), 'Texture LOD comparison', font=title_font, fill=white)
    draw.text((margin, 60), 'Native 400 × 240 GPU readback · Fixed camera path', font=small_font, fill=muted)
    for column, mode in enumerate(modes):
        x = margin + column*(WIDTH+gutter)
        draw.text((x, 91), f'{chr(65+mode)}   {MODES[mode]}', font=label_font, fill=accent)
        for row, (index, label) in enumerate(SNAPSHOTS.items()):
            y = top + row*(HEIGHT+row_gap)
            sheet.paste(sequences[mode][index], (x, y))
            draw.text((x, y+HEIGHT+8), f'{index:02d}   {label}', font=small_font, fill=muted)
    contact = out / 'texture-lod-contact.png'
    sheet.save(contact)

    # Synchronised, native-size columns make motion shimmer inspectable at the
    # same camera index. PNGs above retain the original decoded RGB values.
    clip_height = HEIGHT+40
    clip_header = Image.new('RGB', (width, clip_height), background)
    draw = ImageDraw.Draw(clip_header)
    for column, mode in enumerate(modes):
        draw.text((margin+column*(WIDTH+gutter), 9), f'{chr(65+mode)}   {MODES[mode]}',
                  font=label_font, fill=accent)

    def comparison_frames():
        for index in range(CAPTURE_FRAMES):
            frame = clip_header.copy()
            for column, mode in enumerate(modes):
                frame.paste(sequences[mode][index], (margin+column*(WIDTH+gutter), 34))
            yield frame

    motion = out / 'texture-lod-motion.mp4'
    encode_clip(ffmpeg, motion, comparison_frames(), width, clip_height)
    return {'contact_sheet': str(contact), 'motion_comparison': str(motion), 'clips': clips,
            'screenshots': screenshots, 'frames': CAPTURE_FRAMES, 'fps': CAPTURE_FPS}


def timing_rounds(cases, modes):
    rounds = []
    for start in range(0, len(cases), len(modes)):
        group = cases[start:start+len(modes)]
        reference = next(case for case in group if case['mode'] == modes[0])
        differences = {}
        for candidate in group:
            if candidate is reference:
                continue
            before, after = reference['timing'], candidate['timing']
            differences[str(candidate['mode'])] = {metric: {
                name: after[metric][name] - value for name, value in before[metric].items()}
                for metric in ('work_us', 'interval_us')}
        rounds.append({'order': [case['mode'] for case in group], 'reference': reference['mode'],
            'delta_us': differences})
    return rounds


def source_identity(case):
    # The 32 tier starts one authored source mip below the 64 tier.
    reduction = int(case['mode'] == 2)
    return [(asset['label'], asset['bitmap_offset'], asset['source_mip']-reduction)
            for asset in case['payload']['assets']]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('emulator', 'binary', 'assets', 'dsp', 'pack', 'output'):
        parser.add_argument(f'--{name}', type=Path, required=True)
    parser.add_argument('--phase', choices=('capture', 'timing'), required=True)
    parser.add_argument('--renderer', choices=('vulkan', 'opengl', 'software'), default='vulkan')
    parser.add_argument('--timeout', type=emulator_smoke.timeout_seconds, default=120)
    parser.add_argument('--modes', type=int, choices=MODES, nargs='+', default=list(MODES))
    parser.add_argument('--rounds', type=int, choices=(1, 2, 3), default=1)
    parser.add_argument('--ffmpeg', default='ffmpeg', help='FFmpeg with libx264 for capture previews')
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists():
        parser.error('Use a fresh output directory')
    if len(set(args.modes)) != len(args.modes):
        parser.error('Each selected mode must appear once')
    if args.phase == 'capture':
        if args.rounds != 1:
            parser.error('Capture uses one round')
        try:
            import PIL  # noqa: F401
        except ImportError:
            parser.error('Capture previews require Pillow')
        if not shutil.which(args.ffmpeg):
            parser.error('Capture previews require FFmpeg with libx264')
    for path in (args.emulator, args.binary, args.pack, args.dsp):
        if not path.is_file():
            parser.error(f'Missing input: {path}')

    inputs = args.output / 'inputs'
    inputs.mkdir(parents=True)
    pack = inputs / f'{PREFIX}.ntxm'
    shutil.copy2(args.pack, pack)
    mode_files = {}
    for mode in args.modes:
        mode_dir = inputs / str(mode)
        mode_dir.mkdir()
        mode_files[mode] = mode_dir / f'{PREFIX}-mode.txt'
        mode_files[mode].write_text(str(mode), encoding='ascii')

    receipt = {'scope': 'Native controlled corridor in Azahar; preview clips do not measure display pacing',
               'phase': args.phase, 'status': 'running', 'cases': []}
    path = args.output / 'comparison.json'
    for round_number in range(args.rounds):
        offset = round_number % len(args.modes)
        for mode in args.modes[offset:]+args.modes[:offset]:
            label = f'{chr(65+mode)}{round_number+1}'
            result = run_case(args, mode, label, pack, mode_files[mode])
            if result['status'] == 'pass' and receipt['cases']:
                reference = receipt['cases'][0]
                if result['binary_sha256'] != reference['binary_sha256'] or \
                   source_identity(result) != source_identity(reference):
                    result.update(status='fail', error='Variant changed the fixture binary or source textures')
            receipt['cases'].append(result)
            if result['status'] != 'pass':
                receipt['status'] = 'fail'
            path.write_text(json.dumps(receipt, indent=2)+'\n')
            if receipt['status'] == 'fail':
                print(path)
                return 1
    try:
        if args.phase == 'capture':
            receipt['visuals'] = render_visuals(receipt['cases'], args.output / 'visuals', args.ffmpeg)
        else:
            receipt['paired_rounds'] = timing_rounds(receipt['cases'], args.modes)
        receipt['status'] = 'pass'
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        receipt.update(status='fail', error=str(error))
    path.write_text(json.dumps(receipt, indent=2)+'\n')
    print(path)
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
