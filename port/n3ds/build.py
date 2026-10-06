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

PORT = Path(__file__).resolve().parent
ROOT = PORT.parents[1]
COMMON = ['-march=armv6k', '-mfloat-abi=hard', '-mfpu=vfp', '-marm', '-mno-unaligned-access',
          '-mtp=soft', '-O2', '-g', '-ffunction-sections', '-fdata-sections', '-fno-strict-aliasing', '-fwrapv']
WRAPS = ['free', 'debug_free', 'datum_new', 'data_iterator_new', 'data_verify', 'realloc', 'write']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


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
    args = parser.parse_args()
    if args.test_frames<0 or (args.diagnostics and not args.test_frames):
        parser.error('diagnostics require a positive frame limit')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    for name in defaults:
        setattr(args, name, getattr(args, name).resolve())
    ext = '.exe' if os.name=='nt' else ''
    gcc = args.devkitarm/('bin/arm-none-eabi-gcc'+ext)
    ar = args.devkitarm/('bin/arm-none-eabi-ar'+ext)
    nm = args.devkitarm/('bin/arm-none-eabi-nm'+ext)
    for path in [gcc, ar, args.libctru/'lib/libctru.a', args.citro3d/'lib/libcitro3d.a', args.xdk]:
        if not path.exists(): parser.error(f'Missing dependency: {path}')
    manifest = json.loads((PORT/'sources.json').read_text())
    entries = manifest['objects']+[{'source':s,'compiler':'clang','archive':True} for s in manifest['runtime']]
    sources = [ROOT/e['source'] for e in entries]
    if len(set(sources))!=len(sources): raise RuntimeError('Duplicate source in sources.json')
    for source in sources:
        if not source.resolve().is_relative_to(ROOT) or not source.is_file(): raise RuntimeError(f'Missing source: {source}')
    overlay, semantics = prepare(ROOT, args.xdk, out)
    shaders = generate(PORT, out, args.picasso)
    digest = hashlib.sha256((PORT/'sources.json').read_bytes()+Path(__file__).read_bytes())
    for folder in [ROOT/'source', PORT/'include', PORT/'compat', PORT/'tests', PORT/'vendor', overlay, PORT/'tools']:
        for path in sorted(folder.rglob('*')):
            if path.is_file() and path.suffix in ('.h','.inl','.inc','.py'):
                digest.update(path.name.encode()); digest.update(path.read_bytes())
    for source in sources: digest.update(source.read_bytes())
    for shader in shaders.values(): digest.update(Path(shader['binary']).read_bytes())
    build_id = int(digest.hexdigest()[:8],16)
    defines = [f'-DHALO_N3DS_CHECKPOINT_BUILD=0x{build_id:08x}u','-DHALO_N3DS_CHECKPOINT_FEATURE=1',f'-DHALO_N3DS_MISSION_TICKS={args.test_frames}']
    if args.diagnostics: defines.append('-DHALO_N3DS_RENDERER_TESTS=1')
    if args.original_memory: defines.append('-DHALO_N3DS_LOW_MEMORY_TEST=1')
    native = [str(gcc),*COMMON,'-D__3DS__','-I',str(args.libctru/'include'),'-I',str(args.citro3d/'include')]
    game = [args.clang,'--target=arm-none-eabi','-isystem',str(args.devkitarm/'arm-none-eabi/include'),
            *COMMON,'-std=gnu99','-D__STRICT_ANSI__','-DHALO_N3DS','-fsigned-char','-fshort-wchar',
            '-fms-extensions','-fcommon','-ffp-contract=off','-Werror=implicit-function-declaration',
            '-Werror=incompatible-pointer-types','-Wno-multichar','-Wno-pragma-pack',
            '-Wno-ignored-attributes','-Wno-duplicate-decl-specifier']
    for folder in manifest['include_dirs']: game+=['-I',str(ROOT/folder)]
    shared = [*defines,'-I',str(out),'-I',str(PORT/'include'),'-I',str(PORT/'tests/include'),
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
        else:
            sdk=entry.get('sdk',True)
            command+=['-include',str(PORT/'include'/('engine_prefix.h' if sdk else 'platform.h')),'-I',str(PORT/'compat/include')]
            if sdk: command+=['-include',str(semantics),'-idirafter',str(overlay)]
        command+=['-D'+d for d in entry.get('defines',[])]+['-MMD','-MF',str(obj.with_suffix('.d')),'-c',str(source),'-o',str(obj)]
        stamp=hashlib.sha256((digest.hexdigest()+json.dumps(command)).encode()).hexdigest()
        old=cache.get(entry['source'],{})
        if old.get('stamp')!=stamp or not obj.exists() or old.get('sha256')!=sha(obj):
            run(command,out,'compile/'+entry['source'])
        return entry,obj,command,{'stamp':stamp,'sha256':sha(obj)}

    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        for number,(entry,obj,command,record) in enumerate(workers.map(compile_entry,entries),1):
            cache[entry['source']]=record; commands.append(command)
            (archived if entry.get('archive') else objects).append(obj)
            if number%40==0 or number==len(entries): print(f'Compiled {number}/{len(entries)}',flush=True)
    cache_path.write_text(json.dumps(cache,indent=2))
    archive=out/'original-runtime.a'
    if archive.exists(): archive.unlink()  # Only this build's generated archive; no stale members.
    run([str(ar),'rcs',str(archive),*map(str,archived)],out,'archive')
    elf=out/'Halo3DS.elf'; binary=out/'Halo3DS.3dsx'
    link=[str(gcc),*COMMON,'-specs='+str(PORT/'n3ds.specs'),'-Wl,--gc-sections','-Wl,-Map='+str(out/'Halo3DS.map'),
          *['-Wl,--wrap='+name for name in WRAPS],*map(str,objects),str(archive),'-L',str(args.citro3d/'lib'),
          '-lcitro3d','-L',str(args.libctru/'lib'),'-lctru','-lm','-o',str(elf)]
    run(link,out,'link'); commands.append(link)
    undefined=subprocess.check_output([str(nm),'-u',str(elf)],text=True)
    allowed={'__aeabi_unwind_cpp_pr0','__aeabi_unwind_cpp_pr1','__aeabi_unwind_cpp_pr2','__sync_init','__sync_fini',
             '__deregister_frame_info','__register_frame_info','userAppExit','userAppInit'}
    if any(line.split()[-1] not in allowed for line in undefined.splitlines()): raise RuntimeError('Unresolved runtime symbols:\n'+undefined)
    subprocess.run([args.packer,str(elf),str(binary)],check=True)
    verify_linked(shaders,elf,[],out/'shader-verification.json')
    report={'binary':str(binary),'sha256':sha(binary),'test_frames':args.test_frames,'diagnostics':args.diagnostics,
            'checkpoint_identity':build_id,'commands':commands,'source_sha256':{e['source']:sha(ROOT/e['source']) for e in entries},
            'linked_inputs':{str(p):sha(p) for p in [*objects,archive]}}
    (out/'boot-build.json').write_text(json.dumps(report,indent=2))
    print(f'Built {binary}\nSHA256 {report["sha256"]}')


if __name__=='__main__':
    main()
