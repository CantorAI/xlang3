"""Apply exactly the five frozen property repair targets to their saved parent."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CONTROL = ROOT / 'build-repro/controls/property-callable-getter-parent-20261009'
PROOF = ROOT / 'scratch/performance/property-callable-getter-proposed-20261009-provenance.json'
OUT = DATA / 'property-callable-getter-applied-source-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.flags.isolated and not sys.flags.optimize and not OUT.exists()
assert sha(PROOF) == 'aed4a764163c3d5d22e876a7c0a0bfc9883c105ed8cc2042649df5cbfd85ef9a'
proof = json.loads(PROOF.read_bytes())
parent_path = CONTROL / 'preserved-release-provenance.json'
assert sha(parent_path) == '608aadc303532321de8ba8cf464ce7d341ba6366373d386ab2230e865c9f6573'
parent = json.loads(parent_path.read_bytes())
assert parent['terminal'] and parent['correctness_passed'] and parent['fixed_gate_passed'] and not parent['full_validated']
assert parent['source_count'] == 124 and parent['head'] == '3fec9047ddd7c12e3dbcdd511167e972ecc314a5'
reference_path = DATA / 'property-callable-getter-cpython-reference-20261009.json'
assert sha(reference_path) == '586520b943a130092e9eff8ba88d5fdce1e82b79481227845a729c4ade711532'
reference = json.loads(reference_path.read_bytes())
assert reference['passed'] and reference['terminal'] and reference['hashes_unchanged']
assert sha(ROOT / proof['patch']) == 'a83cc74d95b132a4192d718463919a85bae51a066f875d3ce43a4f38b7cdd15d'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == parent['head']
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
targets = proof['candidate_source_sha256']
assert len(targets) == 5 and set(targets) == set(proof['mapping'])
assert proof['source_sha256'] == parent['source_snapshot_sha256']
for p, h in parent['source_snapshot_sha256'].items():
    assert sha(ROOT / p) == h
for p, h in parent['files_sha256'].items():
    assert sha(CONTROL / 'Release' / p) == sha(ROOT / 'build-repro/main-verify-20261006/Release' / p) == h
for p, h in parent['fixed_baseline_sha256'].items():
    assert sha(ROOT / 'build-repro/Release' / p) == h
for p, h in parent['tracked_dirty_sha256'].items():
    assert sha(ROOT / p) == h
for p, h in proof['input_sha256'].items():
    assert sha(ROOT / p) == h
for p in proof['new_targets_absent']:
    assert not (ROOT / p).exists()
for p, h in targets.items():
    assert sha(ROOT / proof['mapping'][p]) == h
for p in targets:
    target = ROOT / p
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes((ROOT / proof['mapping'][p]).read_bytes())
for p, h in parent['tracked_dirty_sha256'].items():
    assert sha(ROOT / p) == h
sources = dict(parent['source_snapshot_sha256'])
sources.update(targets)
assert len(sources) == 126
assert all(sha(ROOT / p) == h for p, h in sources.items())
record = dict(status='applied_frozen_five_target_property_trial', terminal=True, source_count=len(sources),
              source_sha256=sources, targets_sha256=targets, proposal_sha256=sha(PROOF),
              parent_manifest_sha256=sha(parent_path), controller_sha256=sha(Path(__file__)),
              unowned_tracked_dirty_sha256=parent['tracked_dirty_sha256'], cpython_reference_sha256=sha(reference_path),
              parent_release_preserved=True, fixed_baseline_unchanged=True, build_pending=True,
              performance_validation_pending=True, source_identity_limit=parent['source_identity_limit'])
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Applied exactly five property targets; source126/dirty bytes guarded', sha(OUT))
