"""Check the Git index (default) or proposed working tree for excluded content.

The working-tree mode includes untracked, nonignored files and skips deletions.
Neither mode examines history or guarantees that all secrets have been detected.
"""
import argparse
from pathlib import Path, PurePosixPath
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {
    '.map', '.nrl', '.ntx', '.xbe', '.iso', '.xiso', '.3dsx', '.cia', '.elf',
    '.exe', '.dll', '.pdb', '.lib', '.o', '.a', '.zip', '.7z', '.rar', '.dmp',
    '.pfx', '.p12', '.key', '.log', '.shbin',
}
EXCLUDED_NAMES = {'dspfirm.cdc', 'ntsc2276-hud-strings.bin', 'ntsc2276-loading.bin'}
SECRETS = re.compile(
    rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'
    rb'|gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{70,}'
)


def check(name, data):
    path = PurePosixPath(name)
    reasons = []
    if (path.suffix.lower() in FORBIDDEN
            or name.startswith(('assets/', 'original/', 'xbox/'))
            or any(part in ('bin', 'obj', 'sdmc', 'build') for part in path.parts)
            or path.name in EXCLUDED_NAMES
            or (path.name.startswith('.env') and path.name != '.env.example')):
        reasons.append('excluded file type or directory')
    if len(data) > 50 * 1024 * 1024:
        reasons.append('file exceeds 50 MiB')
    if SECRETS.search(data):
        reasons.append('possible private key or GitHub token')
    return reasons


def working_files(git):
    names = subprocess.check_output(
        git + ['ls-files', '--cached', '--others', '--exclude-standard', '-z']
    ).split(b'\0')
    for raw_name in sorted(set(filter(None, names))):
        name = raw_name.decode('utf-8')
        path = ROOT / name
        # Do not follow links, including links to missing targets.
        if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
            yield name, None
        elif path.is_file():
            yield name, path.read_bytes()
        elif path.exists():
            yield name, None


def staged_files(git):
    entries = subprocess.check_output(git + ['ls-files', '--stage', '-z']).split(b'\0')
    proc = subprocess.Popen(git + ['cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for entry in filter(None, entries):
            metadata, raw_name = entry.split(b'\t', 1)
            mode, oid, stage = metadata.split()
            name = raw_name.decode('utf-8')
            if stage != b'0' or mode not in (b'100644', b'100755'):
                yield name, None
                continue
            proc.stdin.write(oid + b'\n')
            proc.stdin.flush()
            header = proc.stdout.readline().split()
            if len(header) != 3 or header[1] != b'blob':
                raise RuntimeError(f'Cannot inspect staged blob: {name}')
            size = int(header[2])
            data = proc.stdout.read(size)
            if len(data) != size or proc.stdout.read(1) != b'\n':
                raise RuntimeError('Incomplete Git object read')
            yield name, data
    finally:
        proc.stdin.close()
        proc.stdout.close()
        proc.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--working-tree', action='store_true')
    parser.add_argument('--git', default='git', help='Path to the Git executable')
    args = parser.parse_args()
    git = [args.git, '-C', str(ROOT)]
    issues, count, total = [], 0, 0
    files = working_files(git) if args.working_tree else staged_files(git)
    for name, data in files:
        count += 1
        if data is None:
            reasons = ['unresolved entry, link, directory or submodule']
        else:
            total += len(data)
            reasons = check(name, data)
        issues.extend((name, reason) for reason in reasons)
    for name, reason in issues:
        print(f'BLOCKED: {name}: {reason}')
    scope = 'working-tree' if args.working_tree else 'staged'
    print(f'{"FAIL" if issues else "PASS"}: {count} {scope} files, {total / 1048576:.1f} MiB; '
          f'{len(issues)} recognized content issues.')
    return int(bool(issues))


if __name__ == '__main__':
    raise SystemExit(main())
