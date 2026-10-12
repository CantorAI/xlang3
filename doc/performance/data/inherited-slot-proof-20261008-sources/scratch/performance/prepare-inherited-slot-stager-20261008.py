"""Prepare exact owned staging from the reviewed proposal's target list."""
import ast
from pathlib import Path

root = Path.cwd()
source = (root / 'scratch/performance/stage-canonical-slot-checkpoint-20261008.py').read_text(encoding='utf-8')
start = source.index('sources = [')
end = source.index('\nvalidation =', start)
source = source[:start] + "sources = [entry['path'] for entry in json.loads((root / 'scratch/performance/inherited-slot-proof-proposal-r2-20261008-provenance.json').read_text(encoding='utf-8'))['targets']]" + source[end:]
source = source.replace('canonical-slot-20261008-sources', 'inherited-slot-proof-20261008-sources')
source = source.replace('canonical-slot-r4-validation', 'inherited-slot-proof-validation')
source = source.replace('canonical-slot-owned-pathspec', 'inherited-slot-proof-owned-pathspec')
source = source.replace('canonical-slot-git-source-sha256', 'inherited-slot-proof-git-source-sha256')
source = source.replace("('canonical-slot', 'build-canonical-slot', 'release-canonical-slot', 'pyperformance-canonical-slot')", "('inherited-slot-proof', 'build-inherited-slot-proof', 'release-inherited-slot-proof', 'pyperformance-inherited-slot-proof')")
source = source.replace('canonical-slot-checkpoint-20261008.md', 'inherited-slot-proof-checkpoint-20261008.md')
source = source.replace('canonical-slot-diagnostic-speed', 'inherited-slot-proof-diagnostic-speed')
source = source.replace('canonical-slot-official-speed', 'inherited-slot-proof-official-speed')
ast.parse(source)
target = root / 'scratch/performance/stage-inherited-slot-proof-checkpoint-20261008.py'
assert not target.exists()
target.write_bytes(source.encode('utf-8'))
print('Prepared inherited stager; all eleven actual targets remain explicit')
