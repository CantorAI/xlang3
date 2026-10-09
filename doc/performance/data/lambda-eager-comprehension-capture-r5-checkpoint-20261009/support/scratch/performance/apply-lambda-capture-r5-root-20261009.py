"""Apply the reviewed cold captured-target preparation to the preserved R4 trial."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
PROOF = ROOT / 'scratch/performance/lambda-eager-comprehension-capture-r5-proposed-20261009-provenance.json'
CONTROL = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-failed-r4-20261009'
OUT = DATA / 'lambda-eager-comprehension-capture-r5-applied-source-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert not OUT.exists()
assert sha(PROOF) == 'ca1f333f7d01105d352e5fe583190a5b276f8d414209b0d448b6df9cd832e88a'
manifest = CONTROL / 'preserved-release-provenance.json'
assert sha(manifest) == '0741e4210f7763e65d8871618e77776ef476477b768b539bd22e3af249e2d594'
saved = json.loads(manifest.read_bytes())
p = json.loads(PROOF.read_bytes())
sources, dirty = saved['source_snapshot_sha256'], saved['tracked_dirty_sha256']
assert len(sources) == 128 and sources == p['source_sha256'] and sources == p['raw_before_sha256']
assert all(sha(ROOT / n) == h for n, h in (sources | dirty).items())
assert tree(RELEASE) == tree(CONTROL / 'Release') == saved['files_sha256']
assert tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == saved['head']
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
parent_manifest = ROOT / p['net_parent_manifest']
assert sha(parent_manifest) == p['net_parent_manifest_sha256'] == '865ed0cfaed6e20bc47710808ebbe29cc56ed8d11426ba2e90e008490308b0e1'
parent = json.loads(parent_manifest.read_bytes())
assert parent['correctness_passed'] and parent['fixed_gate_passed']
cp = ROOT / p['actual_cpython_reference']['receipt']
assert sha(cp) == '1ac44bedf1b2777be889c86ebe0b07d416c200a68c75888204b302608e934b54'
assert json.loads(cp.read_bytes())['passed']
assert sha(ROOT / p['current_delta_patch']) == p['current_delta_patch_sha256'] == 'ce58f99dcb7f60125c6f09ccfb7d4975dad55332a027f8e8f669fb3f919b97d2'
assert sha(ROOT / p['patch']) == p['patch_sha256'] == '186de53dbc7e75007ce93fe33dfe5e6998a1ad0729ac0f887f8f866f678572f3'
targets = p['candidate_source_sha256']
mapping = {r['target']: r['source'] for r in p['mapping']}
assert len(targets) == 6 and set(mapping) == set(targets)
assert all(sha(ROOT / n) == h for n, h in p['input_sha256'].items())
assert all(sha(ROOT / mapping[n]) == h for n, h in targets.items())
changed = [n for n in targets if sources[n] != targets[n]]
assert set(changed) == {'src/sema/lower.cpp', 'src/internal/xlang3/pyc_magic.h'}
for n in changed:
    (ROOT / n).write_bytes((ROOT / mapping[n]).read_bytes())
after = sources | targets
assert all(sha(ROOT / n) == h for n, h in (after | dirty).items())
assert tree(RELEASE) == saved['files_sha256'] and tree(ROOT / 'build-repro/Release') == saved['fixed_baseline_sha256']
record = dict(status='applied_reviewed_r5_cold_captured_target_preparation', terminal=True,
              source_count=128, source_sha256=after, targets_sha256=targets, proposal_sha256=sha(PROOF),
              parent_manifest_sha256=sha(parent_manifest), failed_r4_manifest=str(manifest.relative_to(ROOT)),
              failed_r4_manifest_sha256=sha(manifest), controller_sha256=sha(__file__),
              unowned_tracked_dirty_sha256=dirty, cpython_reference_sha256=sha(cp),
              parent_release_preserved=True, fixed_baseline_unchanged=True, build_pending=True,
              performance_validation_pending=True, source_identity_limit=parent['source_identity_limit'])
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print('Applied only R5 compiler/cache delta, source128', sha(OUT))
