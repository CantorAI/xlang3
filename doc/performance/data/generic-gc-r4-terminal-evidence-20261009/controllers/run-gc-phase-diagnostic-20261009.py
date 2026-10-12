"""Attribution only: unchanged fixed GC workload in five fresh diagnostic processes."""
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
app_path = DATA / 'gc-generic-cycles-phase-diagnostic-applied-20261009.json'
build_path = DATA / 'gc-generic-cycles-phase-diagnostic-build-20261009.json'
assert sha(app_path) == 'e4f795971c0d5a7e42085c3e5081da010c7c031af2f4d35cb13bca47a267007d'
assert sha(build_path) == '4755c35a1ec8b82e4062a23a748c046b7268d193ca16abbeec0c05db8dbd8ffb'
app, build = read(app_path), read(build_path)
assert build['passed'] and build['sources_unchanged']
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
receipt = DATA / 'gc-generic-cycles-phase-diagnostic-20261009.json'
assert not receipt.exists()
record = {'terminal': False, 'status': 'diagnostic_running', 'source_sha256': app['source_sha256'],
    'release_sha256': build['release_sha256'], 'application_sha256': sha(app_path),
    'build_sha256': sha(build_path), 'controller_sha256': sha(__file__), 'phases': phases,
    'scope': 'Temporary instrumentation on the unchanged fixed GC gate case; phase attribution only, no official score or performance acceptance. Diagnostic output cost excluded from following plain phases but included in outer plain_total.'}
def save(): receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
def stable(): return all(sha(p) == h for p, h in pins.items())
def idle():
    raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
    procs = json.loads(raw.decode('utf-8-sig'))
    if isinstance(procs, dict): procs = [procs]
    tools = {'cl.exe', 'link.exe', 'cmake.exe', 'ctest.exe', 'msbuild.exe', 'ninja.exe'}
    busy = [p for p in procs if p['ProcessId'] != os.getpid() and (p['Name'].lower() in tools or p['Name'].lower().startswith(('python', 'xlang3')))]
    assert not busy, busy
save()
try:
    for name, script, expected in [('oracle', ROOT / 'tests/fixtures/core/gc_generic_cycles.py',
            (ROOT / 'tests/fixtures/expected/gc_generic_cycles.out').read_text())] + [
            ('traversal-' + str(i), case, '999 998') for i in range(1, 6)]:
        assert stable(); idle()
        stdout = DATA / ('gc-generic-cycles-phase-diagnostic-' + name + '.stdout.log')
        stderr = DATA / ('gc-generic-cycles-phase-diagnostic-' + name + '.stderr.log')
        with stdout.open('xb') as out, stderr.open('xb') as err:
            result = subprocess.run([str(RELEASE / 'xlang3.exe'), str(script)], cwd=ROOT, env=env,
                stdout=out, stderr=err, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode == 0 and stdout.read_text().strip() == expected.strip()
        if name.startswith('traversal'):
            parsed = []
            for line in stderr.read_text().splitlines():
                parts = line.split()
                assert len(parts) == 5 and parts[0] == 'XLANG3_GC_PHASE', line
                parsed.append({'process': name, 'group': parts[1], 'phase': parts[2], 'ns': int(parts[3]), 'count': int(parts[4])})
            assert len([p for p in parsed if p['phase'] == 'snapshot']) == 4
            rows.extend(parsed)
        phases.append({'name': name, 'exit_code': result.returncode,
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
