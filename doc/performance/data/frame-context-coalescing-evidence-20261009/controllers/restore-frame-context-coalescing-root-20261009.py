"""Archive rejected trial exactly; restore only owned source and accepted Release."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/trace-disable-local-events-accepted-20261009'
TRIAL = ROOT / 'build-repro/controls/frame-context-coalescing-rejected-20261009'
APP = DATA / 'frame-context-coalescing-applied-source-20261009.json'
BUILD = DATA / 'frame-context-coalescing-build-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda d: {p.relative_to(d).as_posix(): sha(p) for p in sorted(d.rglob('*')) if p.is_file()}
read = lambda p: json.loads(p.read_bytes())
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve() and sys.version_info[:3] == (3, 14, 7)
app, build, control = map(read, (APP, BUILD, CONTROL / 'preserved-release-provenance.json'))
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert tree(RELEASE) == build['release_sha256']
assert tree(CONTROL / 'Release') == control['files_sha256']
assert tree(CONTROL / 'sources') == control['source_snapshot_sha256']
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
measurements = {}
for name, expected in (
    ('frame-context-coalescing-diagnostic-20261009.json', '8cdada5aee61b7ce50fab4517c2b93b30d8f135b0c2da8cb1c259228c272978d'),
    ('frame-context-coalescing-official-20261009.json', 'ec9fd63863effc8524a064e2ce8bfe2967b66577ba5e2c5ab3118a60ad309dba'),
    ('frame-context-coalescing-official-rigorous-20261009.json', '8c3a5f91223c2cbfd4492fdfa6644084a79b3c3ffee39dfd92ca90b7e5f3b18a'),
    ('frame-context-coalescing-call-heavy-official-20261009.json', 'f053321169adf46332ca287d6ecb5cec2148526e4b01a4bfe0ccaf6a742cabd6')):
    p = DATA / name
    assert sha(p) == expected
    receipt = read(p)
    assert receipt['terminal'] and receipt['hashes_unchanged'] and receipt['status'] == 'measurement_completed'
    measurements[name] = expected
assert not TRIAL.exists()
OUT = DATA / 'frame-context-coalescing-rejected-restored-20261009.json'
assert not OUT.exists()
raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
rows = json.loads(raw.decode('utf-8-sig') or '[]')
if isinstance(rows, dict): rows = [rows]
import os
tools = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe', 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'}
busy = [p for p in rows if p['ProcessId'] != os.getpid() and (p['Name'].lower() in tools or p['Name'].lower().startswith(('python', 'xlang3')))]
assert not busy, busy
TRIAL.mkdir()
shutil.copytree(RELEASE, TRIAL / 'Release')
for p in app['source_sha256']:
    dst = TRIAL / 'sources' / p
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, dst)
assert tree(TRIAL / 'Release') == build['release_sha256']
assert tree(TRIAL / 'sources') == app['source_sha256']
archive = DATA / 'frame-context-coalescing-trial-sources-20261009'
assert not archive.exists()
for p in app['owned_paths']:
    dst = archive / p
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / p, dst)
for p in app['owned_paths']:
    target = (ROOT / p).resolve()
    assert target.is_relative_to(ROOT.resolve())
    if p in control['source_snapshot_sha256']:
        shutil.copyfile(CONTROL / 'sources' / p, target)
    else:
        assert sha(target) == app['source_sha256'][p]
        target.unlink()
assert set(build['release_sha256']) == set(control['files_sha256'])
for p in control['files_sha256']:
    shutil.copyfile(CONTROL / 'Release' / p, RELEASE / p)
assert all(sha(ROOT / p) == h for p, h in control['source_snapshot_sha256'].items())
assert tree(RELEASE) == control['files_sha256']
assert tree(ROOT / 'build-repro/Release') == app['fixed_baseline_sha256']
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
result = {'terminal': True, 'status': 'rejected_trial_exact_accepted_checkpoint_restored',
    'reason': 'Balanced call diagnostic improved, but original rigorous unpickle has no significant gain; three preselected official call-heavy cases show small/mixed changes. No engine change retained.',
    'head': app['head'], 'measurements_sha256': measurements, 'rejected_control': str(TRIAL),
    'trial_sources_sha256': app['source_sha256'], 'trial_release_sha256': build['release_sha256'],
    'restored_source_count': len(control['source_snapshot_sha256']), 'restored_source_sha256': control['source_snapshot_sha256'],
    'restored_release_sha256': control['files_sha256'], 'fixed_baseline_sha256': app['fixed_baseline_sha256'],
    'unowned_tracked_dirty_sha256': app['unowned_tracked_dirty_sha256'],
    'owned_trial_archive_sha256': tree(archive), 'controller_sha256': sha(__file__),
    'restoration_method': 'Exact preserved source bytes and complete Release178 copied back to original path, not a fresh rebuild. Ninja objects from trial remain and changed source mtimes require a rebuild before future candidate timing.',
    'gate_run_for_rejected_candidate': False, 'engine_change_retained': False}
OUT.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({'status': result['status'], 'source_count': result['restored_source_count'], 'receipt_sha256': sha(OUT)}, indent=2))
