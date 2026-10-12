"""Record variation from completed raw samples and refresh evidence hashes."""
import hashlib
import json
from pathlib import Path
import statistics
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
comparison_path = data / 'inherited-subscript-cache-bpe-vs-cpython3147-20261007.json'
comparison = json.loads(comparison_path.read_text())
assert comparison['status'] == 'completed'
for runtime in ('candidate', 'reference'):
    values = comparison[runtime]['values_seconds']
    comparison[runtime]['sample_standard_deviation_seconds'] = statistics.stdev(values)
    comparison[runtime]['coefficient_of_variation'] = statistics.stdev(values) / statistics.mean(values)
comparison['candidate']['pyperf_warning'] = 'Result may be unstable; not enough samples for the stated precision criterion'
comparison_path.write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
archive = data / 'inherited-subscript-cache-20261007-sources'
source = Path(__file__)
target = archive / source.name
assert not target.exists()
target.write_bytes(source.read_bytes())
manifest_path = archive / 'manifest.json'
manifest = json.loads(manifest_path.read_text())
manifest['files'][target.name] = digest(target)
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
inventory_path = data / 'inherited-subscript-cache-evidence-20261007.json'
inventory = json.loads(inventory_path.read_text())
for path in (comparison_path, root / 'doc/performance/inherited-subscript-cache-checkpoint-20261007.md', target, manifest_path):
    inventory['files'][str(path.relative_to(root)).replace('\\', '/')] = digest(path)
for name, sha in inventory['files'].items():
    assert digest(root / name) == sha, name
inventory_path.write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Recorded BPE variation; candidate sample SD:', comparison['candidate']['sample_standard_deviation_seconds'])
