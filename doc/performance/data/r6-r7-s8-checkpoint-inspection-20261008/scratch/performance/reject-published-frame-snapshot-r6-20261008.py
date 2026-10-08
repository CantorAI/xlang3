"""Preserve rejected trial bytes, then restore only its owned source and Release."""
import hashlib
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
C5 = ROOT / 'build-repro/controls/class-constructor-c5-before-snapshot-r6-20261008'
PRESERVED = ROOT / 'build-repro/controls/published-frame-snapshot-r6-rejected-20261008'
OUT = DATA / 'published-frame-snapshot-r6-rejection-20261008.json'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def tree(root):
    return {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert not OUT.exists() and not PRESERVED.exists()
    pairs_path = DATA / 'pickle-published-frame-c6-paired-20261008.json'
    pairs = json.loads(pairs_path.read_bytes())
    assert pairs['terminal'] and pairs['hashes_unchanged']
    assert pairs['status'] == 'terminal_unscored_paired_original_body_diagnostic'
    assert pairs['summary']['decision'] == 'reject_c6_retain_verified_c5'
    assert len(pairs['raw']) == 14 and all(row['passed'] and row['measurement_valid'] for row in pairs['raw'])
    sources = json.loads((DATA / 'published-frame-snapshot-r6-applied-source-20261008.json').read_bytes())['source_sha256']
    assert len(sources) == 107 and all(sha(ROOT / p) == h for p, h in sources.items())
    files = tree(RELEASE)
    assert len(files) == 178
    assert {str(RELEASE / p).replace('\\', '/'): h for p, h in files.items()} == {
        str(ROOT / p).replace('\\', '/'): h for p, h in pairs['release_after'].items()}
    c5_manifest = C5 / 'preserved-release-provenance.json'
    assert sha(c5_manifest) == '85910ee375f16c677153fc128fb17303868a849c124f5909d5ee03eeed764bdc'
    c5 = json.loads(c5_manifest.read_bytes())
    assert tree(C5 / 'source-snapshot') == c5['source_snapshot_sha256']
    assert all(sha(C5 / p) == h for p, h in c5['files_sha256'].items())
    rows = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command',
        'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig'))
    if isinstance(rows, dict): rows = [rows]
    busy = [r for r in rows if r['ProcessId'] != os.getpid() and
        (r['Name'].lower().startswith(('python', 'xlang3')) or
         r['Name'].lower() in {'cl.exe', 'link.exe', 'ninja.exe', 'cmake.exe', 'ctest.exe', 'msbuild.exe'})]
    assert not busy, busy
    for base, target, mapping in [(RELEASE, PRESERVED, files), (ROOT, PRESERVED / 'source-snapshot', sources)]:
        for p, expected in mapping.items():
            dest = target / p
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(base / p, dest)
            assert sha(dest) == expected
    manifest = PRESERVED / 'preserved-release-provenance.json'
    manifest.write_text(json.dumps(dict(status='rejected_no_paired_original_body_gain',
        source_snapshot_sha256=sources, files_sha256=files, focused_correctness_passed=True,
        full_gate_run=False, official_benchmark_run=False, paired_receipt=str(pairs_path),
        paired_receipt_sha256=sha(pairs_path), summary=pairs['summary']), indent=2) + '\n', encoding='utf-8')
    owned = ['src/runtime/runtime.cpp', 'tests/cpp/interpreter_tests.cpp']
    new_header = 'tests/cpp/published_frame_snapshot_cases.h'
    assert set(sources) - set(c5['source_snapshot_sha256']) == {new_header}
    assert {p for p in sources if p in c5['source_snapshot_sha256'] and sources[p] != c5['source_snapshot_sha256'][p]} == set(owned)
    for p in owned:
        shutil.copyfile(C5 / 'source-snapshot' / p, ROOT / p)
        os.utime(ROOT / p, None)  # Future Ninja must rebuild the rejected trial's stale object files.
        assert sha(ROOT / p) == c5['source_snapshot_sha256'][p]
    header = (ROOT / new_header).resolve()
    assert header.parent == (ROOT / 'tests/cpp').resolve() and sha(header) == sources[new_header]
    header.unlink()
    assert set(files) == set(c5['files_sha256'])
    for p in files:
        shutil.copy2(C5 / p, RELEASE / p)
    assert tree(RELEASE) == c5['files_sha256']
    assert all(sha(ROOT / p) == h for p, h in c5['source_snapshot_sha256'].items())
    OUT.write_text(json.dumps(dict(status='trial_rejected_verified_c5_source_and_release_restored',
        terminal=True, completed_utc=datetime.now(timezone.utc).isoformat(), summary=pairs['summary'],
        rejected_control=str(PRESERVED), rejected_manifest_sha256=sha(manifest),
        accepted_control=str(C5), accepted_manifest_sha256=sha(c5_manifest),
        paired_receipt_sha256=sha(pairs_path), restored_source=owned, removed_owned_file=new_header,
        restored_release_count=len(files), future_ninja_rebuild_forced=True,
        accepted_gate_baseline_changed=False, unrelated_changes_preserved=True), indent=2) + '\n', encoding='utf-8')
    print('Rejected R6; complete trial preserved; verified C5 source and fixed-path Release restored.')

if __name__ == '__main__':
    main()
