"""Run two untimed date native-boundary children with the actual current DLL."""
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
SOURCE = ROOT / 'scratch/performance/date-native-boundary-untimed-diagnostic-proposed-20261009.py'
PROOF = ROOT / 'scratch/performance/date-native-boundary-untimed-diagnostic-provenance-proposed-20261009.json'
BUILD = DATA / 'property-callable-getter-build-terminal-20261009.json'
OUT = DATA / 'date-native-boundary-untimed-diagnostic-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.isolated and not sys.flags.optimize and not OUT.exists()
assert sha(SOURCE) == '7fdda0ebbaa1851cb1e0dff4ca39b7cff56a0fa672675d74d504ac02693c2617'
assert sha(PROOF) == '437a77652c9202af3a8cdad2c45dbcfe26fa8b298a9238d6408fa97239ec54ba'
assert sha(BUILD) == 'a624e71483e86cd50c641629f00c01091bd266954713eeebb24621f9b61efc24'
ast.parse(SOURCE.read_bytes())
build = json.loads(BUILD.read_bytes())
proof = json.loads(PROOF.read_bytes())
before = {str(ROOT / p): h for p, h in build['source_sha256'].items()}
before.update({str(RELEASE / p): h for p, h in build['binaries_sha256'].items()})
for p in [Path(__file__), SOURCE, PROOF, BUILD, CP, CP.parent / 'python314.dll']:
    before[str(p)] = sha(p)
before.update({str(Path(p)): h for p, h in proof['original_sources'].items()})
assert all(sha(p) == h for p, h in before.items())
record = dict(status='preflight', terminal=False, timed=False, scored=False, phases=[], hashes_before=before)
def save():
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
env = os.environ.copy()
env['PATH'] = str(RELEASE) + os.pathsep + env.get('PATH', '')
env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')
for key in ['PYTHONPATH', 'PYTHONHOME', 'PYTHONOPTIMIZE', 'PYTHONIOENCODING', 'PYTHONPYCACHEPREFIX']:
    env.pop(key, None)
try:
    for runtime, executable, dll in [('cpython3147', CP, CP.parent / 'python314.dll'),
                                    ('xlang3', RELEASE / 'xlang3.exe', RELEASE / 'xlang3_runtime.dll')]:
        command = [str(executable)] + (['-I'] if runtime == 'cpython3147' else []) + [str(SOURCE), '--runtime', runtime,
                    '--exe-sha256', sha(executable), '--dll-sha256', sha(dll)]
        stdout, stderr = DATA / ('date-native-boundary-untimed-20261009-' + runtime + '.stdout.log'), DATA / ('date-native-boundary-untimed-20261009-' + runtime + '.stderr.log')
        row = dict(runtime=runtime, command=command, timed=False)
        record['phases'].append(row)
        save()
        with stdout.open('xb') as out, stderr.open('xb') as err:
            child = subprocess.run(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                   timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
        row.update(exit_code=child.returncode, stdout_log=stdout.name, stdout_sha256=sha(stdout),
                   stderr_log=stderr.name, stderr_sha256=sha(stderr))
        assert child.returncode == 0 and not stderr.read_bytes(), (runtime, child.returncode, stderr.read_bytes())
        result = json.loads(stdout.read_bytes())
        assert result['status'] == 'untimed_boundary_diagnostic_passed' and result['hashes_unchanged']
        row['result'] = result
    record['status'] = 'terminal_untimed_boundary_diagnostic_passed'
except BaseException as error:
    record.update(status='terminal_untimed_boundary_diagnostic_failed', error=repr(error))
finally:
    record['hashes_after'] = {p: sha(p) for p in before}
    record['hashes_unchanged'] = before == record['hashes_after']
    record['terminal'] = True
    save()
print(record['status'], sha(OUT))
for row in record['phases']:
    print(row['runtime'], row.get('exit_code'), row.get('result', {}).get('native_datetime_imported'), row.get('result', {}).get('reducer'))
if record.get('error'):
    print(record['error'])
raise SystemExit(0 if record['status'] == 'terminal_untimed_boundary_diagnostic_passed' and record['hashes_unchanged'] else 1)
