"""Reuse the verified exporter with only evidence filenames changed."""
from pathlib import Path

source = Path('scratch/performance/export-native-trivial-callback-pairs-20261007.py')
code = source.read_text().replace('native-trivial-callback', 'dict-missing-special-lookup')
exec(compile(code, str(source), 'exec'))
