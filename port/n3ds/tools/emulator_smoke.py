#!/usr/bin/env python3
"""Bounded Halo diagnostic run in a fresh private Azahar profile (macOS).

Requires an already-built diagnostic binary. This checks its own RESULT marker;
it never treats the installed release as evidence for changed source. No global
emulator settings, source assets, saves or physical SD contents are changed.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def observe_backend(receipt, log):
    text = log.read_text(errors='replace') if log.exists() else ''
    # Vulkan capability probing also runs before software rendering starts.
    # Only renderer-instance messages are evidence of an observed backend;
    # Renderer_GraphicsAPI is the requested configuration, not proof of use.
    for name, marker in (('vulkan', 'renderer_vulkan/renderer_vulkan.cpp:'),
                         ('opengl', 'renderer_opengl/renderer_opengl.cpp:'),
                         ('software', 'renderer_software/renderer_software.cpp:')):
        if marker in text:
            receipt['renderer_observed'] = name
    for line in text.splitlines():
        if 'Unknown or unsupported graphics API' in line:
            receipt['status'] = 'backend-unavailable'
            receipt['renderer_error'] = line
            return False
    return True


def processes(timeout=2):
    return subprocess.check_output(['ps', '-axo', 'pid=,args='], text=True, timeout=timeout)


def process_command(pid, deadline):
    # Observe only the owned PID. A whole-system process inventory timed out
    # during a successful menu run; it is only needed for the prelaunch check.
    for attempt in range(2):
        remaining = deadline-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Process observation deadline reached')
        try:
            result = subprocess.run(['ps', '-p', str(pid), '-o', 'args='],
                text=True, capture_output=True, check=False, timeout=min(remaining, 2))
        except subprocess.TimeoutExpired:
            if attempt == 0 and time.monotonic() < deadline:
                continue
            raise
        command = result.stdout.strip()
        if result.returncode == 1 and not command and not result.stderr.strip():
            return None  # ps reports no matching PID.
        if result.returncode != 0 or not command:
            raise RuntimeError(f'Cannot observe owned PID {pid}: ps returned {result.returncode}')
        return command


def owned(pid, emulator, binary, deadline):
    # Both paths are absolute and the binary is this run's private copy.
    # A command that merely mentions these paths is never our emulator.
    return process_command(pid, deadline) == f'{emulator} -w {binary}'


def launch(emulator, binary, env, log, state, deadline):
    # This Azahar macOS build warns for a terminal parent. Retain the PID of
    # our double-forked child so detaching never means unbounded ownership.
    # The grandchild cannot exec until the owner has stored its PID and sent
    # the acknowledgement. Closing that pipe aborts a failed launch.
    read_fd, write_fd = os.pipe()
    go_read, go_write = os.pipe()
    intermediate = os.fork()
    if intermediate == 0:
        os.close(read_fd)
        os.close(go_write)
        try:
            os.setsid()
            child = os.fork()
            if child:
                os.write(write_fd, f'PID {child}\n'.encode())
                os._exit(0)
            while os.getppid() != 1:
                if time.monotonic() >= deadline:
                    raise TimeoutError('Azahar launcher was not adopted before the deadline')
                time.sleep(.01)
            fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            os.dup2(fd, 1); os.dup2(fd, 2); os.close(fd)
            os.write(write_fd, b'READY\n')
            remaining = deadline-time.monotonic()
            if remaining <= 0 or not select.select([go_read], [], [], remaining)[0]:
                raise TimeoutError('Azahar launch acknowledgement timed out')
            if os.read(go_read, 1) != b'G':
                raise RuntimeError('Azahar launch owner withdrew before exec')
            os.close(go_read)
            # Python pipes are close-on-exec. EOF tells the owner that startup
            # has left this helper; an exec failure sends an explicit error.
            os.execve(str(emulator), [str(emulator), '-w', str(binary)], env)
        except BaseException as error:
            try:
                os.write(write_fd, ('ERROR '+str(error)+'\n').encode()[:2048])
            except OSError:
                pass
            os._exit(127)
    state['launcher_pid'] = intermediate
    os.close(write_fd)
    os.close(go_read)
    pending = b''
    ready = acknowledged = False
    try:
        while True:
            if state.get('signal') is not None:
                raise InterruptedError('Emulator launch interrupted')
            remaining = deadline-time.monotonic()
            if remaining <= 0:
                raise TimeoutError('Azahar launch timed out')
            if not select.select([read_fd], [], [], min(remaining, .1))[0]:
                continue
            chunk = os.read(read_fd, 4096)
            if not chunk:
                if not acknowledged:
                    raise RuntimeError('Azahar launcher exited before acknowledgement')
                return state['pid']
            pending += chunk
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                if line.startswith(b'PID '):
                    state['pid'] = int(line[4:])
                elif line == b'READY':
                    ready = True
                elif line.startswith(b'ERROR '):
                    raise RuntimeError(line[6:].decode(errors='replace'))
            if ready and state['pid'] is not None and not acknowledged:
                os.write(go_write, b'G')
                acknowledged = True
    finally:
        os.close(read_fd)
        os.close(go_write)


def reap_launcher(state, deadline):
    pid = state.get('launcher_pid')
    if pid is None:
        return True
    # An unreaped direct child cannot have its PID reused. Never signal a
    # grandchild or another process through this launcher-only path.
    try:
        if os.waitpid(pid, os.WNOHANG)[0]:
            state['launcher_pid'] = None
            return True
        os.kill(pid, signal.SIGKILL)
        while time.monotonic() < deadline:
            if os.waitpid(pid, os.WNOHANG)[0]:
                state['launcher_pid'] = None
                return True
            time.sleep(.01)
    except ChildProcessError:
        state['launcher_pid'] = None
        return True
    except ProcessLookupError:
        pass
    return False


def stop_owned(state, emulator, binary, deadline):
    pid = state['pid']
    if pid is None:
        # Before PID acknowledgement, the unreaped direct child reserves the
        # session/group ID created by setsid. Only this launch can own that
        # group. Stop its pre-exec helpers before reaping releases the ID.
        launcher = state.get('launcher_pid')
        if launcher is not None:
            try:
                os.killpg(launcher, signal.SIGKILL)
            except ProcessLookupError:
                pass  # setsid may not have run; reap_launcher stops our child.
        return reap_launcher(state, deadline)
    launcher_reaped = reap_launcher(state, deadline)
    term_sent = kill_sent = False
    kill_after = min(time.monotonic()+2, deadline-.5)
    while time.monotonic() < deadline:
        command = process_command(pid, deadline)
        if command is None:
            return launcher_reaped
        if command == f'{emulator} -w {binary}':
            sig = signal.SIGKILL if time.monotonic() >= kill_after else signal.SIGTERM
            if (sig == signal.SIGTERM and not term_sent) or (sig == signal.SIGKILL and not kill_sent):
                try:
                    os.kill(pid, sig)
                except ProcessLookupError:
                    return launcher_reaped
                term_sent |= sig == signal.SIGTERM
                kill_sent |= sig == signal.SIGKILL
        # Before exec, a helper may still be observing its closed owner pipe.
        # A different command is never signaled and never assumed cleaned up.
        time.sleep(min(.05, max(0, deadline-time.monotonic())))
    return False


def timeout_seconds(text):
    seconds = int(text)
    if not 1 <= seconds <= 600:
        raise argparse.ArgumentTypeError('timeout must be 1..600 seconds')
    return seconds


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True, help='Prepared halo-source directory (read only)')
    parser.add_argument('--dsp', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New, unused test directory')
    parser.add_argument('--timeout', type=timeout_seconds, default=180)
    parser.add_argument('--renderer', choices=('vulkan', 'opengl', 'software'), default='vulkan')
    parser.add_argument('--map', action='append', default=[], metavar='STEM',
        help='Also copy this map and its renderer sidecars into the private SD')
    parser.add_argument('--archive', action='append', type=Path, default=[], metavar='PATH',
        help='Copy an alternate .ntx, .ntx4 or .nmg using its original filename')
    parser.add_argument('--sd-file', action='append', type=Path, default=[], metavar='PATH',
        help='Copy an explicit fixture input to the private SD root using its original filename')
    parser.add_argument('--require-log', action='append', default=[], metavar='TEXT',
        help='Require this literal text in the completed guest log')
    args = parser.parse_args(argv)
    if sys.platform != 'darwin':
        parser.error('This owned-process harness currently supports macOS Azahar only')
    lock_path = Path(tempfile.gettempdir()) / f'halo3ds-azahar-{os.getuid()}.lock'
    lock_fd = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(lock_fd, 'r+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error('Another owned Azahar run is active')
        return run(args, parser)


def run(args, parser):
    emulator, binary, assets, dsp, out = (p.resolve() for p in
        (args.emulator, args.binary, args.assets, args.dsp, args.output))
    maps = {'ui', *args.map}
    if any(not re.fullmatch(r'[a-z0-9_]+', name) for name in maps):
        parser.error('--map must be a lowercase map stem')
    archives = {}
    for path in args.archive:
        if path.stem not in maps or path.suffix not in ('.ntx', '.ntx4', '.nmg'):
            parser.error(f'Archive must name a selected map with a .ntx, .ntx4 or .nmg suffix: {path}')
        if path.name in archives:
            parser.error(f'Duplicate archive override: {path.name}')
        archives[path.name] = path.resolve()
    sd_files = {}
    for path in args.sd_file:
        if not re.fullmatch(r'halo-[a-z0-9_.-]+', path.name) or path.name.endswith('.log'):
            parser.error(f'Invalid fixture input filename: {path.name}')
        if path.name in sd_files:
            parser.error(f'Duplicate fixture input: {path.name}')
        sd_files[path.name] = path.resolve()
    required = [emulator, binary, dsp, assets/'ui.nrl', assets/'ntsc2276-hud-strings.bin']
    required.extend(assets/f'{name}.map' for name in sorted(maps))
    required.extend(archives.values())
    required.extend(sd_files.values())
    for path in required:
        if not path.is_file(): parser.error(f'Missing input: {path}')
    if out.exists(): parser.error('Use a fresh output directory; existing profiles are retained')
    if any(len(fields) == 2 and (fields[1] == str(emulator) or fields[1].startswith(str(emulator)+' '))
           for fields in (line.strip().split(None, 1) for line in processes().splitlines())):
        parser.error('An Azahar process is already active; do not overlap emulator batches')
    out.mkdir(parents=True)
    source_binary = binary
    binary = out/'tested-Halo3DS.3dsx'
    shutil.copy2(source_binary, binary)
    env = os.environ.copy(); env.pop('TERM', None)
    for key, leaf in [('XDG_DATA_HOME', 'data'), ('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache')]:
        base = out/leaf; (base/'azahar-emu').mkdir(parents=True)
        env[key] = str(base)
    sd = out/'data/azahar-emu/sdmc'
    (sd/'halo-source').mkdir(parents=True)
    (sd/'3ds').mkdir()
    copied = []
    # Copy only selected assets. A supplied archive overrides its own basename;
    # candidate archives require explicit selection, even if assets contains one.
    selected = {f'{name}{suffix}' for name in maps for suffix in ('.map', '.nrl', '.ntx')}
    selected.update(('ntsc2276-hud-strings.bin', 'ntsc2276-loading.bin'))
    sources = {source.name: source for source in assets.iterdir() if source.is_file() and
        (source.name in selected or (source.suffix == '.nrl' and
        any(source.stem.startswith(name+'-bsp') for name in maps)))}
    sources.update(archives)
    for name, source in sorted(sources.items()):
        target = sd/'halo-source'/name
        shutil.copy2(source, target)
        copied.append({'name':name, 'source':str(source), 'sha256':sha(target), 'bytes':target.stat().st_size})
    staged = []
    for name, source in sorted(sd_files.items()):
        target = sd/name
        shutil.copy2(source, target)
        staged.append({'name':name, 'sha256':sha(target), 'bytes':target.stat().st_size})
    shutil.copy2(dsp, sd/'3ds/dspfirm.cdc')
    config = ('[Core]\nuse_cpu_jit=true\nuse_cpu_jit\\default=false\ncpu_clock_percentage=100\ncpu_clock_percentage\\default=false\n'
        '[System]\nis_new_3ds=true\nis_new_3ds\\default=false\n'
        '[Renderer]\ngraphics_api='+{'software':'0', 'opengl':'1', 'vulkan':'2'}[args.renderer]+'\ngraphics_api\\default=false\n'
        'resolution_factor=1\nresolution_factor\\default=false\n'
        # The first real fresh-profile run stopped at Azahar's update notice.
        # Disable that host dialog only in this newly created test profile.
        '[UI]\nfirst_start=false\nfirst_start\\default=false\nconfirmClose=false\nconfirmClose\\default=false\n'
        'check_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
        '[Miscellaneous]\nenable_telemetry=false\nenable_telemetry\\default=false\n')
    (out/'config/azahar-emu/qt-config.ini').write_text(config)
    receipt = {'scope':'Selected private Azahar inputs only; no physical performance claim',
        'binary_source':str(source_binary), 'binary':str(binary), 'binary_sha256':sha(binary), 'emulator':str(emulator),
        'emulator_sha256':sha(emulator), 'renderer':args.renderer,
        'renderer_requested':args.renderer, 'renderer_observed':None,
        'model':'Azahar New 3DS XL default (not New 2DS XL midpoint coverage)',
        'timeout_seconds':args.timeout, 'timeout_scope':'Launch, observation and owned-process cleanup combined',
        'assets':copied, 'sd_inputs':staged, 'required_log_text':args.require_log,
        'dsp_sha256':sha(sd/'3ds/dspfirm.cdc'),
        'initial_config_sha256':sha(out/'config/azahar-emu/qt-config.ini'),
        'status':'not-started', 'cleanup_complete':False}
    (out/'result.json').write_text(json.dumps(receipt, indent=2)+'\n')
    state = {'pid':None, 'launcher_pid':None, 'signal':None}
    previous_handlers = {}
    def request_stop(signum, frame):
        # Do not interrupt PID publication or pipe ownership. All launch and
        # observation waits are short/bounded and observe this request.
        state['signal'] = signum
    for signum in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[signum] = signal.signal(signum, request_stop)
    started = time.monotonic()
    deadline = started+args.timeout
    observation_deadline = deadline-min(6, args.timeout/2)
    try:
        pid = launch(emulator, binary, env, str(out/'process.log'), state,
            min(started+5, observation_deadline))
        receipt['pid'] = pid; receipt['status'] = 'running'
        while time.monotonic() < observation_deadline:
            if state['signal'] is not None:
                receipt['status'] = 'interrupted'; break
            if not observe_backend(receipt, out/'data/azahar-emu/log/azahar_log.txt'):
                break
            log = sd/'halo-source-engine.log'
            text = log.read_text(errors='replace') if log.exists() else ''
            if 'RESULT: PASS' in text:
                receipt['status'] = 'pass'
                # RESULT precedes the final logger/parallel teardown. Allow a
                # brief grace inside the same budget before closing Azahar.
                grace_deadline = min(time.monotonic()+1, observation_deadline)
                while time.monotonic() < grace_deadline and state['signal'] is None:
                    if not owned(pid, emulator, binary, deadline): break
                    time.sleep(min(.05, max(0, grace_deadline-time.monotonic())))
                break
            if 'RESULT: FAIL' in text or 'FRONTEND FAIL:' in text or 'FATAL:' in text:
                receipt['status'] = 'game-failure'; break
            if time.monotonic()-started > 3 and not owned(pid, emulator, binary, observation_deadline):
                receipt['status'] = 'emulator-exited-before-result'; break
            time.sleep(min(.25, max(0, observation_deadline-time.monotonic())))
        else:
            receipt['status'] = 'timeout'
    except (InterruptedError, KeyboardInterrupt):
        receipt['status'] = 'interrupted'
    except Exception as error:
        receipt['status'] = 'harness-failure'
        receipt['error'] = str(error)
    finally:
        for signum in previous_handlers:
            signal.signal(signum, signal.SIG_IGN)
        receipt['pid'] = state['pid']
        if state['signal'] is not None:
            receipt['signal'] = state['signal']
            receipt['status'] = 'interrupted'
        try:
            receipt['cleanup_complete'] = stop_owned(state, emulator, binary, deadline)
        except Exception as error:
            receipt['cleanup_complete'] = False
            receipt['cleanup_error'] = str(error)
        try:
            observe_backend(receipt, out/'data/azahar-emu/log/azahar_log.txt')
        except OSError as error:
            receipt['renderer_log_error'] = str(error)
        if receipt['status'] == 'pass' and args.require_log:
            try:
                text = (sd/'halo-source-engine.log').read_text(errors='replace')
                missing = [marker for marker in args.require_log if marker not in text]
                if missing:
                    receipt['status'] = 'missing-required-log-text'
                    receipt['missing_log_text'] = missing
            except OSError as error:
                receipt['status'] = 'harness-failure'
                receipt['error'] = str(error)
        receipt['elapsed_seconds'] = round(time.monotonic()-started, 3)
        (out/'result.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(json.dumps({key:receipt[key] for key in ('status','cleanup_complete','elapsed_seconds')}))
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)
    return 0 if receipt['status'] == 'pass' and receipt['cleanup_complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
