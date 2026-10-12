"""Apply only two frozen parser targets to the already-preserved parent."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
DATA=ROOT/'doc/performance/data'
CONTROL=ROOT/'build-repro/controls/triple-string-closing-comment-parent-20261008'
PROOF=ROOT/'scratch/performance/triple-string-closing-comment-proposed-20261008-provenance.json'
OUT=DATA/'triple-string-closing-comment-applied-source-20261008.json'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sys.version_info[:3]==(3,14,7) and Path(sys.executable).resolve()==Path('C:/Python/Python314/python.exe').resolve()
assert sha(PROOF)=='279a10cfb06301350b307874f4e358cf59fa1b08817481ffaff9f8ba145b4260'
proof=json.loads(PROOF.read_bytes())
parent_path=CONTROL/'preserved-release-provenance.json'
assert sha(parent_path)=='c25be037d188eb0b83a3885acfa16085e49bd1192fdbbf15010182feeb78f6b3'
parent=json.loads(parent_path.read_bytes())
assert parent['terminal'] and parent['full_validated'] and parent['source_count']==124
targets={'src/parser/lexer.cpp':'fc144ed8797dc0e3f1b75cff737cdc7b58015a9c51bb3274ebd7870d842cc990',
    'tests/cpp/parser_tests.cpp':'2eba5207ba2213f77b9ed57df568588d1ac1a2c5ee459e6daa0d6e1d5247bd33'}
assert proof['targets']==list(targets) and not OUT.exists()
assert sha(ROOT/proof['patch'])=='10029d039e3671cd8ca412406bf1290d4a861bc50e7f2e2e09f004995c988707'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()==parent['head']
assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=ROOT)
for p,h in parent['source_snapshot_sha256'].items(): assert sha(ROOT/p)==h,p
for p,h in parent['files_sha256'].items(): assert sha(CONTROL/'Release'/p)==h,p
for p,h in parent['fixed_baseline_sha256'].items(): assert sha(ROOT/'build-repro/Release'/p)==h,p
for p,h in proof['raw_before_sha256'].items(): assert sha(ROOT/p)==h,p
for p,h in parent['tracked_dirty_sha256'].items(): assert sha(ROOT/p)==h,p
for p,h in targets.items():
    candidate=ROOT/proof['candidate_root']/p
    assert sha(candidate)==h and sha(ROOT/p)==parent['source_snapshot_sha256'][p]
for p,h in targets.items(): (ROOT/p).write_bytes((ROOT/proof['candidate_root']/p).read_bytes())
for p,h in parent['tracked_dirty_sha256'].items(): assert sha(ROOT/p)==h,p
sources=dict(parent['source_snapshot_sha256']);sources.update(targets)
for p,h in sources.items(): assert sha(ROOT/p)==h,p
record=dict(status='applied_frozen_two_file_parser_trial',terminal=True,source_count=len(sources),source_sha256=sources,
    targets_sha256=targets,proposal_sha256=sha(PROOF),parent_manifest_sha256=sha(parent_path),
    controller_sha256=sha(Path(__file__)),unowned_tracked_dirty_sha256=parent['tracked_dirty_sha256'],
    parent_release_preserved=True,fixed_baseline_unchanged=True,build_pending=True,performance_validation_pending=True,
    source_identity_limit=parent['source_identity_limit'])
OUT.write_bytes((json.dumps(record,indent=2)+'\n').encode())
print('Applied exactly two parser files; source124 and pre-existing dirty files guarded')
