"""Build the playable Halo3DS frontend from source. Python 3.10+.

Run --help for dependency paths and diagnostic options. No previous objects,
private build manifests, game assets or desktop platform ports are required.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from tools.compat import prepare
from tools.shaders import generate, verify_linked
from tools.identity import canonical_digest, checkpoint_identity, external_build_inputs, file_sha256 as sha

PORT = Path(__file__).resolve().parent
ROOT = PORT.parents[1]
COMMON = ['-march=armv6k', '-mfloat-abi=hard', '-mfpu=vfp', '-marm', '-mno-unaligned-access',
          '-mtp=soft', '-O2', '-g', '-ffunction-sections', '-fdata-sections', '-fno-strict-aliasing', '-fwrapv']
# Select the devkitARM newlib ABI in both compilers. Clang does not supply the
# devkit compiler identity macros used by sys/config.h; without dynamic reent,
# inline libc consumers can accidentally use the global rather than thread state.
NEWLIB_ABI = ['-D__DYNAMIC_REENT__=', '-DMALLOC_ALIGNMENT_THRESHOLD=32',
              '-D_READ_WRITE_RETURN_TYPE=_ssize_t', '-D_READ_WRITE_BUFSIZE_TYPE=size_t']
WRAPS = ['free', 'debug_free', 'datum_new', 'data_iterator_new', 'data_verify', 'realloc', 'write']
CITRO3D_FRAME_REVISION = '9f21cf7b380ce6f9e01a0420f19f0763e5443ca7'
CITRO3D_FRAME_SOURCE = {
    'renderqueue.c': '8face2edf47bd759f6ed7cb5dd339b6d0e959e3424bd6c6ab556144151436b93',
    'internal.h': 'ab1efd417fcb2247932c800c8080108c7f8a9443ad2d1f7d1f2597b718c10e83',
}
CITRO3D_COLOR_READ_SOURCE = {
    **CITRO3D_FRAME_SOURCE,
    'base.c': '291ecfabef7a948f015f2746cdd34b8f0947352d6e00b40d4384449ef59c413e',
    'drawElements.c': 'dd5cb551ec7dbc88d3ceb7ad88301f0198476225549ec2bb511fb61a24b4bcbe',
    'drawArrays.c': 'da8544e074e74d8d56cbffd200bd9df56e9d3164afa1c6f505c3af4ca8ee1904',
    'immediate.c': '6e562fd4e352b74cac5323846616fbe601d358150888e7ce5d7c203ca7c91939',
    'framebuffer.c': '6641661f4353f675c93cc8516a18a09d2b75f832aa443c773dc17ac06fae51f6',
    'effect.c': 'c32e5972aed2d7b99c542dae571f085e9dc046b7a0c3d2f12e01b64333a0ab65',
}
CITRO3D_COLOR_READ_WRAPS = ('C3D_DrawElements', 'C3Di_UpdateContext')
# Stable, independent renderer mode identities. The RGBA8 candidate archive
# cooked geometry and packed layouts are opt-in.
# Keep these IDs in sync with include/engine_renderer_features.h.
RENDERER_MODES = {
    'texture_storage': {'legacy': 0, 'candidate-archive': 1},
    'geometry_source': {'runtime': 0, 'cooked': 1},
    'vertex_layout': {'float': 0, 'packed': 1, 'packed-verify': 2},
}
RENDERER_BASELINES = {'texture_storage': 'legacy', 'geometry_source': 'runtime', 'vertex_layout': 'float'}
RENDERER_AVAILABLE = {'texture_storage': ('legacy', 'candidate-archive'),
                      'geometry_source': ('runtime', 'cooked'), 'vertex_layout': ('float', 'packed', 'packed-verify')}
# Each experiment has its own selector. Unimplemented modes fail before any
# build output is created, rather than silently behaving like the reference.
ALGORITHM_MODES = {'reference': 0, 'candidate': 1, 'verify': 2}
ALGORITHMS = ('root_pose', 'vfp_schedule', 'support_corners', 'water_base',
              'adpcm_table', 'adpcm_loops', 'root_decode', 'collision_inverse',
              'collision_topology', 'material_candidates', 'cpu_skin_stream',
              'pose_eviction', 'bsp_resolved_batch', 'audio_split',
              'prepared_view', 'shared_skinning', 'inverse_bind', 'midpoint_quaternions',
              'model_compact', 'collision_features', 'collision_active_planes', 'uv_outputs',
              'compressed_tracks', 'deferred_decode', 'stable_palette', 'retained_skin', 'local_hierarchy',
              'boolean_selectors', 'skin_mad', 'midpoint_particles', 'region_selection')
GPU_ALGORITHMS = ('uv_outputs', 'boolean_selectors', 'skin_mad', 'water_base')
IMPLEMENTED_ALGORITHMS = ('prepared_view', 'shared_skinning', 'inverse_bind',
                          'midpoint_quaternions', 'root_pose', 'model_compact',
                          'collision_features', 'collision_active_planes', 'compressed_tracks',
                          'deferred_decode', 'stable_palette', 'retained_skin', 'local_hierarchy',
                          'bsp_resolved_batch', 'midpoint_particles', 'region_selection', *GPU_ALGORITHMS)


def run(command, out, label):
    """Response files handle long link commands and spaces in Windows paths."""
    response = out/(label+'.rsp'); response.parent.mkdir(parents=True, exist_ok=True)
    response.write_text('\n'.join('"'+str(a).replace('\\', '/')+'"' for a in command[1:]), encoding='utf-8')
    result = subprocess.run([str(command[0]), '@'+str(response)], cwd=ROOT, capture_output=True, text=True)
    log = out/(label+'.log'); log.write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{label} failed; see {log}\n{result.stderr[-2500:]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/n3ds')
    defaults = {'devkitarm': 'C:/devkitPro/devkitARM', 'libctru': ROOT.parent/'libctru/libctru',
                'citro3d': ROOT.parent/'citro3d', 'xdk': ROOT/'xbox/include'}
    for name, default in defaults.items():
        parser.add_argument('--'+name, type=Path, default=Path(os.environ.get('HALO_XDK' if name=='xdk' else name.upper(), str(default))))
    parser.add_argument('--clang', default=os.environ.get('CLANG') or shutil.which('clang') or 'clang')
    parser.add_argument('--picasso', default=os.environ.get('PICASSO') or str(ROOT.parent/'picasso-host/picasso.exe'))
    parser.add_argument('--packer', default=os.environ.get('THREEDSXTOOL') or str(ROOT.parent/'3dsxtool.exe'))
    parser.add_argument('--jobs', type=int, default=2, choices=range(1, 9))
    parser.add_argument('--test-frames', type=int, default=0, help='0 produces the playable release; positive values stop after that many frames')
    parser.add_argument('--diagnostics', action='store_true', help='Enable renderer checks; requires --test-frames')
    parser.add_argument('--original-memory', action='store_true', help='Constrain heap sizes for original-model emulator tests')
    parser.add_argument('--fixed-world-width', type=int, choices=(400, 320),
                        help='Fix the New-model world viewport for a render comparison')
    parser.add_argument('--present-60', action='store_true',
                        help='Allow native-resolution in-between frames on New Nintendo 2DS XL')
    parser.add_argument('--midpoint-capture-tick', type=int, default=0,
                        help='Capture one submitted midpoint and following regular target at or after this tick')
    parser.add_argument('--action-replay', choices=('off', 'record', 'replay'), default='off',
                        help='Record or replay a bounded tick-indexed local campaign correctness route')
    parser.add_argument('--action-replay-fixture-ticks', type=int, default=0,
                        help='Run a deterministic no-input a10 action journal fixture for 1..3600 ticks')
    parser.add_argument('--texture-cache-mib', type=int, choices=(4, 8), default=4,
                        help='New-model gameplay texture cache limit in MiB (default: 4)')
    parser.add_argument('--texture-prefetch', action='store_true',
                        help='Enable bounded texture preparation on New models')
    parser.add_argument('--texture-compare', action='store_true',
                        help='Accept opt-in matched raw, LZ4 HC and exact native texture archives')
    parser.add_argument('--texture-compare-fixture', choices=('off', 'capture', 'timing'), default='off',
                        help='Run the bounded menu-to-a10 comparison route with capture or timing')
    parser.add_argument('--texture-lod-fixture', choices=('off', 'capture', 'timing'), default='off',
                        help='Compare native RGB8 mip tiers on the same bounded perspective route')
    parser.add_argument('--disable-portal-cache', action='store_true',
                        help='Use uncached portal projections for a baseline')
    parser.add_argument('--disable-resident-indices', action='store_true',
                        help='Upload all model triangle lists through the per-frame arena')
    parser.add_argument('--performance-profile', action='store_true',
                        help='Report frame percentiles, visibility and texture costs')
    parser.add_argument('--algorithm-profile', action='store_true',
                        help='Add bounded owner-side algorithm counters; requires --performance-profile')
    parser.add_argument('--allocation-profile', action='store_true',
                        help='Observe allocator peaks after public allocation calls; requires --algorithm-profile')
    parser.add_argument('--profile-frames', type=int, default=0,
                        help='Exit after 1..18000 frontend loops without readback; requires --performance-profile')
    parser.add_argument('--dependency-trace', action='store_true',
                        help='Collect a bounded dependency event trace independently of profiling; export at shutdown')
    parser.add_argument('--dependency-trace-loops', type=int,
                        help='Capture 1..18000 completed frontend loops after gameplay starts (default: 120); requires --dependency-trace')
    parser.add_argument('--gpu-probe', action='store_true',
                        help='Enable the paused-view GPU workload comparison; requires --fixed-world-width 400')
    parser.add_argument('--framebuffer-reuse', action='store_true',
                        help='Reuse identical native framebuffer bindings on New models; requires --fixed-world-width 400')
    parser.add_argument('--skip-opaque-color-reads', action='store_true',
                        help='Skip destination-color reads for exact RGBA overwrites; requires --fixed-world-width 400')
    for name in ALGORITHMS:
        choices=('reference', 'candidate') if name in (*GPU_ALGORITHMS, 'bsp_resolved_batch', 'midpoint_particles') else tuple(ALGORITHM_MODES)
        parser.add_argument('--algo-'+name.replace('_', '-'), choices=choices, default='reference',
                            help=('Independent experimental algorithm comparison' if name in IMPLEMENTED_ALGORITHMS
                                  else 'Independent algorithm comparison; only reference is implemented'))
    for name, modes in RENDERER_MODES.items():
        baseline = RENDERER_BASELINES[name]
        parser.add_argument('--'+name.replace('_', '-'), choices=tuple(modes), default=baseline,
                            help=f'Independent renderer mode (default: {baseline}); available: {", ".join(RENDERER_AVAILABLE[name])}')
    args = parser.parse_args()
    for name in ALGORITHMS:
        if name not in IMPLEMENTED_ALGORITHMS and getattr(args, 'algo_'+name) != 'reference':
            parser.error(f'--algo-{name.replace("_", "-")} candidate/verify is reserved but not implemented')
    if not args.present_60 and any(getattr(args, 'algo_'+name) != 'reference' for name in
                                  ('prepared_view', 'midpoint_quaternions', 'midpoint_particles')):
        parser.error('Prepared view, midpoint quaternions and midpoint particles require --present-60')
    if not 0 <= args.midpoint_capture_tick <= 0x7fffffff:
        parser.error('--midpoint-capture-tick must be 0..2147483647')
    if args.midpoint_capture_tick and (not args.present_60 or args.dependency_trace or args.performance_profile):
        parser.error('--midpoint-capture-tick requires --present-60 and a separate correctness run')
    if args.action_replay != 'off' and (args.performance_profile or args.dependency_trace or args.test_frames or
                                        args.diagnostics or args.original_memory or args.midpoint_capture_tick):
        parser.error('--action-replay requires a separate correctness run')
    if not 0 <= args.action_replay_fixture_ticks <= 3600 or (
            args.action_replay_fixture_ticks and (args.action_replay == 'off' or args.present_60 or
            args.fixed_world_width != 400 or args.texture_prefetch or args.texture_storage != 'legacy')):
        parser.error('--action-replay-fixture-ticks requires action record/replay and a regular native-400 legacy build')
    if args.profile_frames < 0 or args.profile_frames > 18000:
        parser.error('--profile-frames must be 0..18000 frontend loops')
    if (args.algorithm_profile or args.profile_frames) and not args.performance_profile:
        parser.error('--algorithm-profile and --profile-frames require --performance-profile')
    if args.allocation_profile and not args.algorithm_profile:
        parser.error('--allocation-profile requires --algorithm-profile')
    if args.profile_frames and (args.test_frames or args.diagnostics or args.original_memory):
        parser.error('--profile-frames cannot be combined with readback or validation fixtures')
    if args.dependency_trace_loops is not None and not args.dependency_trace:
        parser.error('--dependency-trace-loops requires --dependency-trace')
    if args.dependency_trace_loops is None:
        args.dependency_trace_loops = 120
    if not 1 <= args.dependency_trace_loops <= 18000:
        parser.error('--dependency-trace-loops must be 1..18000 completed frontend loops')
    if args.dependency_trace and (args.test_frames or args.diagnostics or args.original_memory):
        parser.error('--dependency-trace cannot be combined with readback or validation fixtures')
    if args.skip_opaque_color_reads and args.framebuffer_reuse:
        parser.error('Measure --skip-opaque-color-reads independently of --framebuffer-reuse')
    if (args.gpu_probe or args.framebuffer_reuse or args.skip_opaque_color_reads) and (
                           args.fixed_world_width != 400 or args.test_frames or args.diagnostics or
                           args.original_memory or args.present_60 or args.dependency_trace or
                           args.performance_profile or args.texture_prefetch or
                           args.disable_portal_cache or args.disable_resident_indices or
                           args.texture_storage != 'legacy' or args.texture_cache_mib != 4 or
                           args.action_replay != 'off' or args.texture_compare_fixture != 'off' or
                           args.texture_lod_fixture != 'off' or args.geometry_source != 'runtime' or
                           args.vertex_layout != 'float' or
                           any(getattr(args, 'algo_'+name) != 'reference' for name in ALGORITHMS)):
        parser.error('GPU comparison options require --fixed-world-width 400 and the regular legacy renderer configuration')
    for name in RENDERER_MODES:
        selected = getattr(args, name)
        if selected not in RENDERER_AVAILABLE[name]:
            parser.error(f'--{name.replace("_", "-")} {selected} is reserved but not implemented; '
                         f'use {RENDERER_BASELINES[name]}')
    if args.test_frames<0 or (args.diagnostics and not args.test_frames):
        parser.error('diagnostics require a positive frame limit')
    if args.diagnostics and args.fixed_world_width:
        parser.error('--fixed-world-width is for gameplay and cannot be combined with --diagnostics')
    if args.texture_compare and (args.texture_storage != 'candidate-archive' or args.texture_prefetch or args.present_60):
        parser.error('--texture-compare requires candidate-archive with prefetch and midpoint disabled')
    if args.texture_compare_fixture != 'off':
        if (not args.texture_compare or args.fixed_world_width != 400 or args.texture_cache_mib != 4 or
                args.test_frames or args.diagnostics or args.original_memory or args.dependency_trace or
                args.action_replay != 'off' or args.profile_frames or
                args.performance_profile != (args.texture_compare_fixture == 'timing')):
            parser.error('texture comparison fixture requires native 400, 4 MiB, and profiling only in timing mode')
    if args.texture_lod_fixture != 'off' and (
            args.fixed_world_width != 400 or args.test_frames or args.diagnostics or args.original_memory or
            args.action_replay != 'off' or args.dependency_trace or args.profile_frames or args.performance_profile or
            args.present_60 or args.texture_prefetch or args.texture_compare or args.texture_storage != 'legacy'):
        parser.error('texture LOD fixture requires native 400 and its own capture or timing configuration')
    if args.present_60 and (args.test_frames or args.original_memory or args.fixed_world_width):
        parser.error('--present-60 requires a playable New-model build with its own native-resolution policy')
    # The diagnostic test-frame limit performs readbacks even without
    # --diagnostics. The independent profile-loop limit does not select it.
    # This describes configured intent, never a successful validation or a
    # measured performance result; external capture cannot be inferred here.
    readback_configured = bool(args.test_frames or args.diagnostics or args.midpoint_capture_tick or
                               args.texture_compare_fixture == 'capture' or args.texture_lod_fixture == 'capture')
    validation_configured = (readback_configured or args.original_memory or args.action_replay != 'off' or
                             args.texture_lod_fixture != 'off')
    algorithm_verification = (args.vertex_layout == 'packed-verify' or
                              any(getattr(args, 'algo_'+name) == 'verify' for name in IMPLEMENTED_ALGORITHMS))
    uncaptured_profile_eligible = bool(args.performance_profile and not validation_configured and
                                       not algorithm_verification and not args.allocation_profile)
    run_purpose = ('validation-readback' if readback_configured else
                   'validation-fixture' if validation_configured else
                   'algorithm-verification' if algorithm_verification else
                   'allocation-observation' if args.allocation_profile else
                   'opaque-color-read-probe' if args.gpu_probe and args.skip_opaque_color_reads else
                   'framebuffer-reuse-probe' if args.gpu_probe and args.framebuffer_reuse else
                   'gpu-workload-probe' if args.gpu_probe else
                   'dependency-trace-uncaptured' if args.dependency_trace else
                   'performance-uncaptured-candidate' if uncaptured_profile_eligible else 'gameplay')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    for name in defaults:
        setattr(args, name, getattr(args, name).resolve())
    ext = '.exe' if os.name=='nt' else ''
    gcc = args.devkitarm/('bin/arm-none-eabi-gcc'+ext)
    ar = args.devkitarm/('bin/arm-none-eabi-ar'+ext)
    nm = args.devkitarm/('bin/arm-none-eabi-nm'+ext)
    for path in [gcc, ar, args.libctru/'lib/libctru.a', args.citro3d/'lib/libcitro3d.a', args.xdk]:
        if not path.exists(): parser.error(f'Missing dependency: {path}')
    frame_adapter = args.present_60 or args.dependency_trace or args.gpu_probe
    pinned_citro3d = dict(CITRO3D_FRAME_SOURCE) if frame_adapter else {}
    if args.skip_opaque_color_reads:
        pinned_citro3d.update(CITRO3D_COLOR_READ_SOURCE)
    for name, expected in pinned_citro3d.items():
        path = args.citro3d/'source'/name
        if not path.is_file() or hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest() != expected:
            parser.error(f'The selected Citro3D adapter requires the {CITRO3D_FRAME_REVISION} source checkout and its built library; check {path}')
    manifest = json.loads((PORT/'sources.json').read_text())
    entries = manifest['objects']+[{'source':s,'compiler':'clang','archive':True} for s in manifest['runtime']]
    sources = [ROOT/e['source'] for e in entries]
    if len(set(sources))!=len(sources): raise RuntimeError('Duplicate source in sources.json')
    for source in sources:
        if not source.resolve().is_relative_to(ROOT) or not source.is_file(): raise RuntimeError(f'Missing source: {source}')
    overlay, semantics = prepare(ROOT, args.xdk, out)
    shader_modes = {name: True for name in GPU_ALGORITHMS if getattr(args, 'algo_'+name) == 'candidate'}
    if args.vertex_layout != 'float':
        shader_modes['packed_vertices'] = True
    shaders = generate(PORT, out, args.picasso, **shader_modes)
    digest = hashlib.sha256((PORT/'sources.json').read_bytes()+Path(__file__).read_bytes())
    for folder in [ROOT/'source', PORT/'include', PORT/'compat', PORT/'tests', PORT/'vendor', overlay, PORT/'tools']:
        for path in sorted(folder.rglob('*')):
            if path.is_file() and path.suffix in ('.h','.inl','.inc','.py'):
                digest.update(path.name.encode()); digest.update(path.read_bytes())
    for source in sources: digest.update(source.read_bytes())
    for shader in shaders.values(): digest.update(Path(shader['binary']).read_bytes())
    digest.update(semantics.read_bytes())
    source_digest=digest.hexdigest()
    if not frame_adapter:
        entries = [entry for entry in entries if not entry.get('citro3d_frame_adapter')]
    if not args.skip_opaque_color_reads:
        entries = [entry for entry in entries if not entry.get('citro3d_color_read_adapter')]
    defines = ['-DHALO_N3DS_CHECKPOINT_FEATURE=1',f'-DHALO_N3DS_MISSION_TICKS={args.test_frames}']
    for name, modes in RENDERER_MODES.items():
        defines.append(f'-DHALO_N3DS_{name.upper()}={modes[getattr(args, name)]}')
    for name in ALGORITHMS:
        defines.append(f'-DHALO_N3DS_ALGO_{name.upper()}={ALGORITHM_MODES[getattr(args, "algo_"+name)]}')
    defines.append(f'-DHALO_N3DS_PROFILE_FRAMES={args.profile_frames}')
    defines.append(f'-DHALO_N3DS_ACTION_REPLAY={("off", "record", "replay").index(args.action_replay)}')
    defines.append(f'-DHALO_N3DS_ACTION_REPLAY_FIXTURE_TICKS={args.action_replay_fixture_ticks}')
    defines.append(f'-DHALO_N3DS_MIDPOINT_CAPTURE_TICK={args.midpoint_capture_tick}')
    if args.algorithm_profile:
        defines.append('-DHALO_N3DS_ALGORITHM_PROFILE=1')
    if args.allocation_profile:
        defines.append('-DHALO_N3DS_ALLOCATION_PROFILE=1')
    if args.dependency_trace:
        defines.extend(('-DHALO_N3DS_DEPENDENCY_TRACE=1',
                        f'-DHALO_N3DS_DEPENDENCY_TRACE_LOOPS={args.dependency_trace_loops}'))
    if args.gpu_probe:
        defines.append('-DHALO_N3DS_GPU_PROBE=1')
    if args.framebuffer_reuse:
        defines.append('-DHALO_N3DS_FRAMEBUFFER_REUSE=1')
    if args.skip_opaque_color_reads:
        defines.append('-DHALO_N3DS_OPAQUE_COLOR_READS=1')
    if args.diagnostics: defines.append('-DHALO_N3DS_RENDERER_TESTS=1')
    if args.original_memory: defines.append('-DHALO_N3DS_LOW_MEMORY_TEST=1')
    if args.fixed_world_width:
        defines.append(f'-DHALO_N3DS_FIXED_WORLD_WIDTH={args.fixed_world_width}')
    if args.present_60:
        defines.append('-DHALO_N3DS_PRESENT_INTERPOLATION=1')
    defines.append(f'-DHALO_N3DS_TEXTURE_CACHE_MIB={args.texture_cache_mib}')
    if args.texture_prefetch:
        defines.append('-DHALO_N3DS_TEXTURE_PREFETCH=1')
    if args.texture_compare:
        defines.append('-DHALO_N3DS_TEXTURE_COMPARE=1')
    if args.texture_compare_fixture != 'off':
        defines.append(f'-DHALO_N3DS_TEXTURE_COMPARE_FIXTURE={("off", "capture", "timing").index(args.texture_compare_fixture)}')
    if args.texture_lod_fixture != 'off':
        defines.append(f'-DHALO_N3DS_TEXTURE_LOD_FIXTURE={("off", "capture", "timing").index(args.texture_lod_fixture)}')
    if args.disable_portal_cache:
        defines.append('-DHALO_N3DS_DISABLE_PORTAL_CACHE=1')
    if args.disable_resident_indices:
        defines.append('-DHALO_N3DS_DISABLE_RESIDENT_INDICES=1')
    if args.performance_profile:
        defines.extend(('-DHALO_N3DS_PERFORMANCE_PROFILE=1', '-DHALO_N3DS_TEXTURE_PROFILE=1'))
    source_date_epoch=os.environ.get('SOURCE_DATE_EPOCH')
    identity_dependencies=external_build_inputs(args.devkitarm,gcc,ar,args.clang,args.libctru,args.citro3d,
        PORT/'n3ds.specs',COMMON,ROOT)
    if pinned_citro3d:
        identity_dependencies['citro3d_adapter_source']={
            name:sha(args.citro3d/'source'/name) for name in pinned_citro3d}
    external_digest=canonical_digest(identity_dependencies)
    build_id=checkpoint_identity(source_digest,defines,source_date_epoch,external_digest)
    defines.insert(0,f'-DHALO_N3DS_CHECKPOINT_BUILD=0x{build_id:08x}u')
    native = [str(gcc),*COMMON,'-D__3DS__','-I',str(args.libctru/'include'),'-I',str(args.citro3d/'include')]
    game = [args.clang,'--target=arm-none-eabi','-isystem',str(args.devkitarm/'arm-none-eabi/include'),
            *COMMON,'-std=gnu99','-D__STRICT_ANSI__','-DHALO_N3DS','-fsigned-char','-fshort-wchar',
            '-fms-extensions','-fcommon','-ffp-contract=off','-Werror=implicit-function-declaration',
            '-Werror=incompatible-pointer-types','-Wno-multichar','-Wno-pragma-pack',
            '-Wno-ignored-attributes','-Wno-duplicate-decl-specifier']
    for folder in manifest['include_dirs']: game+=['-I',str(ROOT/folder)]
    shared = [*NEWLIB_ABI,*defines,'-I',str(out),'-I',str(PORT/'include'),'-I',str(PORT/'tests/include'),
              f'-ffile-prefix-map={ROOT}=Halo3DS',f'-ffile-prefix-map={out}=build']
    cache_path=out/'compile-cache.json'
    cache=json.loads(cache_path.read_text()) if cache_path.exists() else {}
    commands=[]; objects=[]; archived=[]

    def compile_entry(entry):
        source=ROOT/entry['source']; obj=out/'objects'/(entry['source']+'.o')
        obj.parent.mkdir(parents=True,exist_ok=True)
        command=list(native if entry['compiler']=='gcc' else game)+shared
        if entry['compiler']=='gcc':
            command+=['-std=gnu++11','-fno-exceptions','-fno-rtti'] if source.suffix=='.cpp' else ['-std=gnu11']
            if entry.get('citro3d_frame_adapter') or entry.get('citro3d_color_read_adapter'):
                command+=['-iquote',str(args.citro3d/'source')]
        else:
            sdk=entry.get('sdk',True)
            command+=['-include',str(PORT/'include'/('engine_prefix.h' if sdk else 'platform.h')),'-I',str(PORT/'compat/include')]
            if sdk: command+=['-include',str(semantics),'-idirafter',str(overlay)]
        command+=['-D'+d for d in entry.get('defines',[])]+['-MMD','-MF',str(obj.with_suffix('.d')),'-c',str(source),'-o',str(obj)]
        stamp=canonical_digest([source_digest,external_digest,command,source_date_epoch])
        old=cache.get(entry['source'],{})
        if old.get('stamp')!=stamp or not obj.exists() or old.get('sha256')!=sha(obj):
            run(command,out,'compile/'+entry['source'])
        return entry,obj,command,{'stamp':stamp,'sha256':sha(obj)}

    failures=[]
    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        pending=[workers.submit(compile_entry,entry) for entry in entries]
        for number,future in enumerate(pending,1):
            try:
                entry,obj,command,record=future.result()
            except RuntimeError as error:
                # Finish this bounded batch so a recovered SDK reports all
                # missing declarations together. Never link partial objects.
                failures.append(str(error))
                continue
            cache[entry['source']]=record; commands.append(command)
            (archived if entry.get('archive') else objects).append(obj)
            if number%40==0 or number==len(entries): print(f'Compiled {number}/{len(entries)}',flush=True)
    if failures:
        raise RuntimeError(f'{len(failures)} compilation unit(s) failed:\n'+'\n\n'.join(failures))
    cache_path.write_text(json.dumps(cache,indent=2))
    archive=out/'original-runtime.a'
    if archive.exists(): archive.unlink()  # Only this build's generated archive; no stale members.
    run([str(ar),'rcs',str(archive),*map(str,archived)],out,'archive')
    elf=out/'Halo3DS.elf'; binary=out/'Halo3DS.3dsx'
    wraps = WRAPS + (['malloc', 'calloc', 'memalign', 'aligned_alloc'] if args.allocation_profile else [])
    if args.skip_opaque_color_reads:
        wraps += list(CITRO3D_COLOR_READ_WRAPS)
    link=[str(gcc),*COMMON,'-specs='+str(PORT/'n3ds.specs'),'-Wl,--gc-sections','-Wl,-Map='+str(out/'Halo3DS.map'),
          *['-Wl,--wrap='+name for name in wraps],*map(str,objects),str(archive),'-L',str(args.citro3d/'lib'),
          '-lcitro3d','-L',str(args.libctru/'lib'),'-lctru','-lm','-o',str(elf)]
    run(link,out,'link'); commands.append(link)
    undefined=subprocess.check_output([str(nm),'-u',str(elf)],text=True)
    allowed={'__aeabi_unwind_cpp_pr0','__aeabi_unwind_cpp_pr1','__aeabi_unwind_cpp_pr2','__sync_init','__sync_fini',
             '__deregister_frame_info','__register_frame_info','userAppExit','userAppInit'}
    if any(line.split()[-1] not in allowed for line in undefined.splitlines()): raise RuntimeError('Unresolved runtime symbols:\n'+undefined)
    subprocess.run([args.packer,str(elf),str(binary)],check=True)
    verify_linked(shaders,elf,[],out/'shader-verification.json')
    report={'binary':str(binary),'sha256':sha(binary),'elf_sha256':sha(elf),'link_map_sha256':sha(out/'Halo3DS.map'),
            'test_frames':args.test_frames,'diagnostics':args.diagnostics,'source_date_epoch':source_date_epoch,
            'algorithm_features':{'schema_version':1,
                                  'selections':{name:{'mode':getattr(args,'algo_'+name),
                                                      'id':ALGORITHM_MODES[getattr(args,'algo_'+name)]}
                                                for name in ALGORITHMS},
                                  'instrumentation':args.algorithm_profile,
                                  'allocation_event_samples':args.allocation_profile,
                                  'instrumentation_overhead':'unmeasured',
                                  'profile_loop_limit':args.profile_frames,
                                  'implemented_experiments':IMPLEMENTED_ALGORITHMS,
                                  'verification':algorithm_verification,
                                  'candidate_status':'experimental; no candidate promoted'},
            'dependency_trace':{'enabled':args.dependency_trace,
                                'independent_of_profile':True,
                                'schema_version':1,'header_bytes':128,'record_bytes':64,'footer_bytes':16,
                                'capture_loop_limit':args.dependency_trace_loops if args.dependency_trace else 0,
                                'capture_start':'first gameplay loop',
                                'capture_limit_unit':'completed frontend loops; not ticks or presentations',
                                'owner_event_capacity':8192,'owner_event_bytes':64,
                                'callback_ring_capacity':256,'vblank_ring_capacity':256,
                                'callback_ring_bytes':16400,'vblank_ring_bytes':16400,
                                'submission_metadata_slots':16,'submission_metadata_bytes':704,
                                'configured_buffer_bytes':557792 if args.dependency_trace else 0,
                                'buffer_bytes_exclude':'collector contexts/scalars, stack and stdio export storage',
                                'overflow_policy':'drop new records; never overwrite retained events',
                                'export':'shutdown only; capture limit does not end gameplay',
                                'export_path':'sdmc:/halo-trace-%016llx.bin (run ID)',
                                'checksum':'FNV-1a32 over header and records; trailer excluded',
                                'tail_always_unconfirmed':True,
                                'instrumentation_overhead':'unmeasured',
                                'frame_adapter':args.present_60 or args.dependency_trace,
                                'citro3d_frame_revision':CITRO3D_FRAME_REVISION},
            'renderer_features':{'schema_version':1,
                                 **{name:{'mode':getattr(args,name),'id':modes[getattr(args,name)]}
                                    for name,modes in RENDERER_MODES.items()}},
            'renderer_run':{'purpose':run_purpose,'configured_readback':readback_configured,
                            'texture_compare':args.texture_compare,
                            'texture_compare_fixture':args.texture_compare_fixture,
                            'texture_lod_fixture':args.texture_lod_fixture,
                            'configured_validation_fixture':validation_configured,
                            'gpu_workload_probe':args.gpu_probe,
                            'profile_instrumentation':args.performance_profile,
                            'dependency_trace_instrumentation':args.dependency_trace,
                            'midpoint_capture_start_tick':args.midpoint_capture_tick,
                            'action_replay':args.action_replay,
                            'action_replay_fixture_ticks':args.action_replay_fixture_ticks,
                            'configured_uncaptured_profile_eligible':uncaptured_profile_eligible,
                            'external_capture':'unverified',
                            'evidence':'configuration only; not validation or measured performance'},
            'performance':{'fixed_world_width':args.fixed_world_width,'texture_cache_mib':args.texture_cache_mib,
                           'skip_opaque_color_reads':args.skip_opaque_color_reads,
                           'framebuffer_reuse':args.framebuffer_reuse,
                           'present_60':args.present_60,
                           'texture_prefetch':args.texture_prefetch,'portal_cache':not args.disable_portal_cache,
                           'resident_indices':not args.disable_resident_indices,
                           'profile':args.performance_profile},
            'checkpoint_identity':build_id,'checkpoint_identity_source_sha256':source_digest,
            'checkpoint_identity_external_sha256':external_digest,'checkpoint_identity_dependencies':identity_dependencies,
            'commands':commands,'source_sha256':{e['source']:sha(ROOT/e['source']) for e in entries},
            'linked_inputs':{str(p):sha(p) for p in [*objects,archive]}}
    (out/'boot-build.json').write_text(json.dumps(report,indent=2))
    print(f'Built {binary}\nSHA256 {report["sha256"]}')


if __name__=='__main__':
    main()
