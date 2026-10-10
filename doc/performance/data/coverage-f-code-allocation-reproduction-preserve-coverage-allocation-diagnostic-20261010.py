"""Retain failed and successful diagnostic implementations without rewriting raw proofs."""
import hashlib
import json
from pathlib import Path
import shutil

root = Path('D:/CantorAI/xlang3')
data = root / 'doc/performance/data'
scratch = root / 'scratch/performance'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
receipt = data / 'coverage-f-code-allocation-r3-20261010.json'
assert sha(receipt) == 'fa009691724f782fe619d519b91c62eeda209a03563648496b55c7d0693f010d'
r = json.loads(receipt.read_bytes())
assert r['terminal'] and r['status'] == 'completed_untimed_counter_diagnostic'
assert r['output_tracing_parity'] and not r['scored']
x = r['phases'][1]['result']
assert x['counts'] == {'Code': {'allocations': 1213976, 'final_releases': 1213976},
                       'Frame': {'allocations': 242847, 'final_releases': 242847}}
for phase in r['phases']:
    assert phase['exit_code'] == 0 and phase['cleanup_passed']
    assert phase['pre_identity']['passed'] and phase['post_identity']['passed']
    for p, h in phase['raw_sha256'].items():
        assert sha(p) == h
names = ['coverage-original-body-allocation-child-proposed-20261010.py',
    'run-coverage-original-body-allocation-proposed-20261010.py',
    'coverage-original-body-allocation-provenance-proposed-20261010.json',
    'coverage-original-body-allocation-child-r2-proposed-20261010.py',
    'run-coverage-original-body-allocation-r2-proposed-20261010.py',
    'coverage-original-body-allocation-provenance-r2-proposed-20261010.json',
    'coverage-original-body-allocation-child-r3-proposed-20261010.py',
    'run-coverage-original-body-allocation-r3-proposed-20261010.py',
    'coverage-original-body-allocation-provenance-r3-proposed-20261010.json',
    'prepare-coverage-allocation-r2-20261010.py', Path(__file__).name]
for name in names:
    dst = data / ('coverage-f-code-allocation-reproduction-' + name)
    assert not dst.exists()
    shutil.copyfile(scratch / name, dst)
    assert sha(dst) == sha(scratch / name)
manifest = data / 'coverage-f-code-allocation-publication-20261010.json'
assert not manifest.exists()
paths = sorted(p for p in data.glob('coverage-f-code-allocation*') if p.is_file())
manifest.write_text(json.dumps(dict(status='untimed_counts_and_failures_preserved',
    successful_origin_sha256=sha(receipt),
    files={p.relative_to(root).as_posix(): sha(p) for p in paths},
    limits='No timing, CPU share, CP allocation ratio or optimization gain claim.'), indent=2) + '\n',
    encoding='utf-8', newline='\n')
print('preserved', len(paths), 'manifest_sha256', sha(manifest), flush=True)
