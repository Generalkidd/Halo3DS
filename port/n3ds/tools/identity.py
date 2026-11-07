"""Conservative image identity for checkpoints containing executable offsets."""
import hashlib
import json
import os
import shlex
import shutil
import subprocess
from pathlib import Path


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def tree_fingerprint(root, include=lambda path: True, *, recursive=True):
    """Hash relative names and file bytes, including files reached by symlink.

    Follow directory aliases under their relative names. A cycle fails closed
    since it cannot be enumerated, while broken links supply no file bytes.
    """
    def visit(directory, relative, ancestors):
        resolved = directory.resolve(strict=True)
        if resolved in ancestors:
            raise RuntimeError(f'Build input directory cycle: {directory}')
        ancestors = ancestors | {resolved}
        for path in sorted(directory.iterdir(), key=lambda p: p.name):
            if path.name in ('.git', '__pycache__'):
                continue
            name = relative / path.name
            if recursive and path.is_dir():
                yield from visit(path, name, ancestors)
            elif path.is_file() and include(name):
                yield (name.as_posix(), file_sha256(path))

    files = list(visit(root, Path(), set()))
    return {'sha256': canonical_digest(files), 'files': len(files)}


def external_build_inputs(devkitarm, gcc, ar, clang, libctru, citro3d, specs, common, cwd):
    env = {**os.environ, 'LC_ALL': 'C'}

    def probe(command):
        return subprocess.run(command, input='', capture_output=True, text=True,
                              cwd=cwd, env=env, timeout=30, check=True)

    def executable(name):
        result = cwd / name if os.path.dirname(name) else shutil.which(name)
        if result is None or not Path(result).is_file():
            raise RuntimeError(f'Build input executable unavailable: {name}')
        return file_sha256(Path(result))

    def include_dirs(command, language):
        # Use the driver's effective order, including target resource headers
        # and any header search overrides introduced by a compiler wrapper.
        output = probe([*command, '-E', '-v', '-x', language, '-']).stderr
        directories = []
        active = False
        for line in output.splitlines():
            if line.startswith('#include ') and line.endswith(' search starts here:'):
                active = True
            elif line == 'End of search list.':
                break
            elif active:
                path = line.strip().removesuffix(' (framework directory)')
                directories.append((cwd / path).resolve())
        if not directories:
            raise RuntimeError(f'No effective {language} header paths from {command[0]}')
        return [tree_fingerprint(directory) for directory in directories]

    def is_runtime_input(path):
        return (path.name == 'specs' or path.suffix in ('.a', '.o', '.so', '.dll', '.ld', '.lds', '.specs', '.cfg'))

    gcc_command = [str(gcc), *common]
    clang_command = [clang, '--target=arm-none-eabi', '-isystem', str(devkitarm/'arm-none-eabi/include'), *common]
    # Inspect the linker's effective arguments in addition to the SDK tree.
    # A driver or local adapter can pass -L directories ahead of its own
    # -print-file-name results, so those queries alone miss the chosen archive.
    link_probe = probe([*gcc_command, '-specs='+str(specs), '-###', '-x', 'c', '-', '-lm']).stderr
    gcc_files = {'driver': executable(str(gcc)), 'archiver': executable(str(ar))}
    for name in ('cc1', 'cc1plus', 'as', 'collect2', 'ld'):
        program = probe([*gcc_command, f'-print-prog-name={name}']).stdout.strip()
        gcc_files[name] = executable(program)

    spec_files = {}
    library_dirs = []
    driver_library_dirs = []
    linker_files = {}
    for line in link_probe.splitlines():
        if line.startswith('COLLECT_GCC='):
            gcc_files['backend_driver'] = executable(line.split('=', 1)[1])
        elif line.startswith('Reading specs from '):
            path = cwd / line.removeprefix('Reading specs from ').strip()
            spec_files[f'{len(spec_files)}:{path.name}'] = file_sha256(path)
        elif line.startswith('LIBRARY_PATH='):
            driver_library_dirs.extend(line.split('=', 1)[1].split(os.pathsep))
        elif line.startswith(' '):
            words = shlex.split(line)
            for i, word in enumerate(words):
                if word == '-L' and i + 1 < len(words):
                    library_dirs.append(words[i+1])
                elif word.startswith('-L') and len(word) > 2:
                    library_dirs.append(word[2:])
                elif word in ('-T', '-plugin') and i + 1 < len(words):
                    path = cwd / words[i+1]
                    linker_files[f'{len(linker_files)}:{path.name}'] = file_sha256(path)
                elif word.endswith('.o') and (cwd / word).is_file():
                    path = cwd / word
                    linker_files[f'{len(linker_files)}:{path.name}'] = file_sha256(path)

    if 'backend_driver' not in gcc_files or not linker_files or not library_dirs:
        raise RuntimeError('Unable to fingerprint effective GCC link inputs')
    unique_libraries = dict.fromkeys((cwd / path).resolve() for path in library_dirs + driver_library_dirs if path)
    gcc_libraries = [tree_fingerprint(path,is_runtime_input,recursive=False)
                     for path in unique_libraries if path.is_dir()]
    clang_version = probe([*clang_command,'--version']).stdout.splitlines()
    clang_configs = [file_sha256(cwd / line.removeprefix('Configuration file: ').strip())
                     for line in clang_version if line.startswith('Configuration file: ')]
    return {
        'gcc_version': probe([str(gcc),'--version']).stdout.splitlines()[0],
        'clang_version': clang_version[0],
        'gcc_executables': gcc_files,
        'clang_executable': executable(clang),
        'gcc_specs': spec_files,
        'gcc_link_files': linker_files,
        'gcc_library_search': gcc_libraries,
        'devkit_runtime_tree': tree_fingerprint(devkitarm,is_runtime_input),
        'gcc_c_headers': include_dirs(gcc_command,'c'),
        'gcc_cxx_headers': include_dirs(gcc_command,'c++'),
        'clang_headers': include_dirs(clang_command,'c'),
        'clang_configuration': clang_configs,
        'libctru_headers': tree_fingerprint(libctru/'include'),
        'citro3d_headers': tree_fingerprint(citro3d/'include'),
        'libctru_archive': file_sha256(libctru/'lib/libctru.a'),
        'citro3d_archive': file_sha256(citro3d/'lib/libcitro3d.a'),
        'link_specs': file_sha256(specs),
    }


def checkpoint_identity(source_and_shader_digest, defines, epoch, dependencies):
    # Normal portable saves use symbolic callbacks and store build=0. Unusual
    # pointers into the image store offsets, so code/layout variants MUST NOT
    # share this identity just to make comparison fixtures load across builds.
    payload = {'source_and_shaders': source_and_shader_digest,
               'defines': list(defines), 'source_date_epoch': epoch,
               'dependencies': dependencies}
    # Zero is the checkpoint format's portable-save sentinel.
    return int(canonical_digest(payload)[:8], 16) or 1
