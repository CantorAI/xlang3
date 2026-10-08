"""Keep official-run variation and a neutral result explicit in the report."""
import hashlib
import json
from pathlib import Path
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
comparison_path = data / 'minmax-streaming-bpe-vs-cpython3147-20261007.json'
comparison = json.loads(comparison_path.read_text())
assert comparison['status'] == 'completed'
current_log = (data / 'minmax-streaming-validation-20261007-official-bpe.log').read_text()
previous_log = (data / 'inherited-subscript-cache-validation-20261007-official-bpe.log').read_text()
comparison['candidate_pyperf_instability_warning'] = 'WARNING:' in current_log
comparison['previous_pyperf_instability_warning'] = 'WARNING:' in previous_log
assert not comparison['candidate_pyperf_instability_warning']
assert comparison['previous_pyperf_instability_warning']
comparison['interpretation'] = 'Approximately 1% nominal official speedup over preceding XLang3, with much greater preceding-run variation; no material or statistically significant speed gain established. Counter native callback diagnostic is roughly 3% slower.'
comparison_path.write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
report = root / 'doc/performance/minmax-streaming-checkpoint-20261007.md'
text = report.read_text()
old = 'Preserve the pyperf warning in the original log; this is not a statistical significance claim.'
assert old in text
text = text.replace(old, 'The candidate log has no instability warning. The preceding run did warn and had a much larger sample SD (about 3.350 s). The approximately 1% nominal difference establishes no material or statistically significant speed gain; this is a correctness and memory-footprint checkpoint.')
report.write_bytes(text.encode('utf-8'))
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
archive = data / 'minmax-streaming-20261007-sources'
target = archive / Path(__file__).name
assert not target.exists()
target.write_bytes(Path(__file__).read_bytes())
manifest_path = archive / 'manifest.json'
manifest = json.loads(manifest_path.read_text())
manifest['files'][target.name] = digest(target)
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
inventory_path = data / 'minmax-streaming-evidence-20261007.json'
inventory = json.loads(inventory_path.read_text())
for path in (comparison_path, report, target, manifest_path):
    inventory['files'][str(path.relative_to(root)).replace('\\', '/')] = digest(path)
for name, sha in inventory['files'].items():
    assert digest(root / name) == sha, name
inventory_path.write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Recorded neutral official interpretation and verified all evidence hashes')
