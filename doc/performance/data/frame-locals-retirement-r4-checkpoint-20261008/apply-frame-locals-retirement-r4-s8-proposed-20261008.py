"""Root-only three-file frame-retirement correctness application over S8.

No build, test, benchmark, staging or acceptance. Preserve complete source110
and current Release178 before only two existing writes and one new helper.
Strict application idle guards include MSBuild; no untimed logging exception.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
CP = Path('C:/Python/Python314/python.exe')
PROOF = ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008-provenance.json'
PROOF_SHA = '24b1c58347f3bea0c4bc40d5e450efb04c8ff614f4c0b482cc12511380cf8e76'
SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
READ = lambda path: json.loads(Path(path).read_bytes())
OWNED = ('src/runtime/runtime.cpp', 'tests/cpp/interpreter_tests.cpp')
NEW = ('tests/cpp/frame_locals_retirement_cases.h',)


def safe(base, relative):
    assert base.is_absolute() and base.resolve() == base
    assert isinstance(relative, str) and relative and '\\' not in relative and ':' not in relative
    parts = PurePosixPath(relative)
    assert not parts.is_absolute() and all(part not in ('', '.', '..') for part in parts.parts)
    path = base
    for part in parts.parts:
        path = path / part
        assert not path.is_symlink() and not path.is_junction(), str(path)
    path = path.resolve()
    assert path != base and path.is_relative_to(base)
    return path


def tree(base):
    assert base.is_dir() and not base.is_symlink() and not base.is_junction()
    return {path.relative_to(base).as_posix(): SHA(safe(base, path.relative_to(base).as_posix()))
            for path in base.rglob('*') if path.is_file()}


def unrelated_dirty():
    completed = subprocess.run(['git', '--no-optional-locks', 'status', '--porcelain=v1',
        '-z', '--untracked-files=no'], cwd=ROOT, capture_output=True, check=True)
    values = {}
    for item in completed.stdout.decode('utf-8').split('\0'):
        if not item: continue
        assert len(item) > 3 and item[2] == ' ' and not any(flag in item[:2] for flag in 'RD')
        relative = item[3:]
        if relative not in OWNED: values[relative] = SHA(safe(ROOT, relative))
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert ROOT == Path('D:/CantorAI/xlang3').resolve() and Path.cwd().resolve() == ROOT
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7)
    assert not sys.flags.optimize and Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    preserved = safe(ROOT, 'build-repro/controls/' + args.prefix)
    output = safe(DATA, args.prefix + '-applied-source.json')
    assert not preserved.exists()
    pins = {}
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True); actual = SHA(path)
        assert expected is None or actual == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == actual
        pins[str(path)] = actual
        return path
    pin(PROOF, PROOF_SHA); pin(__file__)
    pin(CP, '4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9')
    pin(CP.with_name('python314.dll'), '0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')
    proof = READ(PROOF)
    base_path = pin(safe(ROOT, proof['parent_inventory']), proof['parent_inventory_sha256'])
    restore_path = pin(safe(ROOT, proof['restoration_receipt']), proof['restoration_receipt_sha256'])
    patch = pin(ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008.patch', proof['patch_sha256'])
    base, restore = READ(base_path), READ(restore_path)
    source = base['source_sha256']
    assert len(source) == 110 and source == proof['raw_before_sha256']
    assert restore['terminal'] and restore['status'] == 'withdrawn_unmeasured_duplicate_r10_preserved_exact_s8_restored'
    assert restore['restored_source_sha256'] == source and restore['s8_manifest_sha256'] == proof['restored_parent_control_manifest_sha256']
    assert restore['accepted_gate_baseline_changed'] is False and restore['unrelated_changes_preserved']
    original = READ(pin(DATA / 'runtime-frame-context-cpython3147-s8-correctness-r2-20261008.json',
        '159f7294d3ff4e1395722145e3bc8ef72a6ed47834989798cd588af0b5af7d41'))
    retained = READ(pin(DATA / 'frame-locals-retained-cpython3147-s8-reference-20261008.json',
        '71a0288e97ffb357de8e447a7a9fca38450fb173201b708988d74341d7150751'))
    for reference, expected_count in ((original, 1), (retained, 2)):
        assert reference['terminal'] and reference['hashes_unchanged']
        cp_rows = [row for row in reference['phases'] if row['runtime'] == 'cpython3147']
        assert len(cp_rows) == expected_count
        assert all(row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['output_matches_expected'] for row in cp_rows)
        for row in reference['phases']:
            for stream in ('stdout', 'stderr'): pin(safe(DATA, row[stream + '_log']), row[stream + '_sha256'])
    assert original['source_sha256'] == proof['strict_reference_source_sha256']
    assert original['expected_sha256'] == proof['strict_reference_expected_sha256'] and original['groups_expected'] == 4
    assert [(item['name'], item['groups_expected']) for item in retained['fixtures']] == [('retained', 1), ('extra', 1)]
    for item, prefix in zip(retained['fixtures'], ('retained_mapping', 'retained_extra')):
        assert item['source_sha256'] == proof[prefix + '_fixture_sha256'] and item['expected_sha256'] == proof[prefix + '_expected_sha256']
    for name in ('strict_reference_source', 'strict_reference_expected', 'retained_mapping_fixture',
                 'retained_mapping_expected', 'retained_extra_fixture', 'retained_extra_expected'):
        pin(safe(ROOT, proof[name]), proof[name + '_sha256'])
    assert tuple(proof['existing_owned_targets']) == OWNED and tuple(proof['new_owned_targets']) == NEW
    assert set(proof['candidate_source_sha256']) == set(OWNED + NEW)
    for path, value in source.items():
        pin(safe(ROOT, path), value)
        pin(safe(safe(ROOT, proof['raw_input_root']), path), value)
    for path in NEW: assert not safe(ROOT, path).exists()
    candidates = safe(ROOT, proof['candidate_root'])
    for path, value in proof['candidate_source_sha256'].items(): pin(safe(candidates, path), value)

    held = READ(pin(CONTROL / 'preserved-release-provenance.json', proof['restored_parent_control_manifest_sha256']))
    assert held['source_snapshot_sha256'] == source and held['source_count'] == 110 and held['file_count'] == 178
    control_map = tree(CONTROL)
    assert control_map == dict(held['files_sha256'], **{'source-snapshot/' + p: h for p, h in source.items()},
        **{'preserved-release-provenance.json': proof['restored_parent_control_manifest_sha256']})
    release, baseline = tree(RELEASE), tree(BASELINE)
    assert len(release) == 178 and release == held['files_sha256'] == restore['restored_release_sha256']
    assert baseline == restore['accepted_gate_baseline_sha256_before'] == restore['accepted_gate_baseline_sha256_after']
    unrelated = unrelated_dirty()
    record = dict(status='preflight', terminal=False, mutation_started=False, full_validated=False,
        scored=False, accepted=False, controller_sha256=SHA(__file__), proposal_sha256=PROOF_SHA,
        restoration_receipt_sha256=SHA(restore_path), parent_inventory_sha256=SHA(base_path),
        owned_targets=list(OWNED + NEW),
        binaries_before=release, baseline_before=baseline, phases=[], unrelated_dirty_sha256_before=unrelated)
    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def idle(label):
        completed = subprocess.run(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'],
            capture_output=True, check=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        rows = json.loads(completed.stdout.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        busy = [row for row in rows if row['ProcessId'] != os.getpid() and
            (row['Name'].lower().startswith(('python', 'xlang3')) or row['Name'].lower() in
             {'cl.exe', 'link.exe', 'ninja.exe', 'cmake.exe', 'ctest.exe', 'msbuild.exe', 'nmake.exe', 'clang-cl.exe', 'lld-link.exe'})]
        record['phases'].append(dict(phase=label, busy=busy)); save(); assert not busy, busy
    def stable():
        return (all(Path(p).is_file() and SHA(p) == h for p, h in pins.items()) and
            tree(CONTROL) == control_map and tree(RELEASE) == release and tree(BASELINE) == baseline and
            unrelated_dirty() == unrelated and not any(safe(ROOT, p).exists() for p in NEW))
    try:
        idle('before-complete-parent-preservation'); assert stable()
        preserved.mkdir()
        for directory, prefix, mapping in ((RELEASE, '', release), (ROOT, 'source-snapshot/', source),
                                           (ROOT, 'unrelated-dirty-snapshot/', unrelated)):
            for path, value in mapping.items():
                target = safe(preserved, prefix + path); target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(safe(directory, path), target); assert SHA(target) == value
        preserved_map = dict(release, **{'source-snapshot/' + p: h for p, h in source.items()},
            **{'unrelated-dirty-snapshot/' + p: h for p, h in unrelated.items()})
        assert tree(preserved) == preserved_map and stable()
        manifest = safe(preserved, 'preserved-release-provenance.json')
        manifest.write_bytes((json.dumps(dict(status='preserved_restored_s8_before_unvalidated_frame_retirement_trial',
            terminal=True, accepted=False, performance_measured=False, files_sha256=release, file_count=178,
            source_snapshot_sha256=source, source_count=110, unrelated_dirty_snapshot_sha256=unrelated,
            source_inventory_sha256=SHA(base_path), actual_restoration_receipt_sha256=SHA(restore_path),
            proposal_sha256=PROOF_SHA, baseline_sha256=baseline), indent=2) + '\n').encode('utf-8'))
        preserved_map['preserved-release-provenance.json'] = SHA(manifest)
        record.update(preserved_parent=str(preserved), preserved_manifest_sha256=SHA(manifest))
        save()
        check = subprocess.run(['git', 'apply', '--check', str(patch)], cwd=ROOT, capture_output=True)
        assert check.returncode == 0, check.stderr.decode('utf-8', 'replace')
        idle('before-three-owned-writes'); assert stable() and tree(preserved) == preserved_map
        record.update(mutation_started=True, status='applying_owned_frame_retirement_sources')
        save()
        for path in OWNED + NEW:
            destination = safe(ROOT, path); assert destination.parent.is_dir()
            destination.write_bytes(safe(candidates, path).read_bytes())
            assert SHA(destination) == proof['candidate_source_sha256'][path]
        after = dict(source, **proof['candidate_source_sha256'])
        assert len(after) == 111
        assert all(SHA(safe(ROOT, p)) == h for p, h in after.items())
        immutable = {p: h for p, h in pins.items() if p not in {str(safe(ROOT, relative)) for relative in OWNED}}
        assert all(Path(p).is_file() and SHA(p) == h for p, h in immutable.items())
        assert tree(CONTROL) == control_map and tree(RELEASE) == release and tree(BASELINE) == baseline
        assert tree(preserved) == preserved_map and unrelated_dirty() == unrelated
        record.update(status='applied_unbuilt_unvalidated_frame_locals_retirement_correctness', source_count=len(after), source_sha256=after,
            binaries_after=release, baseline_after=baseline, accepted_gate_baseline_changed=False,
            unrelated_dirty_sha256_after=unrelated, strict_original_four_retained_one_extra_one_expectations_unchanged=True)
    except BaseException as error:
        record.update(status='failed_after_partial_source_application' if record['mutation_started'] else 'refused_before_source_application',
            error=type(error).__name__ + ': ' + str(error))
    finally:
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat()); save()
    print(record['status'], flush=True)
    return 0 if record['status'] == 'applied_unbuilt_unvalidated_frame_locals_retirement_correctness' else 1


if __name__ == '__main__':
    raise SystemExit(main())
