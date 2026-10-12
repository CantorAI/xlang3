"""Preserve validated R4 bytes, then apply the narrow trace-state repair."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CONTROL = ROOT / 'build-repro/controls/python-new-vm-continuation-accepted-r4-20261009'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
HEAD = '09b0500e44820a4e557ca2774459957534702ddd'
CAPTURE = DATA / 'pyperformance-xlang3-python-new-r4-full-fast-r3-20261009-provenance.json'
PROOF = ROOT / 'scratch/performance/nested-trace-setting-engine-provenance-proposed-20261009.json'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def tree(path):
    return {p.relative_to(path).as_posix(): sha(p) for p in sorted(path.rglob('*')) if p.is_file()}

def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))

def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8', newline='\n')

def insert(path, anchor, addition):
    raw = path.read_bytes()
    newline = b'\r\n' if b'\r\n' in raw else b'\n'
    anchor = anchor.encode() + newline
    assert raw.count(anchor) == 1, path
    path.write_bytes(raw.replace(anchor, anchor + addition.encode() + newline))

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == HEAD
    assert sha(CAPTURE) == '797281785559e1ee7d03cb64156edff25714a164e988bbe3362f02d5efc6c8a7'
    assert sha(PROOF) == '10a4d46931a34af9ad10e948707762d25b1e42ca559fcdb538e4c08fe7463903'
    parent = read(CAPTURE); proposal = read(PROOF)
    assert parent['terminal'] and parent['raw'][0]['attempt_capture_valid']
    source_before = parent['source_sha256']
    assert len(source_before) == 132
    assert all(sha(ROOT / p) == h for p, h in source_before.items())
    for relative, expected in proposal['input_sha256'].items():
        assert sha(ROOT / relative) == expected, relative
    patch = ROOT / proposal['patch']; helper = ROOT / proposal['helper']
    assert sha(patch) == proposal['patch_sha256'] and sha(helper) == proposal['helper_sha256']
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=ROOT, check=True)
    binaries = tree(RELEASE); baseline = tree(BASELINE)
    recorded_binaries = {(ROOT / p).relative_to(RELEASE).as_posix(): h
                         for p, h in parent['binaries_sha256'].items()}
    assert len(binaries) == 178 and binaries == recorded_binaries
    assert len(baseline) == 177 and baseline == parent['baseline_sha256']
    assert not CONTROL.exists(), 'Do not overwrite an existing preserved control'
    CONTROL.mkdir()
    shutil.copytree(RELEASE, CONTROL / 'Release')
    for relative in source_before:
        destination = CONTROL / 'sources' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / relative, destination)
    assert tree(CONTROL / 'Release') == binaries
    assert tree(CONTROL / 'sources') == source_before
    save(CONTROL / 'preserved-release-provenance.json', {'status': 'validated_R4_release_preserved',
        'head': HEAD, 'measurement_head': parent['source_base_commit'], 'source_snapshot_sha256': source_before,
        'files_sha256': binaries, 'fixed_baseline_sha256': baseline, 'full_capture': str(CAPTURE),
        'full_capture_sha256': sha(CAPTURE), 'validation': parent['validation'],
        'validation_sha256': parent['validation_sha256'],
        'scope': 'Selected source132 and complete Release178; not every transitive compiled source or a clean-checkout claim.'})
    dirty = subprocess.check_output(['git', 'diff', '--name-only'], cwd=ROOT, text=True).splitlines()
    protected = {p: sha(ROOT / p) for p in dirty}
    new_paths = ['tests/cpp/nested_trace_setting_cases.h', 'tests/fixtures/core/nested_trace_setting.py',
                 'tests/fixtures/expected/nested_trace_setting.out']
    assert all(not (ROOT / p).exists() for p in new_paths)
    subprocess.run(['git', 'apply', str(patch)], cwd=ROOT, check=True)
    shutil.copyfile(helper, ROOT / new_paths[0])
    shutil.copyfile(ROOT / 'scratch/performance/nested-trace-setting-fixture-proposed-20261009.py', ROOT / new_paths[1])
    shutil.copyfile(ROOT / 'scratch/performance/nested-trace-setting-expected-proposed-20261009.out', ROOT / new_paths[2])
    insert(ROOT / 'tests/cpp/interpreter_tests.cpp', '#include "observable_builtin_method_cases.h"',
           '#include "nested_trace_setting_cases.h"')
    insert(ROOT / 'tests/cpp/interpreter_tests.cpp', '  xlang3::test::check_observable_builtin_method_cases(result);',
           '  xlang3::test::check_nested_trace_setting_cases(result);')
    insert(ROOT / 'tests/run_fixtures.py', 'nested_profile_setting', 'nested_trace_setting')
    insert(ROOT / 'tests/run_fixtures.ps1', '    "nested_profile_setting",', '    "nested_trace_setting",')
    owned = ['src/runtime/runtime.cpp', 'src/executor/xlang_vm/xlang_vm_loop.cpp',
             'tests/cpp/interpreter_tests.cpp', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1', *new_paths]
    source_after = {p: sha(ROOT / p) for p in source_before}
    source_after.update({p: sha(ROOT / p) for p in new_paths})
    assert {p for p in source_before if source_before[p] != source_after[p]} == set(owned) - set(new_paths)
    assert all(sha(ROOT / p) == h for p, h in protected.items())
    assert tree(BASELINE) == baseline and tree(RELEASE) == binaries
    receipt = DATA / 'nested-trace-setting-applied-source-20261009.json'
    save(receipt, {'status': 'trace_repair_applied_build_and_validation_pending', 'terminal': True,
        'head': HEAD, 'source_count': len(source_after), 'source_sha256': source_after,
        'owned_paths': owned, 'targets_sha256': {p: sha(ROOT / p) for p in owned},
        'parent_preserved': str(CONTROL / 'preserved-release-provenance.json'),
        'parent_preserved_sha256': sha(CONTROL / 'preserved-release-provenance.json'),
        'proposal': str(PROOF), 'proposal_sha256': sha(PROOF), 'patch_sha256': sha(patch),
        'unowned_tracked_dirty_sha256': protected, 'fixed_baseline_sha256': baseline,
        'parent_release_sha256': binaries, 'CP_reference': str(ROOT / proposal['actual_CP_X_reference']),
        'CP_reference_sha256': proposal['actual_CP_X_reference_sha256'],
        'controller_sha256': sha(__file__), 'engine_commit_permitted': False})
    print(json.dumps({'status': 'applied', 'source_count': len(source_after), 'receipt': str(receipt),
                      'receipt_sha256': sha(receipt), 'preserved_control': str(CONTROL)}, indent=2))

if __name__ == '__main__':
    main()
