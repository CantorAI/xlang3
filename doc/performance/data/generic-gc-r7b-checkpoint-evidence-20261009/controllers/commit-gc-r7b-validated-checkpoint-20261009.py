"""Commit the authorized validated checkpoint, excluding unrelated dirty files."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
sha_bytes = lambda b: hashlib.sha256(b).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
publication = DATA / 'generic-gc-r7b-checkpoint-publication-20261009.json'
assert sha(publication) == '26bb0bbac2dea1cc2226bf5c7220af46e0960b0acd5420c705756a187b59bbc6'
pub = read(publication)
app = read(DATA / 'gc-generic-cycles-applied-source-r7b-20261009.json')
state = read(DATA / 'gc-generic-cycles-r7b-preserved-validated-checkpoint-20261009.json')
assert read(DATA / 'gc-generic-cycles-r7b-acceptance-audit-20261009.json')['engine_commit_permitted']
assert subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip() == 'main'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == '3099f541e4be6557025608bcc7f5a813d365f769'
for filename in ('normalize-gc-checkpoint-staged-source-20261009.py', Path(__file__).name):
    source = ROOT / 'scratch/performance' / filename
    target = DATA / 'generic-gc-r7b-checkpoint-evidence-20261009/controllers' / filename
    assert not target.exists()
    shutil.copyfile(source, target)
    pub['artifact_sha256'][target.relative_to(ROOT).as_posix()] = sha(target)
publication.write_text(json.dumps(pub, indent=2) + '\n', encoding='utf-8', newline='\n')
for name, expected in pub['artifact_sha256'].items():
    if name.endswith('/normalize-gc-checkpoint-staged-source-20261009.py') or name.endswith('/' + Path(__file__).name):
        blob = subprocess.check_output(['git', 'hash-object', '-w', '--no-filters', name], cwd=ROOT, text=True).strip()
        subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name], cwd=ROOT, check=True)
    assert sha_bytes(subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT)) == expected
name = publication.relative_to(ROOT).as_posix()
blob = subprocess.check_output(['git', 'hash-object', '-w', '--no-filters', name], cwd=ROOT, text=True).strip()
subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name], cwd=ROOT, check=True)
staged = set(subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, text=True).splitlines())
assert all(p in app['owned_paths'] or p.startswith('doc/performance/') for p in staged)
assert set(app['owned_paths']) <= staged and len(app['owned_paths']) == 9
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
release = ROOT / 'build-repro/main-verify-20261006/Release'
assert all(sha(release / p) == h for p, h in state['release_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in app['fixed_baseline_sha256'].items())
subprocess.run(['git', 'diff', '--cached', '--check', '--', 'src', 'tests'], cwd=ROOT, check=True)
subprocess.run(['git', 'commit', '--quiet', '-m', 'Fix generic cycle discovery with bounded graph indexing',
    '-m', 'Collect previously missed ordinary instance/container cycles. Preserve owning-reference multiplicity and conservative cleanup boundaries. Use bounded packed scans, contiguous adjacency and identity-guarded registry indexing with exact pointer fallback. Keep performance comments and CPython pure-Python library bodies.',
    '-m', 'Validation: CPython 3.14.7 oracle; 406 core fixtures, 11 compatibility sections, 9 CTests, 2 API checks; complete default 11-case gate with 21 repeats, 5 warmups and unchanged 10% threshold. Original GC creation CP/X 1.585/2.118 ms; traversal 2.348/1.970 ms in sequential fast runs. Preserve controls and raw evidence; full97 pending.'], cwd=ROOT, check=True)
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
print(json.dumps({'head': head, 'engine_files': 9, 'publication_sha256': sha(publication), 'full97': 'pending'}, indent=2))
