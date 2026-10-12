"""Preserve the finite original IR observation without altering its receipt."""
import hashlib
import json
from pathlib import Path
import shutil

root = Path('D:/CantorAI/xlang3')
data = root / 'doc/performance/data'
scratch = root / 'scratch/performance'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
capture = scratch / 'raytrace-original-worker-ir-20261009'
assert sha(capture / 'receipt.json') == 'd77f921ef071dce2e6c52b9fd7a66d13fd2df873c9cacd0bc91eb21444241a6e'
r = json.loads((capture / 'receipt.json').read_bytes())
assert r['terminal'] and r['status'] == 'observation_passed'
assert r['inputs_unchanged'] and r['release_unchanged'] and r['cleanup_completed']
assert not r['timing_scoring_permitted'] and not r['adaptive_cache_eligibility_observed']
paths = {capture / name: data / ('raytrace-original-worker-ir-20261009-' + name)
    for name in ('receipt.json', 'stdout.log', 'stderr.log', 'worker.pyperf.json',
                 'run_benchmark.ir.txt', 'Vector.dot.ir.txt')}
for name in ('observe-raytrace-original-worker-ir-proposed-20261009.py',
             'raytrace-original-worker-ir-proposed-20261009-provenance.json',
             'raytrace-original-worker-ir-proposed-20261009-design.md',
             Path(__file__).name):
    paths[scratch / name] = data / ('raytrace-original-worker-ir-reproduction-' + name)
manifest = data / 'raytrace-original-worker-ir-publication-20261010.json'
assert not manifest.exists()
for source, destination in paths.items():
    assert not destination.exists()
    shutil.copyfile(source, destination)
    assert sha(source) == sha(destination)
manifest.write_text(json.dumps(dict(status='unscored_original_ir_preserved',
    origin_receipt_sha256=sha(capture / 'receipt.json'),
    files={p.relative_to(root).as_posix(): sha(p) for p in paths.values()},
    limitations='Compile IR before execution; no adaptive-cache, CPU-share or speedup claim.'), indent=2) + '\n',
    encoding='utf-8', newline='\n')
print('preserved', len(paths), 'manifest_sha256', sha(manifest), flush=True)
