"""Attribution only: unchanged fixed GC workload in five fresh diagnostic processes."""
import atexit
import shutil
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
app_path = DATA / 'gc-generic-cycles-r5b-phase-diagnostic-applied-20261009.json'
build_path = DATA / 'gc-generic-cycles-r5b-phase-diagnostic-build-20261009.json'
assert sha(app_path) == '90737b0ecf15e26ac81afaa333893ff21f9b294d009e95b99e37007a0fef2be2'
assert sha(build_path) == '966f04993b0aad4a96a989fe46334e4d3c2e89db2bc0d2cd1afbc68758a82888'
app, build = read(app_path), read(build_path)
assert build['passed'] and build['sources_unchanged']
normal = read(DATA / 'gc-generic-cycles-r5b-before-phase-20261009.json')
normal_control = ROOT / 'build-repro/controls/gc-generic-cycles-r5b-failed-gate-20261009'
diag_control = ROOT / 'build-repro/controls/gc-r5b-phase-diagnostic-ready-20261009'
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(RELEASE / p) == h for p, h in build['release_sha256'].items())
assert not diag_control.exists()
diag_control.mkdir()
shutil.copytree(RELEASE, diag_control / 'Release')
for p, h in app['source_sha256'].items():
    target = diag_control / 'sources' / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, target)
    assert sha(target) == h
assert all(sha(diag_control / 'Release' / p) == h for p, h in build['release_sha256'].items())
(diag_control / 'provenance.json').write_text(json.dumps({'diagnostic_only': True,
    'source_sha256': app['source_sha256'], 'release_sha256': build['release_sha256'],
    'application_sha256': sha(app_path), 'build_sha256': sha(build_path)}, indent=2) + '\n', encoding='utf-8')
def restore_normal(controller_hash=sha(__file__)):
    for p in ('src/runtime/modules/system/gc_plain_cycles.h', 'src/runtime/modules/system/gc_module.cpp'):
        shutil.copyfile(normal_control / 'sources' / p, ROOT / p)
    for p in normal['restored_release_sha256']: shutil.copyfile(normal_control / 'Release' / p, RELEASE / p)
    assert all(sha(ROOT / p) == h for p, h in normal['restored_source_sha256'].items())
    assert all(sha(RELEASE / p) == h for p, h in normal['restored_release_sha256'].items())
    out = DATA / 'gc-generic-cycles-r5b-after-phase-restored-20261009.json'
    assert not out.exists()
    out.write_text(json.dumps({'terminal': True, 'status': 'exact_normal_r5b_restored',
        'source_sha256': normal['restored_source_sha256'], 'release_sha256': normal['restored_release_sha256'],
        'temporary_instrumentation_active': False, 'controller_sha256': controller_hash}, indent=2) + '\n', encoding='utf-8')
    print('Exact normal R5b restored; instrumentation inactive.', flush=True)
atexit.register(restore_normal)

pins = {str(ROOT / p): h for p, h in app['source_sha256'].items()}
pins.update({str(RELEASE / p): h for p, h in build['release_sha256'].items()})
pins.update({str(ROOT / 'build-repro/Release' / p): h for p, h in app['fixed_baseline_sha256'].items()})
case = ROOT / 'benchmarks/cases/gc_traversal.py'
pins[str(case)] = '778c764389c326a2be4232dd08eb53cc16ee67c5d71788618120aff551a68f81'
env = os.environ.copy()
for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'XLANG3_VM_OPCODE_TIMING'):
    env.pop(name, None)
env['XLANG3_PYTHON_LIB'] = 'C:/Python/Python314/Lib'
rows, phases = [], []
receipt = DATA / 'gc-generic-cycles-r5b-phase-diagnostic-20261009.json'
assert not receipt.exists()
record = {'terminal': False, 'status': 'diagnostic_running', 'source_sha256': app['source_sha256'],
    'release_sha256': build['release_sha256'], 'application_sha256': sha(app_path),
    'build_sha256': sha(build_path), 'controller_sha256': sha(__file__), 'phases': phases,
    'scope': 'Temporary instrumentation on the unchanged fixed GC gate case; phase attribution only, no official score or performance acceptance. Diagnostic output cost excluded from following plain phases but included in outer plain_total.'}
def save(): receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
def stable(): return all(sha(p) == h for p, h in pins.items())
spec = importlib.util.spec_from_file_location('phase_activity_watch', ROOT / 'scratch/performance/gc-phase-dormant-msbuild-activity-watch-20261009.py')
watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
record['dormant_worker_admission'] = watcher.admit_dormant_worker(34420)
record['r4_phase_reference_sha256'] = sha(DATA / 'gc-generic-cycles-phase-diagnostic-window-20261009.json')
record['watcher_sha256'] = sha(spec.origin)
pins[spec.origin] = sha(spec.origin)
def idle():
    observation = watcher.idle_snapshot()
    assert not observation['busy'], observation['busy']
save()
try:
    for name, script, expected in [('oracle', ROOT / 'tests/fixtures/core/gc_generic_cycles.py',
            (ROOT / 'tests/fixtures/expected/gc_generic_cycles.out').read_text())] + [
            ('traversal-' + str(i), case, '999 998') for i in range(1, 6)]:
        assert stable(); idle()
        stdout = DATA / ('gc-generic-cycles-r5b-phase-diagnostic-' + name + '.stdout.log')
        stderr = DATA / ('gc-generic-cycles-r5b-phase-diagnostic-' + name + '.stderr.log')
        phase_row = {'name': name}
        finish = watcher.start_timing_process_watch('gc-generic-cycles-r5b-phase-diagnostic-20261009', name, phase_row)
        try:
            with stdout.open('xb') as out, stderr.open('xb') as err:
                result = subprocess.run([str(RELEASE / 'xlang3.exe'), str(script)], cwd=ROOT, env=env,
                    stdout=out, stderr=err, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        finally:
            valid = finish()
        assert valid, phase_row
        assert result.returncode == 0 and stdout.read_text().strip() == expected.strip()
        if name.startswith('traversal'):
            parsed = []
            for line in stderr.read_text().splitlines():
                parts = line.split()
                assert len(parts) == 5 and parts[0] == 'XLANG3_GC_PHASE', line
                parsed.append({'process': name, 'group': parts[1], 'phase': parts[2], 'ns': int(parts[3]), 'count': int(parts[4])})
            assert len([p for p in parsed if p['phase'] == 'snapshot']) == 4
            rows.extend(parsed)
        phases.append({**phase_row, 'exit_code': result.returncode,
            'stdout': stdout.name, 'stdout_sha256': sha(stdout), 'stderr': stderr.name, 'stderr_sha256': sha(stderr)})
        assert stable(); idle(); save()
    summary = {}
    for row in rows:
        key = row['group'] + '.' + row['phase']
        summary.setdefault(key, []).append(row['ns'])
    record.update(terminal=True, status='diagnostic_completed', raw_phase_rows=rows,
        median_phase_us={key: statistics.median(values) / 1000 for key, values in summary.items()},
        sources_and_releases_unchanged=stable())
    save(); print(json.dumps(record['median_phase_us'], indent=2))
except BaseException as error:
    record.update(terminal=True, status='diagnostic_failed', error=repr(error)); save(); raise
