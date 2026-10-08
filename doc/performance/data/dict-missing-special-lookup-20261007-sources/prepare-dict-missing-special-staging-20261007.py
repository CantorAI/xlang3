"""Prepare exact owned-file staging; do not stage until validation is terminal."""
from pathlib import Path

scratch = Path.cwd() / 'scratch/performance'
source = scratch / 'stage-native-trivial-callback-checkpoint-20261007.py'
code = source.read_text().replace('native-trivial-callback', 'dict-missing-special-lookup')
target = scratch / 'stage-dict-missing-special-lookup-checkpoint-20261007.py'
assert not target.exists()
compile(code, str(target), 'exec')
target.write_text(code, encoding='utf-8')
print('Prepared exact inventory/source staging with raw-byte and LF-source verification')
