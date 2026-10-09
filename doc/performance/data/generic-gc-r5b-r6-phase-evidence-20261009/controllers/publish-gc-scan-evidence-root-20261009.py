"""Publish immutable GC evidence only; pending/failed engine changes are not staged."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
ARCHIVE = DATA / 'generic-gc-scan-evidence-20261009'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
app = read(DATA / 'gc-generic-cycles-applied-source-r4-20261009.json')
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
for receipt in ('gc-generic-cycles-performance-20261009.json', 'gc-generic-cycles-performance-r3-20261009.json'):
    row = read(DATA / receipt)
    assert row['terminal'] and row['hashes_unchanged'] and not row['fixed_gate']['passed']
assert subprocess.run(['git', 'diff', '--cached', '--quiet'], cwd=ROOT).returncode == 0
assert not ARCHIVE.exists()
ARCHIVE.mkdir()
for label, base in (
    ('r2', ROOT / 'build-repro/controls/gc-generic-cycles-r2-failed-gate-20261009/sources'),
    ('r3', ROOT / 'build-repro/controls/gc-generic-cycles-r3-failed-gate-20261009/sources'),
    ('r4-pending', ROOT)):
    parent = read(DATA / ('gc-generic-cycles-applied-source-' + ('r4' if label == 'r4-pending' else label) + '-20261009.json'))
    for name in app['owned_paths']:
        target = ARCHIVE / label / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(base / name, target)
        assert sha(target) == parent['source_sha256'][name]
controller_names = ('apply-gc-', 'update-gc-', 'build-gc-', 'check-gc-', 'validate-gc-', 'prepare-gc-', 'resume-gc-', 'publish-gc-')
for source in sorted((ROOT / 'scratch/performance').glob('*20261009.py')):
    if source.name.startswith(controller_names):
        target = ARCHIVE / 'controllers' / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
report = ROOT / 'doc/performance/generic-cycle-discovery-and-scan-cost-20261009.md'
paths = [report]
paths.extend(p for p in sorted(DATA.rglob('*')) if p.is_file() and
    (p.is_relative_to(ARCHIVE) or
     (p.relative_to(DATA).parts[0].startswith('gc-generic-cycles-') and '-idle' not in p.relative_to(DATA).parts[0])))
maps = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
receipt = DATA / 'generic-gc-scan-publication-20261009.json'
assert not receipt.exists()
receipt.write_text(json.dumps({'status': 'immutable_docs_evidence_only', 'engine_paths_staged': [],
    'working_candidate_application_sha256': sha(DATA / 'gc-generic-cycles-applied-source-r4-20261009.json'),
    'pending_idle_wait_excluded': True, 'artifact_sha256': maps, 'controller_sha256': sha(__file__),
    'scope': 'Failed R2/R3 gate evidence, R4 correctness and preflight refusal. R4 timing remains pending; no engine acceptance or full97 update.'}, indent=2) + '\n', encoding='utf-8', newline='\n')
maps[receipt.relative_to(ROOT).as_posix()] = sha(receipt)
for name, expected in maps.items():
    # Raw benchmark transcripts must retain their exact bytes despite CRLF
    # attributes. Stage verified raw Git blobs, never a shell-escaped body.
    blob = subprocess.check_output(['git', 'hash-object', '-w', '--no-filters', name], cwd=ROOT, text=True).strip()
    subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name], cwd=ROOT, check=True)
    stored = subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT)
    assert hashlib.sha256(stored).hexdigest() == expected
staged = set(subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, text=True).splitlines())
assert staged == set(maps) and all(p.startswith('doc/performance/') for p in staged)
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
print(json.dumps({'staged_files': len(maps), 'engine_files': 0, 'publication_sha256': sha(receipt)}, indent=2))
