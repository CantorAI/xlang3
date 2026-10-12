"""Preserve the current validated Release before a possible native UTF8 trial."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

if sys.flags.optimize:
    raise RuntimeError('Preservation guards require unoptimized CPython')
ROOT = Path('D:/CantorAI/xlang3').resolve()
CONTROL = ROOT / 'build-repro/controls/native-str-utf8-encode-parent-20261008'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
DATA = ROOT / 'doc/performance/data'
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
def document(path, expected):
    assert sha(path) == expected, str(path)
    return json.loads(path.read_bytes())
def tree(path):
    return {p.relative_to(path).as_posix(): sha(p) for p in sorted(path.rglob('*')) if p.is_file()}
assert sys.version_info[:3] == (3, 14, 7)
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert Path.cwd().resolve() == ROOT and not CONTROL.exists()
full = document(DATA / 'dict-scalar-append-runtime-index-r3-validation-20261008.json',
    '0b6f05e17c2878efa135656abcce38338934069b59a9460de9f2ba98a8516dd0')
focus = document(DATA / 'dict-scalar-append-runtime-index-r3-focused-20261008.json',
    '0737ff2f405151ed29fdfa22bc0177ea77d18f3b63bdf7aff4662972efb94a38')
inputs = document(ROOT / 'scratch/performance/pickle-dict-index-postfix-native-sampling-inputs-proposed-20261008.json',
    '94da69c6bb258e736f6d599803997399afe1d2e6e233ac8bf358028e7b7922ea')
sample = document(DATA / 'pickle-dict-index-postfix-native-sampling-20261008.json',
    '3bf3fa2bd0d5a393b6ed027b535f4bf0691f4e4b1b7f46d415d0271f0f6aad40')
assert full['terminal'] and full['full_validated'] and full['status'] == 'trial_validated'
assert full['hashes_unchanged'] and full['fixed_gate']['exit_code'] == 0
assert sample['terminal'] and sample['hashes_unchanged']
sources = dict(full['source_sha256'])
assert len(sources) == 115
sources.update({
    'src/runtime/methods/string_methods.cpp': '52860b33aaf0dcdc7fbe4f991725f31e21ba722bfce7dcceccbffa5fcc0ec4ae',
    'src/runtime/modules/system/codecs_module.cpp': '6a0184a50ac0026fc0c54a3bff5da3f26961aaa1e76e2b7cc2a8d6cd1848c7f1',
})
assert len(sources) == 117
release = tree(RELEASE)
assert len(release) == 178 and sample['binaries_sha256'] == focus['binaries_sha256']
assert release == {Path(p).relative_to(RELEASE.relative_to(ROOT)).as_posix(): h for p, h in focus['binaries_sha256'].items()}
baseline = tree(BASELINE)
assert len(baseline) == 177 and baseline == sample['baseline_sha256']
objects = {p: row['object_sha256'] for p, row in inputs['objects'].items()}
assert len(objects) == 149 and objects == sample['object_sha256']
metadata = inputs['build_metadata_sha256']
for path, expected in {**sources, **objects, **metadata}.items():
    assert sha(ROOT / path) == expected, path
dirty = {p: sha(ROOT / p) for p in subprocess.check_output(
    ['git', '-c', 'core.safecrlf=false', 'diff', 'HEAD', '--name-only', '-z'], cwd=ROOT).decode().split('\0') if p}
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
assert head == '4d5b87b5b0cb339fa848e63fc896a54eeb39e60c'
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
def capture(source, relative, expected):
    destination = (CONTROL / relative).resolve()
    assert destination.is_relative_to(CONTROL)
    destination.parent.mkdir(parents=True, exist_ok=True)
    assert not destination.exists() and sha(source) == expected
    shutil.copyfile(source, destination)
    assert sha(source) == sha(destination) == expected
for path, expected in release.items(): capture(RELEASE / path, 'Release/' + path, expected)
for path, expected in sources.items(): capture(ROOT / path, 'source-snapshot/' + path, expected)
for path, expected in objects.items(): capture(ROOT / path, 'native-objects/' + path, expected)
for path, expected in metadata.items(): capture(ROOT / path, 'build-metadata/' + path, expected)
assert tree(RELEASE) == release and tree(BASELINE) == baseline
for path, expected in {**sources, **objects, **metadata, **dirty}.items():
    assert sha(ROOT / path) == expected, path
record = {'status': 'preserved_validated_native_str_utf8_parent', 'terminal': True,
    'full_validated': True, 'head': head, 'runtime_path': str(RELEASE / 'xlang3.exe'),
    'file_count': 178, 'source_count': 117, 'object_count': 149,
    'files_sha256': release, 'source_snapshot_sha256': sources,
    'object_snapshot_sha256': objects, 'build_metadata_sha256': metadata,
    'fixed_baseline_sha256': baseline, 'tracked_dirty_sha256': dirty,
    'validation_sha256': '0b6f05e17c2878efa135656abcce38338934069b59a9460de9f2ba98a8516dd0',
    'additional_source_scope': 'Original recorded115 plus the two existing native UTF8 target cpp files',
    'limit': 'Recorded source117 is still a subset; this does not certify all transitive headers or a clean reproducible build',
    'controller_sha256': sha(Path(__file__)), 'engine_changes': False}
with (CONTROL / 'preserved-release-provenance.json').open('xb') as stream:
    stream.write((json.dumps(record, indent=2) + '\n').encode())
print('Preserved Release178 / recorded source117 / native objects149; fixed baseline177 unchanged')
