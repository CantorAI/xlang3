"""Preserve the fully validated R4 source/Release before any later engine edit."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
VALIDATION = DATA / 'frame-locals-retirement-r4-full-validation-20261008.json'
VALIDATION_SHA = 'b28809dcfc420906e3c3f656fdd9584140d0a80d778673a43342799ea64e75ec'
DEST = ROOT / 'build-repro/controls/frame-locals-retirement-r4-validated-checkpoint-20261008'
INVENTORY = DATA / 'frame-locals-retirement-r4-registered-source-20261008.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda p: {x.relative_to(p).as_posix(): sha(x) for x in sorted(p.rglob('*')) if x.is_file()}


def main():
    assert Path.cwd().resolve() == ROOT.resolve()
    assert sha(VALIDATION) == VALIDATION_SHA and not DEST.exists() and not INVENTORY.exists()
    record = json.loads(VALIDATION.read_bytes())
    assert record['terminal'] and record['status'] == 'validated' and record['full_validated']
    assert all(record[k] for k in ('hashes_unchanged', 'release_tree_unchanged',
                                   'baseline_tree_unchanged', 'preserved_parent_tree_unchanged'))
    sources = record['source_sha256']
    release = tree(RELEASE)
    baseline = tree(BASELINE)
    assert len(sources) == 111 and len(release) == 178 and len(baseline) == 177
    assert record['binaries_sha256'] == {RELEASE.relative_to(ROOT).as_posix() + '/' + p: h for p, h in release.items()}
    assert baseline == record['baseline_sha256']
    assert all(sha(ROOT / p) == h for p, h in sources.items())
    DEST.mkdir()
    for directory, prefix, values in ((RELEASE, '', release), (ROOT, 'source-snapshot/', sources)):
        for relative, expected in values.items():
            target = DEST / (prefix + relative)
            assert target.resolve().is_relative_to(DEST.resolve())
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(directory / relative, target)
            assert sha(target) == expected
    assert tree(DEST) == dict(release, **{'source-snapshot/' + p: h for p, h in sources.items()})
    INVENTORY.write_bytes((json.dumps(dict(status='registered_validated_frame_locals_retirement_r4_source',
        source_count=111, source_sha256=sources, validation=str(VALIDATION),
        validation_sha256=VALIDATION_SHA), indent=2) + '\n').encode())
    manifest = DEST / 'preserved-release-provenance.json'
    manifest.write_bytes((json.dumps(dict(status='preserved_validated_frame_locals_retirement_r4_checkpoint',
        terminal=True, full_validated=True, source_count=111, file_count=178,
        files_sha256=release, source_snapshot_sha256=sources,
        source_inventory_sha256=sha(INVENTORY), full_validation_sha256=VALIDATION_SHA,
        fixed_baseline_sha256=baseline, preserved_utc=datetime.now(timezone.utc).isoformat()), indent=2) + '\n').encode())
    assert tree(RELEASE) == release and tree(BASELINE) == baseline
    assert all(sha(ROOT / p) == h for p, h in sources.items())
    print(json.dumps(dict(source_count=111, release_count=178, baseline_unchanged=True,
                          inventory_sha256=sha(INVENTORY), preserved_manifest_sha256=sha(manifest)), indent=2))


if __name__ == '__main__':
    main()
