"""Preserve the current correctness/gate-passed Release before a compiler capture repair."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009'
VALIDATION = ROOT / 'doc/performance/data/lambda-eager-comprehension-capture-r5-validation-r5-20261009.json'
INPUTS = ROOT / 'scratch/performance/pickle-native-utf8-postfix-native-sampling-inputs-proposed-20261008.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
assert not CONTROL.exists()
assert sha(VALIDATION) == '6a41ee3fe92453d6472f2bb1c24b2da7ef125ac8425d27efdffa9f2262ab2d35'
assert sha(INPUTS) == 'a8be77e20bab1a752a870872e194ecabe3354626398916d6e155510002b282d5'
v, inputs = json.loads(VALIDATION.read_bytes()), json.loads(INPUTS.read_bytes())
assert v['terminal'] and v['correctness_passed'] and v['hashes_unchanged'] and v['full_validated'] and not v['official_failures']
gate_path = ROOT / 'doc/performance/data' / v['fixed_gate']['output']
assert sha(gate_path) == v['fixed_gate']['sha256']
assert json.loads(gate_path.read_bytes())['status'] == 'pass' and v['fixed_gate']['exit_code'] == 0
sources = v['source_sha256']
release, baseline = tree(RELEASE), tree(BASELINE)
assert (len(sources), len(release), len(baseline)) == (128, 178, 177)
assert {RELEASE.relative_to(ROOT).as_posix() + '/' + p: h for p, h in release.items()} == v['binaries_sha256']
assert baseline == v['baseline_sha256']
objects = {p: sha(ROOT / p) for p in inputs['objects']}
translation_units = {r['source']: sha(ROOT / r['source']) for r in inputs['objects'].values()}
metadata = {p: sha(ROOT / p) for p in inputs['build_metadata_sha256']}
assert len(objects) == 149 and len(translation_units) == 149
def git(*args):
    return subprocess.check_output(['git', '-c', 'core.safecrlf=false', *args], cwd=ROOT, stderr=subprocess.DEVNULL)
head = git('rev-parse', 'HEAD').decode().strip()
assert head == 'b188b24e86eac9cbc10c7f0efe6234d98caa0009'
assert not git('diff', '--cached', '--name-only')
dirty = {p: sha(ROOT / p) for p in git('diff', 'HEAD', '--name-only', '-z').decode().split('\0') if p}
for p, h in {**sources, **objects, **translation_units, **metadata, **dirty}.items():
    assert sha(ROOT / p) == h
def capture(source, destination, expected):
    target = (CONTROL / destination).resolve()
    assert target.is_relative_to(CONTROL.resolve()) and not target.exists()
    target.parent.mkdir(parents=True, exist_ok=True)
    assert sha(source) == expected
    shutil.copyfile(source, target)
    assert sha(source) == sha(target) == expected
for p, h in release.items():
    capture(RELEASE / p, 'Release/' + p, h)
for group, mapping in [('source-snapshot', sources), ('native-objects', objects),
                       ('translation-units', translation_units), ('build-metadata', metadata)]:
    for p, h in mapping.items():
        capture(ROOT / p, group + '/' + p, h)
assert tree(RELEASE) == release and tree(BASELINE) == baseline
for p, h in {**sources, **objects, **translation_units, **metadata, **dirty}.items():
    assert sha(ROOT / p) == h
record = dict(status='preserved_fully_validated_lambda_capture_r5', terminal=True,
              full_validated=True, correctness_passed=True, fixed_gate_passed=True,
              remaining_official_sql_failures=[],
              head=head, runtime_path=str(RELEASE / 'xlang3.exe'), file_count=len(release),
              source_count=len(sources), object_count=len(objects), files_sha256=release,
              source_snapshot_sha256=sources, object_snapshot_sha256=objects,
              translation_unit_snapshot_sha256=translation_units, build_metadata_sha256=metadata,
              fixed_baseline_sha256=baseline, tracked_dirty_sha256=dirty,
              controller_sha256=sha(Path(__file__)), validation_sha256=sha(VALIDATION),
              source_identity_limit='Recorded128 plus separately pinned actual native translation units; partial headers, no clean-checkout claim',
              engine_changes=False)
out = CONTROL / 'preserved-release-provenance.json'
out.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Preserved Release178/source128/objects149/fixedbaseline177', sha(out))
