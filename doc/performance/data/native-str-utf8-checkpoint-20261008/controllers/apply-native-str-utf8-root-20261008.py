"""Apply only the frozen seven-file UTF-8 trial; retain all unowned changes."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT/'doc/performance/data'
PROOF = ROOT/'scratch/performance/native-str-utf8-encode-proposed-20261008-provenance.json'
PROOF_SHA = '4fd1a6d4d2b9c6c85f7dcdc0f87a6d633e9fbefb3bc61899c0dda9f48732b570'
PARENT = ROOT/'build-repro/controls/native-str-utf8-encode-parent-20261008'
PARENT_SHA = 'cd5035f531a513104f2948286e81d4996bb01a9b6dc4c84353303f370de4bd97'
CP_RECEIPT = DATA/'native-str-utf8-cpython-semantics-20261008.json'
CP_RECEIPT_SHA = 'f3c91eb53ca3f903838367c2bd111b72ba778ae36d507d88b4f4fa95d0f313e4'
RELEASE = ROOT/'build-repro/main-verify-20261006/Release'
BASELINE = ROOT/'build-repro/Release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
tree = lambda root: {p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file()}

def git(*args):
    return subprocess.check_output(['git','-c','core.safecrlf=false',*args],cwd=ROOT)

def main():
    assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3,14,7) and not sys.flags.optimize
    assert sha(PROOF) == PROOF_SHA and sha(CP_RECEIPT) == CP_RECEIPT_SHA
    assert read(CP_RECEIPT)['passed'] and read(CP_RECEIPT)['hashes_unchanged']
    proof = read(PROOF)
    assert proof['status'] == 'frozen_scratch_proposal_unapplied_unexecuted_unmeasured'
    assert proof['apply_check_exit'] == 0 and not proof['actual_engine_changes']
    assert len(proof['candidate_file_list']) == 7 and proof['resulting_recorded_source_count'] == 119
    assert sha(PARENT/'preserved-release-provenance.json') == PARENT_SHA
    parent = read(PARENT/'preserved-release-provenance.json')
    assert parent['terminal'] and parent['full_validated'] and parent['source_count'] == 117
    assert git('rev-parse','HEAD').decode().strip() == parent['head']
    assert not git('diff','--cached','--name-only')
    assert tree(RELEASE) == parent['files_sha256']
    assert tree(PARENT/'Release') == parent['files_sha256']
    assert tree(PARENT/'source-snapshot') == parent['source_snapshot_sha256']
    assert tree(BASELINE) == parent['fixed_baseline_sha256']
    assert proof['raw_before_sha256'] == parent['source_snapshot_sha256']
    assert all(sha(ROOT/p) == value for p,value in proof['raw_before_sha256'].items())
    assert all(sha(ROOT/p) == value for p,value in proof['raw_target_source_sha256'].items())
    assert all(not (ROOT/p).exists() for p in proof['new_owned_targets'])
    assert sha(ROOT/proof['patch']) == proof['patch_sha256']
    assert sha(ROOT/proof['trial_decision']) == proof['trial_decision_sha256']
    payloads = {p:(Path(proof['candidate_root'])/p).read_bytes() for p in proof['candidate_file_list']}
    assert all(hashlib.sha256(data).hexdigest() == proof['candidate_source_sha256'][p] for p,data in payloads.items())
    header = 'src/internal/xlang3/builtins.h'
    old_header = (ROOT/header).read_bytes()
    anchor = proof['owned_declaration_anchor'].encode()
    insertion = b'\n'+proof['owned_declaration_after_anchor'].encode()
    assert old_header.count(anchor) == 1 and payloads[header] == old_header.replace(anchor,anchor+insertion,1)
    dirty = {p:sha(ROOT/p) for p in git('diff','--name-only').decode().splitlines()}
    record = dict(status='applying_frozen_native_str_utf8_trial',terminal=False,scored=False,
                  proposal_sha256=PROOF_SHA,parent_manifest_sha256=PARENT_SHA,
                  cpython_semantics_receipt_sha256=CP_RECEIPT_SHA,git_head=parent['head'],
                  parent_release_sha256=parent['files_sha256'],fixed_baseline_sha256=parent['fixed_baseline_sha256'],
                  owned_targets=proof['candidate_file_list'],tracked_dirty_sha256_before=dirty,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  source_scope='Recorded parent117 plus new fixture and expected transcript; not all repository/transitive headers')
    output = DATA/'native-str-utf8-applied-source-20261008.json'
    assert not output.exists()
    save = lambda: output.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    save()
    for p,data in payloads.items():
        path = ROOT/p
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data)
    sources = dict(proof['raw_before_sha256'])
    sources.update(proof['candidate_source_sha256'])
    assert len(sources) == 119 and all(sha(ROOT/p) == value for p,value in sources.items())
    assert all(sha(ROOT/p) == value for p,value in dirty.items() if p not in payloads)
    assert tree(RELEASE) == parent['files_sha256'] and tree(BASELINE) == parent['fixed_baseline_sha256']
    record.update(status='frozen_native_str_utf8_applied_build_pending',terminal=True,
                  source_sha256=sources,source_count=len(sources),candidate_source_sha256=proof['candidate_source_sha256'],
                  unowned_tracked_bytes_preserved=True,release_unchanged_before_build=True,
                  fixed_baseline_unchanged=True,completed_utc=datetime.now(timezone.utc).isoformat())
    save()
    print(json.dumps({'status':record['status'],'source_count':len(sources),'receipt_sha256':sha(output)}))

if __name__ == '__main__': main()
