"""Seven alternating pairs on the frozen diagnostic; no official suite claim."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
source = root / 'scratch/performance/python-hash-callback-cost-probe-20261008.py'
assert sha(source) == '3823fc09d47caa35ed158677a5a565ad1118c64b1ef9e345a6584a717f8fca70'
control = root / 'build-repro/controls/native-bound-zero-args-checkpoint-20261008/xlang3.exe'
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
manifest = json.loads(control.with_name('preserved-release-provenance.json').read_text(encoding='utf-8'))
assert manifest['accepted'] and all(sha(control.parent / name) == value for name, value in manifest['files_sha256'].items())
applied = json.loads((root / 'doc/performance/data/interpreter-lazy-globals-final-source-20261008.json').read_text(encoding='utf-8'))
engine = root / 'src/internal/xlang3/interpreter.h'
assert sha(engine) == applied['source_sha256']['src/internal/xlang3/interpreter.h']
data = root / 'doc/performance/data'
prefix = 'interpreter-lazy-globals-paired-20261008'
output = data / (prefix + '.json')
assert not output.exists()
paths = [executable.with_name(name) for executable in (control, candidate)
         for name in ('xlang3.exe', 'xlang3_runtime.dll')]
before = {str(p): sha(p) for p in paths}
source_before = {str(root / p): value for p, value in applied['source_sha256'].items()}
assert all(sha(Path(p)) == value for p, value in source_before.items())
env = os.environ.copy()
env['XLANG3_PYTHON_LIB'] = r'C:\Python\Python314\Lib'
for name in ('PYTHONPATH', 'PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX'):
    env.pop(name, None)
record = dict(status='running', diagnostic_only=True, pairs=7, samples_per_pair=5,
              source_sha256=sha(source), engine_source_sha256=sha(engine),
              binaries_sha256=before, raw=[])
def save():
    output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
def idle():
    rows = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command',
        'Get-CimInstance Win32_Process | Select-Object ProcessId,Name | ConvertTo-Json -Compress']).decode('utf-8-sig'))
    if isinstance(rows, dict): rows = [rows]
    forbidden = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe'}
    assert not [r for r in rows if r['ProcessId'] != os.getpid() and
                (r['Name'].lower() in forbidden or r['Name'].lower().startswith(('python', 'xlang3')))]
try:
    for pair in range(7):
        for label in (('control', 'candidate') if pair % 2 == 0 else ('candidate', 'control')):
            idle()
            executable = control if label == 'control' else candidate
            logfile = data / f'{prefix}-{pair}-{label}.log'
            assert not logfile.exists()
            child_env = env.copy()
            child_env['PYTHONPYCACHEPREFIX'] = str(root / 'scratch/performance/pycache-lazy-globals' / label)
            result = subprocess.run([str(executable), str(source)], cwd=root, env=child_env,
                                    capture_output=True, timeout=90)
            logfile.write_bytes(result.stdout + result.stderr)
            assert result.returncode == 0, logfile
            parsed = json.loads(result.stdout.decode('utf-8'))
            assert parsed['instrumentation'] == 'none' and len(parsed['rows']) == 8
            record['raw'].append(dict(pair=pair, runtime=label, result=parsed,
                                      log=logfile.name, log_sha256=sha(logfile)))
            save()
        print('Pair', pair + 1, 'completed', flush=True)
    rows = []
    for index in range(8):
        ratios = []
        control_samples, candidate_samples = [], []
        for pair in range(7):
            a = next(r['result']['rows'][index] for r in record['raw'] if r['pair'] == pair and r['runtime'] == 'control')
            b = next(r['result']['rows'][index] for r in record['raw'] if r['pair'] == pair and r['runtime'] == 'candidate')
            assert all(a[k] == b[k] for k in ('path', 'operations', 'checksum', 'key_text'))
            assert a['operations'] == 16384 and len(a['samples_seconds']) == len(b['samples_seconds']) == 5
            ratios.append(statistics.median(a['samples_seconds']) / statistics.median(b['samples_seconds']))
            control_samples.extend(a['samples_seconds'])
            candidate_samples.extend(b['samples_seconds'])
        rows.append(dict(path=a['path'], control_over_candidate_speed=statistics.median(ratios),
                         pair_ratios=ratios, control_median_seconds=statistics.median(control_samples),
                         candidate_median_seconds=statistics.median(candidate_samples)))
    record['summary'] = rows
    record['status'] = 'terminal_diagnostic_only'
    for row in rows: print(row['path'], format(row['control_over_candidate_speed'], '.3f') + 'x', flush=True)
except Exception as exc:
    record['status'] = 'failed'
    record['error'] = repr(exc)
    raise
finally:
    record['hashes_unchanged'] = all(sha(Path(p)) == value for p, value in before.items()) and sha(engine) == record['engine_source_sha256'] and sha(source) == record['source_sha256']
    record['source_hashes_unchanged'] = all(sha(Path(p)) == value for p, value in source_before.items())
    assert record['hashes_unchanged'] and record['source_hashes_unchanged']
    record['terminal'] = True
    save()
