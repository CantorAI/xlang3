"""Retain rejected R3 inputs before improving its negative eligibility path."""
import hashlib
import json
from pathlib import Path
import sys
assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
record_path = root / 'doc/performance/data/canonical-slot-early-r2-20261008.json'
record = json.loads(record_path.read_text(encoding='utf-8'))
assert record['status'] == 'terminal'
target = root / 'scratch/performance/canonical-slot-r3-tested-sources-20261008'
assert not target.exists()
target.mkdir()
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
for name, sha in record['source_sha256'].items():
    source, copy = root / name, target / name
    assert digest(source) == sha, name
    copy.parent.mkdir(parents=True, exist_ok=True)
    copy.write_bytes(source.read_bytes())
    assert digest(copy) == sha
manifest = {'status': 'R3 focused correctness passed; paired inherited regression, not accepted',
            'early_evidence': record_path.name, 'source_sha256': record['source_sha256'],
            'candidate_binary_sha256': record['candidate_binary_sha256']}
(target / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print('Preserved', len(record['source_sha256']), 'rejected-trial inputs')
