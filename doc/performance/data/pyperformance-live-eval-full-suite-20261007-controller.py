"""One sequential full refresh; failed definitions stay failed, not scored."""
import datetime
import hashlib
import importlib.metadata
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7), sys.version
root = Path.cwd()
data = root / 'doc/performance/data'
python = Path(r'C:\Python\Python314\python.exe')
assert Path(sys.executable).resolve() == python.resolve()
exe = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
proof = json.loads((data / 'eval-live-namespace-checkpoint-20261007.json').read_text(encoding='utf-8'))
assert proof['status'] == 'correctness_fixed_gate_and_official_genshi_passed'
assert {kind: digest(exe if kind == 'exe' else exe.with_name('xlang3_runtime.dll')) for kind in ('exe', 'dll')} == proof['binary_sha256']['xlang3']
gate = json.loads((data / 'release-live-eval-fixed-gate-20261007.json').read_text(encoding='utf-8'))
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
assert head == '0ab4624f500e0677364ebdcafbbb35ff0c329fc9'
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], text=True).strip()
spec = importlib.util.spec_from_file_location('summary', root / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py')
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)
benchmark_root = python.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks'
benchmark_sources = {str(p.relative_to(benchmark_root)): digest(p) for p in sorted(benchmark_root.rglob('*.py'))}
assert benchmark_sources
dependency_site = root / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
dependency_versions = {str(p.relative_to(dependency_site)): digest(p)
                       for p in sorted(dependency_site.glob('*.dist-info/METADATA'))}
source_hashes = {path: digest(root / path) for path in proof['source_sha256_raw']}
for path, expected in proof['source_sha256_canonical_lf'].items():
    assert hashlib.sha256((root / path).read_bytes().replace(b'\r\n', b'\n')).hexdigest() == expected, path
hook = root / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
hook_hash = digest(hook)
runner = root / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
runner_hash = digest(runner)
prefixes = {'xlang3': 'pyperformance-xlang3-live-eval-full-fast-20261007',
            'cpython3147': 'pyperformance-cpython3147-live-eval-full-fast-20261007'}
for prefix in prefixes.values():
    assert not any((data / (prefix + suffix)).exists() for suffix in ('.json', '.log', '-provenance.json'))
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = str(python.parent / 'Lib')
env['PYTHONPATH'] = str(hook.parent)
env['PYTHONIOENCODING'] = 'utf-8'
for name, runtime in [('xlang3', exe), ('cpython3147', python)]:
    prefix = prefixes[name]
    output = data / (prefix + '.json')
    provenance = data / (prefix + '-provenance.json')
    dll = runtime.with_name('xlang3_runtime.dll' if name == 'xlang3' else 'python314.dll')
    files = {'exe': runtime, 'dll': dll}
    if name == 'xlang3':
        files['hashlib'] = runtime.parent / 'modules/xlang__hashlib.x3pkg.dll'
    env['PYTHONPYCACHEPREFIX'] = str(root / ('scratch/performance/pycache-' + name + '-live-eval-full-20261007'))
    command = [str(python), str(runner), '--runtime', str(runtime), '--benchmarks', 'all', '--mode', 'fast',
               '--case-timeout', '300', '--case-timeout-override', 'networkx*=600',
               '--dependency-site', str(dependency_site), '--output', str(output)]
    record = {'status': 'running', 'pid': os.getpid(), 'source_base_commit': head,
              'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'runtime_executable': str(runtime), 'runtime_version': '3.14.7' if name == 'cpython3147' else 'XLang3',
              'manager_executable': sys.executable, 'manager_version': sys.version,
              'pyperformance_version': importlib.metadata.version('pyperformance'),
              'pyperf_version': importlib.metadata.version('pyperf'),
              'mode': 'fast', 'benchmarks': 'all', 'expected_definitions': 97,
              'case_timeout_seconds': 300, 'case_timeout_overrides': {'networkx*': 600},
              'sha256_start': {kind: digest(path) for kind, path in files.items()},
              'source_sha256': source_hashes, 'benchmark_python_sources': benchmark_sources,
              'dependency_metadata_sha256': dependency_versions, 'dependency_site': str(dependency_site),
              'compatibility_hook_sha256': hook_hash, 'runner_sha256': runner_hash,
              'fixed_gate': 'release-live-eval-fixed-gate-20261007.json', 'fixed_gate_exit_code': 0,
              'validation': 'eval-live-namespace-validation-r3-20261007.json',
              'command': command,
              'scope': 'Attempt all 97 definitions; record failures/timeouts and partial evidence without scoring failed definitions',
              'previous_frozen_full_report': 'pyperformance-xlang3-super-method-call-full-fast-20261007.json',
              'paired_with': prefixes['cpython3147' if name == 'xlang3' else 'xlang3']}
    with (data / (prefix + '.log')).open('w', encoding='utf-8') as log:
        process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
        record['manager_child_pid'] = process.pid
        provenance.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        print('Full suite started:', name, 'pid', process.pid, flush=True)
        for line in process.stdout:
            log.write(line)
            log.flush()
            print(line, end='', flush=True)
        code = process.wait()
    text = (data / (prefix + '.log')).read_text(encoding='utf-8')
    sections = summary.case_sections(text)
    record.update(status='finished' if code == 0 else 'finished_with_benchmark_failures',
                  exit_code=code, completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  attempted_definitions=len(sections), sha256_end={kind: digest(path) for kind, path in files.items()})
    failures, _ = summary.failure_details(text)
    record['failed_definitions'] = failures
    record['recorded_subtests'] = len(summary.benchmark_map(json.loads(output.read_text(encoding='utf-8')))) if output.exists() else 0
    provenance.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    assert record['sha256_start'] == record['sha256_end']
    assert len(sections) == 97, 'Manager terminated without attempting every definition'
    assert {str(p.relative_to(benchmark_root)): digest(p) for p in sorted(benchmark_root.rglob('*.py'))} == benchmark_sources
    assert {str(p.relative_to(dependency_site)): digest(p) for p in sorted(dependency_site.glob('*.dist-info/METADATA'))} == dependency_versions
    assert {path: digest(root / path) for path in source_hashes} == source_hashes
    assert digest(hook) == hook_hash and digest(runner) == runner_hash
    print('Full suite terminal:', name, 'exit', code, 'attempted', len(sections), 'failures', len(failures), flush=True)
print('Both full suites terminal; generate and verify the all-97 comparison next', flush=True)
