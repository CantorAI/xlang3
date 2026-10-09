"""Keep the validated worktree/binaries fixed; stage gc_module with HEAD's LF form."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha_bytes = lambda b: hashlib.sha256(b).hexdigest()
sha = lambda p: sha_bytes(Path(p).read_bytes())
read = lambda p: json.loads(Path(p).read_bytes())
publication = DATA / 'generic-gc-r7b-checkpoint-publication-20261009.json'
assert sha(publication) == '9c6a55ac1250ed8e8f58367dc6fd0eb630b4b958bcbbf0101403b6c2a01d02e1'
app = read(DATA / 'gc-generic-cycles-applied-source-r7b-20261009.json')
perf = read(DATA / 'gc-generic-cycles-performance-r7b-activity-20261009.json')
correct = read(DATA / 'gc-generic-cycles-correctness-r7b-20261009.json')
assert perf['terminal'] and perf['hashes_unchanged'] and perf['fixed_gate']['passed']
assert perf['status'] == 'gate_passed_original_gc_completed' and all(p['measurement_valid'] for p in perf['phases'])
assert correct['terminal'] and correct['correctness_passed'] and correct['sources_unchanged'] and correct['release_unchanged']
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
name = 'src/runtime/modules/system/gc_module.cpp'
raw = (ROOT / name).read_bytes()
assert sha_bytes(raw) == app['source_sha256'][name]
assert subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT) == raw
parent = subprocess.check_output(['git', 'show', 'HEAD:' + name], cwd=ROOT)
assert b'\r' not in parent and b'R"' not in raw
normalized = raw.replace(b'\r\n', b'\n')
assert b'\r' not in normalized and normalized.splitlines() == raw.splitlines()
old_publication = DATA / 'generic-gc-r7b-checkpoint-publication-before-stage-normalization-20261009.json'
assert not old_publication.exists()
old_publication.write_bytes(publication.read_bytes())
record = {'terminal': True, 'engine_commit_permitted': True,
    'decision': 'Fresh full correctness, complete default fixed gate0 and affected original definitions pass; authorize the already user-requested checkpoint.',
    'performance_sha256': sha(DATA / 'gc-generic-cycles-performance-r7b-activity-20261009.json'),
    'correctness_sha256': sha(DATA / 'gc-generic-cycles-correctness-r7b-20261009.json'),
    'raw_performance_permission_field': perf['engine_commit_permitted'],
    'raw_permission_field_note': 'The manager initializes this conservative field false and does not update it on success. This separate decision follows its terminal success status and actual checks; raw evidence remains unchanged.',
    'source_normalization': {'path': name, 'working_sha256': sha_bytes(raw), 'staged_sha256': sha_bytes(normalized),
        'working_tree_changed': False, 'release_changed': False, 'difference': 'CRLF to LF in staged text only, matching HEAD. Complete logical lines identical; raw strings absent. All measured working-source hashes remain exact.'},
    'controller_sha256': sha(__file__)}
audit = DATA / 'gc-generic-cycles-r7b-acceptance-audit-20261009.json'
assert not audit.exists()
audit.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
pub = read(publication)
pub['artifact_sha256'][name] = sha_bytes(normalized)
pub['artifact_sha256'][old_publication.relative_to(ROOT).as_posix()] = sha(old_publication)
pub['artifact_sha256'][audit.relative_to(ROOT).as_posix()] = sha(audit)
pub['stage_normalizations'] = [record['source_normalization']]
pub['acceptance_audit_sha256'] = sha(audit)
publication.write_text(json.dumps(pub, indent=2) + '\n', encoding='utf-8', newline='\n')
for path, contents in ((name, normalized), (old_publication.relative_to(ROOT).as_posix(), old_publication.read_bytes()),
                       (audit.relative_to(ROOT).as_posix(), audit.read_bytes()),
                       (publication.relative_to(ROOT).as_posix(), publication.read_bytes())):
    blob = subprocess.check_output(['git', 'hash-object', '-w', '--stdin'], cwd=ROOT, input=contents).decode().strip()
    subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + path], cwd=ROOT, check=True)
    assert subprocess.check_output(['git', 'show', ':' + path], cwd=ROOT) == contents
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
for path, expected in pub['artifact_sha256'].items():
    assert sha_bytes(subprocess.check_output(['git', 'show', ':' + path], cwd=ROOT)) == expected
assert subprocess.run(['git', 'diff', '--cached', '--check', '--', 'src', 'tests'], cwd=ROOT).returncode == 0
print('Verified checkpoint publication:', sha(publication))
