"""Export unchanged diagnostic rows using the verified shared schema."""
from pathlib import Path

source = Path.cwd() / 'scratch/performance/export-immutable-key-hash-pairs-20261007.py'
code = source.read_text(encoding='utf-8').replace('immutable-key-hash', 'captured-lookup')
exec(compile(code, str(source), 'exec'))
