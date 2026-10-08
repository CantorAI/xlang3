"""Root-only byte-verified application of the independently reviewed L9 trial."""
import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
SHA = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
READ = lambda p: json.loads(Path(p).read_bytes())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--checkpoint-head', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == args.checkpoint_head
    inventory = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
    validation = DATA / 'sorted-exact-int-s8-full-validation-20261008.json'
    pyflate = DATA / 'sorted-exact-int-s8-original-official-pyflate-strict-idle-20261008.json'
    proposal = ROOT / 'scratch/performance/native-python-entry-immutable-layout-proposed-20261008-provenance.json'
    patch = ROOT / 'scratch/performance/native-python-entry-immutable-layout-proposed-20261008.patch'
    control = ROOT / 'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
    manifest = control / 'preserved-release-provenance.json'
    assert SHA(inventory) == 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
    assert SHA(validation) == 'd4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022'
    assert SHA(pyflate) == '090ca8208ab3f65f2f29d2596b71fa52d6cdf26c6da23c4dc16e1a052254fd9d'
    assert SHA(proposal) == 'bde7e4fa9b891e60eef8886107675df34c0e6905b94259c59a6c2574b7e2e797'
    assert SHA(patch) == 'f5a1cf0588369abe0ed15e007dbf34fb101eafabca6d50ac08b4df4a83950e83'
    assert SHA(manifest) == '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
    inv, valid, affected, proof, held = map(READ, (inventory, validation, pyflate, proposal, manifest))
    assert valid['terminal'] and valid['hashes_unchanged'] and valid['full_validated'] and valid['fixed_gate']['exit_code'] == 0
    assert affected['terminal'] and affected['hashes_unchanged'] and affected['official_pyflate']['complete']
    assert affected['official_pyflate']['values_count'] == 20 and all(r['measurement_valid'] for r in affected['raw'])
    before = inv['source_sha256']
    assert len(before) == 110 and all(SHA(ROOT / p) == h for p, h in before.items())
    assert held['source_snapshot_sha256'] == before
    assert all(SHA(control / 'source-snapshot' / p) == h for p, h in before.items())
    assert all(SHA(control / p) == h for p, h in held['files_sha256'].items())
    release = ROOT / 'build-repro/main-verify-20261006/Release'
    current = {p.relative_to(ROOT).as_posix(): SHA(p) for p in release.rglob('*') if p.is_file()}
    assert current == valid['binaries_sha256'] and len(current) == 178
    existing, new = proof['existing_owned_targets'], proof['new_owned_targets']
    assert len(existing) == 5 and len(new) == 1
    assert all(SHA(ROOT / p) == proof['raw_before_sha256'][p] for p in existing)
    assert all(not (ROOT / p).exists() for p in new)
    candidate = Path(proof['candidate_root'])
    assert all(SHA(candidate / p) == h for p, h in proof['candidate_source_sha256'].items())
    check = subprocess.run(['git', 'apply', '--check', str(patch)], cwd=ROOT, capture_output=True, text=True)
    assert check.returncode == 0, check.stderr
    output = DATA / 'native-entry-layout-l9-applied-source-20261008.json'
    assert not output.exists()
    for p in existing + new:
        (ROOT / p).write_bytes((candidate / p).read_bytes())
    after = dict(before)
    after.update(proof['candidate_source_sha256'])
    assert len(after) == 111 and all(SHA(ROOT / p) == h for p, h in after.items())
    record = dict(status='applied_unbuilt_unvalidated_trial', source_count=len(after), source_sha256=after,
        checkpoint_head=args.checkpoint_head, source_inventory_sha256=SHA(inventory),
        control_manifest_sha256=SHA(manifest), parent_validation_sha256=SHA(validation),
        parent_official_pyflate_sha256=SHA(pyflate), proposal_sha256=SHA(proposal), patch_sha256=SHA(patch),
        controller_sha256=SHA(Path(__file__)), owned_targets=existing + new,
        binaries_before=current, accepted_gate_baseline_changed=False,
        created_utc=datetime.now(timezone.utc).isoformat())
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('applied_111_sources_unbuilt', SHA(output))


if __name__ == '__main__':
    main()
