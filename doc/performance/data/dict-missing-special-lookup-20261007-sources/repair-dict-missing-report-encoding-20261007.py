"""Repair a derivative report; preserve the original generator bytes."""
import hashlib
import json
from pathlib import Path

root = Path.cwd()
data = root / 'doc/performance/data'
archive = data / 'dict-missing-special-lookup-20261007-sources'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
manifest_path = archive / 'manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
for name in ('finalize-dict-missing-special-evidence-20261007.py', 'export-dict-missing-special-pairs-20261007.py'):
    original = archive / name
    prior = archive / (Path(name).stem + '-before-encoding-repair.py')
    assert not prior.exists()
    prior.write_bytes(original.read_bytes())
    manifest['files'][prior.name] = digest(prior)
    original.write_bytes((root / 'scratch/performance' / name).read_bytes())
    manifest['files'][name] = digest(original)
repair = archive / Path(__file__).name
repair.write_bytes(Path(__file__).read_bytes())
manifest['files'][repair.name] = digest(repair)
manifest['report_encoding_repair'] = 'The exporter source must be decoded as UTF-8. Raw measurements are unchanged; original generators are retained.'
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
report = root / 'doc/performance/dict-missing-special-lookup-checkpoint-20261007.md'
text = report.read_text(encoding='utf-8')
for intended in ('—', '×'):
    corrupted = intended.encode('utf-8').decode('cp1252')
    text = text.replace(corrupted, intended)
report.write_bytes(text.encode('utf-8'))
assert 'Ã' not in text and 'â€' not in text
inventory_path = data / 'dict-missing-special-lookup-evidence-20261007.json'
inventory = json.loads(inventory_path.read_text(encoding='utf-8'))
for name in list(inventory['files']):
    inventory['files'][name] = digest(root / name)
for path in archive.rglob('*'):
    if path.is_file():
        inventory['files'][str(path.relative_to(root)).replace('\\', '/')] = digest(path)
inventory_path.write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Repaired UTF-8 report, retained original generators, refreshed derivative inventory')
