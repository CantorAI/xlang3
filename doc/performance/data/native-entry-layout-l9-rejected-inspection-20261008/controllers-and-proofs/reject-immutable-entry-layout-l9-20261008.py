"""Root-only contingency: preserve rejected L9, then byte-restore owned S8 files.

No benchmark, build, Git mutation, global process control, or automatic decision.
An explicit rejection and a successful terminal failed-usefulness diagnostic are
required. All candidate bytes are preserved before touching a live owned file.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_ROOT = Path('D:/CantorAI/xlang3').resolve()
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROLS = ROOT / 'build-repro/controls'
S8 = CONTROLS / 'sorted-exact-int-s8-validated-checkpoint-20261008'
CP = Path('C:/Python/Python314/python.exe')
INVENTORY = DATA / 'native-entry-layout-l9-applied-source-20261008.json'
INVENTORY_SHA = 'b4fdcbcb4dbcea8eb263411e306a475b5321d50a311f9c2bc7f9c7425d1cc4ec'
BASE = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
BASE_SHA = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
PROOF = ROOT / 'scratch/performance/native-python-entry-immutable-layout-proposed-20261008-provenance.json'
PROOF_SHA = 'bde7e4fa9b891e60eef8886107675df34c0e6905b94259c59a6c2574b7e2e797'
S8_MANIFEST_SHA = '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
FOCUS = DATA / 'native-entry-layout-l9-focused-correctness-r2-20261008.json'
FOCUS_SHA = 'ba43a6c73441242ba02a0949339b0720f052a753853e8b84e7753600d3825c1b'
PAIRED = ROOT / 'scratch/performance/measure-immutable-entry-layout-paired-r2-20261008.py'
PAIRED_SHA = '640725fd35a8f062333332ca6f2dabbac76735f5412dc798d65af1fe245c0b7a'
DECISION = DATA / 'native-entry-layout-l9-callback-paired-strict-idle-20261008.json'
DECISION_SHA = 'f7868c0920a403c5b333df6dd63171570a15a042431194ea72819a870d8ce21c'
OWNED = ('src/internal/xlang3/ir.h', 'src/executor/xlang_vm/xlang_frame.h',
    'src/executor/xlang_vm/xlang_interpreter.cpp', 'src/executor/xlang_vm/xlang_vm_loop.cpp',
    'tests/cpp/interpreter_tests.cpp')
NEW = 'tests/cpp/immutable_entry_layout_cases.h'
SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
READ = lambda path: json.loads(Path(path).read_bytes())


def safe(base, relative):
    """Resolve every copy/remove target inside its explicitly named tree."""
    assert base.is_absolute() and base.resolve() == base
    assert isinstance(relative, str) and relative and '\\' not in relative and ':' not in relative
    parts = PurePosixPath(relative)
    assert not parts.is_absolute() and all(part not in ('', '.', '..') for part in parts.parts)
    path = base
    for part in parts.parts:
        path = path / part
        assert not path.is_symlink() and not path.is_junction(), str(path)
    resolved = path.resolve()
    assert resolved != base and resolved.is_relative_to(base), str(resolved)
    return resolved


def tree(base):
    assert base.is_dir() and not base.is_symlink() and not base.is_junction()
    values = {}
    for path in base.rglob('*'):
        relative = path.relative_to(base).as_posix(); checked = safe(base, relative)
        if checked.is_file(): values[relative] = SHA(checked)
    return values


def unrelated_dirty():
    # Read-only status includes raw EOL-only modifications; never stage/reset.
    completed = subprocess.run(['git', '--no-optional-locks', 'status',
        '--porcelain=v1', '-z', '--untracked-files=no'], cwd=ROOT, capture_output=True, check=True)
    values = {}
    for item in completed.stdout.decode('utf-8').split('\0'):
        if not item: continue
        assert len(item) > 3 and item[2] == ' ' and not any(flag in item[:2] for flag in 'RD'), 'Unexpected rename/deletion'
        relative = item[3:]
        if relative not in OWNED and relative != NEW: values[relative] = SHA(safe(ROOT,relative))
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reject-trial', action='store_true', required=True)
    parser.add_argument('--decision-receipt', type=Path, required=True)
    parser.add_argument('--decision-receipt-sha256', required=True)
    parser.add_argument('--prefix', required=True, help='Fresh output and preserved-control name')
    args = parser.parse_args()
    assert ROOT == EXPECTED_ROOT and sys.implementation.name == 'cpython'
    assert sys.version_info[:3] == (3,14,7) and not sys.flags.optimize and Path(sys.executable).resolve() == CP.resolve()
    assert args.reject_trial and re.fullmatch(r'[a-z0-9-]+',args.prefix)
    assert args.decision_receipt.resolve() == DECISION.resolve() and args.decision_receipt_sha256 == DECISION_SHA
    for base in (ROOT, DATA, RELEASE, CONTROLS, S8):
        assert base.is_dir() and base.resolve() == base and not base.is_symlink() and not base.is_junction()
    preserved = safe(CONTROLS,args.prefix)
    assert not preserved.exists() and not any(DATA.glob(args.prefix+'*')), 'Never overwrite preserved evidence'
    output = safe(DATA,args.prefix+'.json')
    pins = {}
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True); actual = SHA(path)
        assert expected is None or actual == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == actual
        pins[str(path)] = actual; return actual
    for path, expected in ((INVENTORY,INVENTORY_SHA),(BASE,BASE_SHA),(PROOF,PROOF_SHA),
        (S8/'preserved-release-provenance.json',S8_MANIFEST_SHA),(FOCUS,FOCUS_SHA),
        (PAIRED,PAIRED_SHA),(args.decision_receipt,args.decision_receipt_sha256)):
        pin(path,expected)
    pin(__file__)
    inventory, base, proof, focus, s8, decision = map(READ,
        (INVENTORY,BASE,PROOF,FOCUS,S8/'preserved-release-provenance.json',args.decision_receipt))
    assert set(proof['existing_owned_targets']) == set(OWNED) and proof['new_owned_targets'] == [NEW]
    sources = inventory['source_sha256']; originals = base['source_sha256']
    assert len(sources) == 111 and len(originals) == 110
    assert sources == dict(originals,**proof['candidate_source_sha256']) and s8['source_snapshot_sha256'] == originals
    assert set(sources)-set(originals) == {NEW}
    assert {path for path in originals if sources[path] != originals[path]} == set(OWNED)
    assert proof['raw_before_sha256'] == originals
    assert focus['terminal'] and focus['status'] == 'targeted_correctness_passed' and focus['hashes_unchanged']
    assert focus['hashes_before'] == focus['hashes_after'] and len(focus['phases']) == 17
    assert all(row['passed'] and row['exit_code'] == 0 and not row['timeout'] for row in focus['phases'])
    assert focus['source_sha256'] == sources and focus['source_inventory_sha256'] == INVENTORY_SHA
    assert decision['terminal'] and decision['status'] == 'terminal_unscored_layout_pairs' and decision['hashes_unchanged']
    assert decision['hashes_before'] == decision['hashes_after'] and decision['controller_sha256'] == PAIRED_SHA
    assert decision['source_inventory_sha256'] == INVENTORY_SHA and decision['source_sha256'] == sources
    assert decision['focused_receipt_sha256'] == FOCUS_SHA and decision['control_manifest_sha256'] == S8_MANIFEST_SHA
    assert decision['pair_count'] == len(decision['pair_results']) == 7 and decision['values_trimmed'] == decision['outliers_removed'] == 0
    assert decision['mode'] in ('callback-pairs','pprint-pairs')
    assert decision['decision_rule'] == dict(selected_case='sort_callback' if decision['mode']=='callback-pairs' else 'pprint_original_body',
        minimum_median_ratio=1.02,minimum_lower_ci=1.0,bootstrap_resamples=50000,bootstrap_seed=20261008,
        strict_greater_than=True,negative_control_median_floor=1/1.05)
    summary = decision['summary']; ratios = summary['pair_ratios']; ci = summary['bootstrap95_ci']
    assert len(ratios) == 7 and len(ci) == 2 and all(math.isfinite(v) and v > 0 for v in ratios+ci)
    assert summary['median_pair_ratio'] == statistics.median(ratios)
    useful = summary['median_pair_ratio'] > 1.02 and ci[0] > 1.0
    assert summary['useful_signal'] == useful and summary['decision'] == 'no_useful_affected_signal_or_control_regression'
    controls_pass = summary.get('negative_controls_pass',True)
    if decision['mode'] == 'callback-pairs':
        assert len(decision['raw']) == 14 and decision['total_timed_case_invocations'] == 210
        assert controls_pass == all(summary['all_cases'][name]['median_pair_ratio'] >= 1/1.05
            for name in ('loop','small_calls','branch_calls','sort_plain'))
    else: assert len(decision['raw']) == 15 and decision['total_original_body_invocations'] == 15
    assert not (useful and controls_pass), 'Successful usefulness decision cannot authorize this contingency'
    for row in decision['raw']:
        assert row['passed'] and row['measurement_valid'] and row['exit_code'] == 0 and not row['timeout']
        watch = row['external_process_watch']
        assert watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
        pin(safe(DATA,watch['log']),watch['sha256'])
        for stream in ('stdout','stderr'): pin(safe(DATA,row[stream+'_log']),row[stream+'_sha256'])
    for path, value in {**focus['hashes_before'],**decision['hashes_before']}.items(): pin(path,value)
    release = tree(RELEASE)
    assert len(release) == 178 and {RELEASE.relative_to(ROOT).as_posix()+'/'+path:value for path,value in release.items()} == focus['binaries_sha256'] == decision['binaries_sha256']
    assert len(s8['files_sha256']) == s8['file_count'] == 178 and set(release) == set(s8['files_sha256'])
    s8_tree = tree(S8)
    assert s8_tree == dict(s8['files_sha256'],**{'source-snapshot/'+path:value for path,value in originals.items()},
        **{'preserved-release-provenance.json':S8_MANIFEST_SHA})
    for path,value in sources.items(): assert SHA(safe(ROOT,path)) == value
    unrelated = unrelated_dirty()
    mutated_paths = {str(safe(ROOT,path)) for path in (*OWNED,NEW)} | {str(safe(RELEASE,path)) for path in release}
    immutable_pins = {path:value for path,value in pins.items() if path not in mutated_paths}
    record = dict(status='preflight',terminal=False,restoration_started=False,accepted=False,
        controller_sha256=SHA(__file__),decision_receipt=str(args.decision_receipt.resolve()),decision_receipt_sha256=args.decision_receipt_sha256,
        source_inventory_sha256=INVENTORY_SHA,focused_receipt_sha256=FOCUS_SHA,summary=summary,
        unrelated_dirty_sha256_before=unrelated,started_utc=datetime.now(timezone.utc).isoformat())
    def save(): output.write_bytes((json.dumps(record,indent=2)+'\n').encode('utf-8'))
    def stable():
        return (all(Path(path).is_file() and SHA(path)==value for path,value in pins.items()) and
            all(SHA(safe(ROOT,path))==value for path,value in sources.items()) and tree(RELEASE)==release and
            tree(S8)==s8_tree and unrelated_dirty()==unrelated)
    def idle(label):
        scan = subprocess.run(['powershell','-NoProfile','-Command',
            "Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress"],capture_output=True,check=True,timeout=10)
        rows = json.loads(scan.stdout.decode('utf-8-sig') or '[]')
        if isinstance(rows,dict): rows=[rows]
        busy = [row for row in rows if row['ProcessId'] != os.getpid() and
            (row['Name'].lower().startswith(('python','xlang3')) or row['Name'].lower() in
            {'cl.exe','link.exe','ninja.exe','cmake.exe','ctest.exe','msbuild.exe','nmake.exe','clang-cl.exe','lld-link.exe'})]
        record.setdefault('idle_guards',[]).append(dict(phase=label,busy=busy)); save(); assert not busy,busy
    try:
        idle('before-preservation'); assert stable()
        preserved.mkdir()
        record['status']='preserving_rejected_trial'; save()
        for source_root,target_prefix,mapping in ((RELEASE,'',release),(ROOT,'source-snapshot/',sources),(ROOT,'unrelated-dirty-snapshot/',unrelated)):
            for path,expected in mapping.items():
                target=safe(preserved,target_prefix+path); target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(safe(source_root,path),target); assert SHA(target)==expected
        preserved_expected=dict(release,**{'source-snapshot/'+path:value for path,value in sources.items()},
            **{'unrelated-dirty-snapshot/'+path:value for path,value in unrelated.items()})
        assert tree(preserved)==preserved_expected and stable()
        manifest=safe(preserved,'preserved-release-provenance.json')
        manifest.write_bytes((json.dumps(dict(status='preserved_rejected_immutable_layout_no_useful_diagnostic',terminal=True,
            accepted=False,full_validated=False,correctness_status='targeted_17_phases_passed',
            files_sha256=release,file_count=178,source_snapshot_sha256=sources,source_count=111,
            unrelated_dirty_snapshot_sha256=unrelated,source_inventory_sha256=INVENTORY_SHA,
            focused_receipt_sha256=FOCUS_SHA,decision_receipt_sha256=args.decision_receipt_sha256,summary=summary,
            created_utc=datetime.now(timezone.utc).isoformat()),indent=2)+'\n').encode('utf-8'))
        record.update(rejected_control=str(preserved),rejected_manifest_sha256=SHA(manifest),status='preservation_complete')
        save(); idle('before-owned-restoration'); assert stable()
        record.update(restoration_started=True,status='restoring_owned_source_and_fixed_release'); save()
        timestamps={}
        for path in OWNED:
            destination=safe(ROOT,path)
            shutil.copyfile(safe(S8,'source-snapshot/'+path),destination)
            os.utime(destination,None)  # Future Ninja must replace the trial's stale objects.
            assert SHA(destination)==originals[path]; timestamps[path]=destination.stat().st_mtime_ns
        header=safe(ROOT,NEW)
        assert header.parent==safe(ROOT,'tests/cpp') and SHA(header)==sources[NEW]
        header.unlink()  # The only removed file is the exactly pinned new L9 header.
        for path,expected in s8['files_sha256'].items():
            destination=safe(RELEASE,path); shutil.copy2(safe(S8,path),destination); assert SHA(destination)==expected
        assert tree(RELEASE)==s8['files_sha256'] and not safe(ROOT,NEW).exists()
        assert all(SHA(safe(ROOT,path))==value for path,value in originals.items())
        assert all(SHA(safe(ROOT,path))==value for path,value in unrelated.items()) and unrelated_dirty()==unrelated
        assert tree(preserved)==dict(preserved_expected,**{'preserved-release-provenance.json':record['rejected_manifest_sha256']})
        assert tree(S8)==s8_tree and all(Path(path).is_file() and SHA(path)==value for path,value in immutable_pins.items())
        record.update(status='rejected_l9_complete_trial_preserved_exact_s8_restored',restored_source_sha256=originals,
            restored_release_sha256=s8['files_sha256'],restored_owned_sources=list(OWNED),removed_owned_file=NEW,
            restored_source_timestamps_ns=timestamps,future_ninja_rebuild_forced=True,unrelated_changes_preserved=True,
            unrelated_dirty_sha256_after=unrelated,s8_manifest_sha256=S8_MANIFEST_SHA,accepted_gate_baseline_changed=False)
    except BaseException as error:
        record.update(status='terminal_failed_restore_after_mutation' if record['restoration_started'] else 'terminal_refused_before_live_mutation',
            error=type(error).__name__+': '+str(error))
    finally:
        record.update(terminal=True,completed_utc=datetime.now(timezone.utc).isoformat()); save()
    if record['status'] != 'rejected_l9_complete_trial_preserved_exact_s8_restored': return 1
    print('L9 rejected by explicit decision; full trial preserved; exact S8 source and fixed Release restored.',flush=True)
    return 0


if __name__=='__main__': raise SystemExit(main())
