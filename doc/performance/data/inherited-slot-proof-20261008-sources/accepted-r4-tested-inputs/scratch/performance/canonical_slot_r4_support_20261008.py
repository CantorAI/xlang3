"""Serial R4 diagnostic support; never changes benchmark workloads or controls."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(r'D:\CantorAI\xlang3')
CPYTHON = Path(r'C:\Python\Python314\python.exe')
CANDIDATE = ROOT / 'build-repro/main-verify-20261006/Release/xlang3.exe'
CONTROL = ROOT / 'build-repro/controls/vm-captured-lookup-checkpoint-20261007/xlang3.exe'
CTEST = r'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
SQLGLOT_SOURCE = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_sqlglot_v2\run_benchmark.py')
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
SOURCES = (
    'src/executor/xlang_vm/xlang_vm_attr.cpp',
    'src/executor/xlang_vm/ops/xlang_vm_ops_attr.h',
    'tests/cpp/canonical_slot_read_cases.h',
    'tests/cpp/interpreter_tests.cpp', 'tests/run_fixtures.py',
    'tests/fixtures/core/canonical_slot_reads.py',
    'tests/fixtures/expected/canonical_slot_reads.out',
    'scratch/performance/canonical-slot-probe-20261008.py',
    'scratch/performance/canonical-slot-sqlglot-probe-20261008.py',
    'scratch/performance/check-canonical-slot-r4-20261008.py',
    'scratch/performance/compare-canonical-slot-r4-20261008.py',
    'scratch/performance/canonical_slot_r4_support_20261008.py',
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def environment(timing=False):
    env = os.environ.copy()
    env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
    for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
        env.pop(name, None)
    if timing:
        env['PYTHONPATH'] = os.pathsep.join((str(HOOK.parent), str(ROOT /
            'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')))
        env['PYTHONIOENCODING'] = 'utf-8'
    return env


def snapshot():
    return {
        'binaries_sha256': {
            label: {'exe': digest(path), 'dll': digest(path.with_name('xlang3_runtime.dll'))}
            for label, path in (('control', CONTROL), ('candidate', CANDIDATE))},
        'cpp_test_binary_sha256': digest(CANDIDATE.with_name('xlang3_interpreter_tests.exe')),
        'cpython3147_binary_sha256': digest(CPYTHON),
        'source_sha256': {name: digest(ROOT / name) for name in SOURCES},
        'benchmark_source_sha256': digest(SQLGLOT_SOURCE),
        'compatibility_hook_sha256': digest(HOOK),
    }


class CheckFailure(RuntimeError):
    pass


class DiagnosticRun:
    def __init__(self, prefix, scope, allowed_controller_pid=None):
        self.prefix = prefix
        self.data = ROOT / 'doc/performance/data'
        self.output = self.data / (prefix + '.json')
        self.allowed_pids = {os.getpid()}
        if allowed_controller_pid is not None:
            self.allowed_pids.add(allowed_controller_pid)
        self.record = {'status': 'running', 'scope': scope,
                       'owned_controller_pids': sorted(self.allowed_pids),
                       'phases': [], 'idle_guards': []}
        # Refuse to replace a previous result, even if it failed midway.
        with self.output.open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(self.record, indent=2) + '\n')

    def save(self):
        self.output.write_text(json.dumps(self.record, indent=2) + '\n', encoding='utf-8')

    def initialize(self):
        if sys.version_info[:3] != (3, 14, 7):
            raise CheckFailure('Controller must use CPython 3.14.7')
        if os.path.normcase(str(Path(sys.executable).resolve())) != os.path.normcase(str(CPYTHON.resolve())):
            raise CheckFailure('Controller must use C:/Python/Python314/python.exe')
        if Path.cwd().resolve() != ROOT.resolve():
            raise CheckFailure('Run directory must remain D:/CantorAI/xlang3')
        self.record['hashes_before'] = snapshot()
        self.save()

    def phase(self, name, command, env, timeout, validator=None):
        stdout_log = self.data / (self.prefix + '-' + name + '-stdout.log')
        stderr_log = self.data / (self.prefix + '-' + name + '-stderr.log')
        if stdout_log.exists() or stderr_log.exists():
            raise CheckFailure('Raw phase logs already exist: ' + name)
        phase = {'name': name, 'command': command, 'status': 'running'}
        self.record['phases'].append(phase)
        self.save()
        started = time.perf_counter()
        stdout, stderr, error, exit_code = b'', b'', None, None
        try:
            completed = subprocess.run(command, cwd=ROOT, env=env,
                                       capture_output=True, timeout=timeout)
            stdout, stderr, exit_code = completed.stdout, completed.stderr, completed.returncode
        except subprocess.TimeoutExpired as exc:
            stdout, stderr = exc.stdout or b'', exc.stderr or b''
            error = 'timeout after ' + str(timeout) + ' seconds'
        except Exception as exc:
            error = type(exc).__name__ + ': ' + str(exc)
        for log, payload in ((stdout_log, stdout), (stderr_log, stderr)):
            with log.open('xb') as stream:
                stream.write(payload)
        phase.update({'exit_code': exit_code, 'elapsed_seconds': time.perf_counter() - started,
                      'stdout_log': stdout_log.name, 'stdout_sha256': digest(stdout_log),
                      'stderr_log': stderr_log.name, 'stderr_sha256': digest(stderr_log)})
        if error is not None or exit_code != 0:
            phase['status'] = 'failed'
            phase['error'] = error or ('nonzero exit: ' + str(exit_code))
            self.record['status'] = 'failed_' + name
            self.save()
            raise CheckFailure(name + ': ' + phase['error'])
        try:
            if validator is not None:
                validator(stdout)
        except Exception as exc:
            phase['status'] = 'failed_validation'
            phase['error'] = type(exc).__name__ + ': ' + str(exc)
            self.record['status'] = 'failed_' + name + '_validation'
            self.save()
            raise CheckFailure(name + ': validation failed') from exc
        phase['status'] = 'passed'
        self.save()
        return stdout

    def idle_guard(self, name):
        # Query process names and IDs only; the guard PowerShell process is not
        # itself a build, test or Python benchmark. Every Python process other
        # than explicitly owned controller PIDs blocks measurement.
        command = ['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                   'Get-CimInstance Win32_Process | Select-Object Name,ProcessId,ParentProcessId | ConvertTo-Json -Compress']
        raw = self.phase('idle-' + name, command, environment(), 30)
        processes = json.loads(raw)
        if isinstance(processes, dict):
            processes = [processes]
        build_names = {'msbuild.exe', 'cl.exe', 'link.exe', 'cmake.exe', 'ninja.exe',
                       'nmake.exe', 'lld-link.exe', 'clang.exe', 'clang-cl.exe',
                       'gcc.exe', 'g++.exe', 'ctest.exe', 'devenv.exe'}
        busy = []
        for process in processes:
            pid = int(process['ProcessId'])
            executable = process['Name'].lower()
            is_python = executable.startswith('python') and executable.endswith('.exe')
            is_xlang = executable.startswith('xlang3') and executable.endswith('.exe')
            if pid not in self.allowed_pids and (executable in build_names or is_python or is_xlang):
                busy.append(process)
        guard = {'name': name, 'allowed_controller_pids': sorted(self.allowed_pids),
                 'busy_processes': busy, 'status': 'blocked' if busy else 'idle'}
        self.record['idle_guards'].append(guard)
        self.save()
        if busy:
            self.record['status'] = 'blocked_active_processes'
            self.save()
            raise CheckFailure('Active build/test/benchmark processes: ' + json.dumps(busy))

    def finish(self, error=None):
        if error is not None:
            self.record['error'] = type(error).__name__ + ': ' + str(error)
            if self.record['status'] == 'running':
                self.record['status'] = 'failed'
        try:
            self.record['hashes_after'] = snapshot()
            self.record['hashes_unchanged'] = self.record.get('hashes_before') == self.record['hashes_after']
            if not self.record['hashes_unchanged']:
                self.record['status'] = 'failed_hash_integrity'
        except Exception as exc:
            self.record['hashes_after_error'] = type(exc).__name__ + ': ' + str(exc)
            self.record['status'] = 'failed_hash_integrity'
        if error is None and self.record['status'] == 'running':
            self.record['status'] = 'terminal'
        self.record['terminal_record'] = True
        self.save()
        if self.record['status'] != 'terminal':
            raise CheckFailure(self.record['status']) from error
