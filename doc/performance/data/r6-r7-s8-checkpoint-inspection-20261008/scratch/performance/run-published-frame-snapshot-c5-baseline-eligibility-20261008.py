"""Verify the public proof's sole expected failure on the preserved C5 DLL."""
import hashlib, json, os, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
PREFIX = 'published-frame-snapshot-c5-baseline-eligibility-20261008'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sys.version_info[:3] == (3,14,7)
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
inventory = DATA / 'class-constructor-plan-c5-applied-source-20261008.json'
assert sha(inventory) == '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937'
sources = json.loads(inventory.read_bytes())['source_sha256']
control = ROOT / 'build-repro/controls/class-constructor-c5-before-snapshot-r6-20261008'
manifest = control / 'preserved-release-provenance.json'
assert sha(manifest) == '85910ee375f16c677153fc128fb17303868a849c124f5909d5ee03eeed764bdc'
preserved = json.loads(manifest.read_bytes())
exe = ROOT / 'scratch/performance/published-frame-snapshot-c5-baseline-eligibility-20261008-bin/proof.exe'
header = ROOT / 'scratch/performance/published-frame-snapshot-tests-proposed-20261008-candidates/tests/cpp/published_frame_snapshot_cases.h'
assert sha(header) == '6bccd0d990bb438cc92d2de029bf323c2f1ff48ca36a13e3d72723261b7b4e83'
assert not exe.with_name('xlang3_runtime.dll').exists()
pins = {str(ROOT / p):h for p,h in sources.items()}
pins.update({str(control / p):h for p,h in preserved['files_sha256'].items()})
pins.update({str(exe):sha(exe),str(header):sha(header)})
def stable(): return all(sha(p) == h for p,h in pins.items())
assert stable()
rows = json.loads(subprocess.check_output(['powershell','-NoProfile','-Command','Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig'))
assert not any(r['ProcessId'] != os.getpid() and (r['Name'].lower().startswith(('python','xlang3')) or r['Name'].lower() in {'cl.exe','link.exe','ninja.exe','ctest.exe','cmake.exe','msbuild.exe'}) for r in rows)
assert not (DATA / (PREFIX+'.json')).exists()
env = os.environ.copy()
env.update(PATH=str(control)+os.pathsep+env.get('PATH',''),XLANG3_PYTHON_LIB='C:/Python/Python314/Lib')
result = subprocess.run([str(exe)],cwd=ROOT,env=env,capture_output=True,timeout=120)
for stream,raw in [('stdout',result.stdout),('stderr',result.stderr)]:
    (DATA / (PREFIX+'.'+stream+'.log')).write_bytes(raw)
expected = b'32 real same-owner publications refresh scalars with zero Module Value retains/releases: retains=32, releases=32\n'
matched = result.returncode == 1 and result.stdout == b'' and result.stderr.replace(b'\r\n',b'\n') == expected
record = dict(status='expected_baseline_inactive_optimization_proof' if matched else 'unexpected_baseline_test_failure',
 terminal=True,diagnostic_only=True,performance_validated=False,exit_code=result.returncode,
 expected_failure_only=matched,pins=pins,hashes_unchanged=stable(),
 stdout_sha256=sha(DATA / (PREFIX+'.stdout.log')),stderr_sha256=sha(DATA / (PREFIX+'.stderr.log')))
(DATA / (PREFIX+'.json')).write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(result.stderr.decode('utf-8',errors='replace'))
print(record['status'],record['hashes_unchanged'])
raise SystemExit(0 if matched and record['hashes_unchanged'] else 1)
