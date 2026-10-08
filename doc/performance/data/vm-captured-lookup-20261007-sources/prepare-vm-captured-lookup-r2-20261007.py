"""Keep the failed log/source; add preserved-control check to repaired fixture."""
from pathlib import Path
import sys
assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
source = root / 'scratch/performance/check-vm-captured-lookup-20261007.py'
target = root / 'scratch/performance/check-vm-captured-lookup-r2-20261007.py'
assert not target.exists()
text = source.read_text(encoding='utf-8')
text = text.replace("prefix = 'vm-captured-lookup-early-20261007'", "prefix = 'vm-captured-lookup-early-r2-20261007'")
text = text.replace("(('cpython3147', Path(sys.executable)), ('xlang3', candidate))", "(('cpython3147', Path(sys.executable)), ('control', root / 'build-repro/controls/captured-lookup-checkpoint-20261007/xlang3.exe'), ('xlang3', candidate))")
target.write_text(text, encoding='utf-8')
