"""Hash the full unchanged Chameleon workload; not a timing benchmark."""
import hashlib
import json
import runpy
import sys
from pathlib import Path

sys.path.insert(0, str(Path('venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages').resolve()))
benchmark = runpy.run_path(
    r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_chameleon\run_benchmark.py',
    run_name='diagnostic')
template = benchmark['PageTemplate'](benchmark['BIGTABLE_ZPT'])
table = [dict(a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8, i=9, j=10)
         for _ in range(500)]
output = template(options={'table': table})
print(json.dumps({'purpose': 'untimed render identity', 'characters': len(output),
                  'utf8_sha256': hashlib.sha256(output.encode('utf-8')).hexdigest()}))
