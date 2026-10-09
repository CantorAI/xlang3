"""Publish terminal trial evidence without changing measured inputs or staging."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3').resolve()
DATA = ROOT/'doc/performance/data'
SCRATCH = ROOT/'scratch/performance'
ARCHIVE = DATA/'dict-scalar-append-checkpoint-20261008'
VALIDATION = DATA/'dict-scalar-append-runtime-index-r3-validation-20261008.json'
VALIDATION_SHA = '0b6f05e17c2878efa135656abcce38338934069b59a9460de9f2ba98a8516dd0'
MANIFEST = DATA/'dict-scalar-append-checkpoint-publication-20261008.json'
OWNED = ['src/runtime/mapping.cpp', 'tests/cpp/interpreter_tests.cpp',
         'tests/cpp/dict_scalar_append_index_cases.h']
sha = lambda data: hashlib.sha256(data).hexdigest()
read = lambda path: json.loads(path.read_bytes())
assert sys.version_info[:3] == (3, 14, 7)
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert Path.cwd().resolve() == ROOT and not MANIFEST.exists() and not ARCHIVE.exists()
assert sha(VALIDATION.read_bytes()) == VALIDATION_SHA
v = read(VALIDATION)
assert v['status'] == 'trial_validated' and v['terminal'] and v['full_validated']
assert v['hashes_unchanged'] and not v['whole_goal_complete']
assert len(v['source_sha256']) == 115 and len(v['phases']) == 3
assert all(p['passed'] and p['exit_code'] == 0 and p['measurement_valid'] for p in v['phases'])
for path, expected in v['source_sha256'].items():
    assert sha((ROOT/path).read_bytes()) == expected, path
focus = read(DATA/'dict-scalar-append-runtime-index-r3-focused-20261008.json')
assert len(focus['binaries_sha256']) == 178
for path, expected in focus['binaries_sha256'].items():
    assert sha((ROOT/path).read_bytes()) == expected, path
for run in v['official_results'].values():
    assert run['values_count'] == 20 and sha((DATA/run['output']).read_bytes()) == run['sha256']
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip()
assert head == '3f18dc86dcfcdcd9c4c2616a3f2fdd7d61d14e00'
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
dirty = {p:sha((ROOT/p).read_bytes()) for p in subprocess.check_output(
    ['git','diff','HEAD','--name-only','-z'],cwd=ROOT).decode().split('\0') if p}
rows = []
def capture(source, destination):
    source = source.resolve(strict=True)
    destination = destination.resolve()
    assert source.is_relative_to(ROOT) and destination.is_relative_to(ROOT/'doc/performance')
    raw = source.read_bytes()
    if source != destination:
        assert not destination.exists(), destination
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(raw)
    rows.append({'source':source.relative_to(ROOT).as_posix(),
                 'destination':destination.relative_to(ROOT).as_posix(),
                 'sha256':sha(raw),'bytes':len(raw)})

# Keep raw failures, warnings and all paired observations, including empty logs.
for path in sorted(DATA.glob('dict-scalar-append*')):
    if path.is_file(): capture(path,path)
for path in sorted(DATA.glob('pickle-frame-locals-retirement-r4*')):
    if path.is_file(): capture(path,path)
controls = [p for p in SCRATCH.iterdir() if p.is_file() and
            'dict-scalar-append' in p.name and p.suffix in ('.py','.ps1','.cmd','.cpp','.json','.patch')]
controls += [SCRATCH/'pickle-original-pure-python-diagnostic-proposed-20261008.py',
             SCRATCH/'validate-call-ex-cross-activation-constructor-resume-r3-20261008.py']
for path in sorted(set(controls)):
    capture(path,ARCHIVE/'controllers'/path.name)
parent = ROOT/'build-repro/controls/dict-scalar-append-runtime-index-parent-20261008'
capture(parent/'preserved-release-provenance.json',ARCHIVE/'parent-release-provenance.json')
parent_manifest = read(parent/'preserved-release-provenance.json')
assert len(parent_manifest['source_snapshot_sha256']) == 114
for path, expected in parent_manifest['source_snapshot_sha256'].items():
    source = parent/'source-snapshot'/path
    assert sha(source.read_bytes()) == expected,path
    capture(source,ARCHIVE/'parent-source'/path)
for path,expected in v['source_sha256'].items():
    capture(ROOT/path,ARCHIVE/'candidate-source'/path)

# A native SVG bar chart preserves the exact numerical source in the receipt.
official = v['official_results']
cpms = official['cpython3147']['mean_seconds']*1000
xms = official['xlang3']['mean_seconds']*1000
scale = 130.0
chart = ROOT/'doc/performance/dict-scalar-append-runtime-index-trial-20261008.svg'
assert not chart.exists()
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="960" height="330" viewBox="0 0 960 330" role="img" aria-labelledby="title desc">
<title id="title">Pure-Python pickle: XLang3 versus CPython 3.14.7</title>
<desc id="desc">Mean milliseconds, lower is better. CPython {cpms:.6f}; XLang3 {xms:.6f}. XLang3 takes {xms/cpms:.2f} times CPython's time. Fresh unpaired fast-mode results, 20 values per runtime.</desc>
<rect width="960" height="330" fill="#fff"/>
<g font-family="Arial, sans-serif" fill="#182332">
<text x="28" y="36" font-size="22" font-weight="bold">Original pure-Python pickle · protocol 5</text>
<text x="28" y="63" font-size="15">Mean elapsed milliseconds · shorter bars are faster</text>
<text x="28" y="122" font-size="16">CPython 3.14.7</text>
<rect x="215" y="98" width="{cpms*scale:.4f}" height="34" rx="3" fill="#237a57"/>
<text x="{225+cpms*scale:.4f}" y="121" font-size="16">{cpms:.6f} ms</text>
<text x="28" y="187" font-size="16">XLang3 candidate</text>
<rect x="215" y="163" width="{xms*scale:.4f}" height="34" rx="3" fill="#ba6424"/>
<text x="{225+xms*scale:.4f}" y="186" font-size="16">{xms:.6f} ms</text>
<line x1="215" y1="216" x2="800" y2="216" stroke="#687789"/>
'''
for tick in range(5):
    x=215+tick*scale
    svg+=f'<line x1="{x}" y1="212" x2="{x}" y2="221" stroke="#687789"/><text x="{x}" y="240" text-anchor="middle" font-size="14">{tick}</text>\n'
svg+=f'''<text x="800" y="240" font-size="14">ms</text>
<text x="28" y="278" font-size="17" font-weight="bold">XLang3 speed: {cpms/xms:.5f}× CPython · {xms/cpms:.2f}× longer time</text>
<text x="28" y="307" font-size="13">20 values each · fresh unpaired fast mode · stability warnings retained · one case, full-suite goal open</text>
</g></svg>
'''
chart.write_text(svg,encoding='utf-8',newline='\n')
capture(chart,chart)
for name in ('dict-scalar-append-runtime-index-trial-20261008.md',
             'pickle-memo-index-freshness-investigation-20261008.md'):
    p=ROOT/'doc/performance'/name
    capture(p,p)
assert len({r['destination'] for r in rows}) == len(rows)
manifest = {'status':'terminal_validated_trial_publication','parent_head':head,
            'validation_sha256':VALIDATION_SHA,'whole_goal_complete':False,
            'owned_engine_targets':OWNED,'recorded_candidate_sources':115,
            'recorded_parent_sources':114,'candidate_release_count':178,
            'fixed_baseline_count':177,'raw_bytes_preserved':True,
            'source_inventory_is_partial':True,'files':rows,'file_count':len(rows)}
MANIFEST.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
for p,expected in dirty.items(): assert sha((ROOT/p).read_bytes()) == expected,p
print(json.dumps({'manifest':str(MANIFEST),'sha256':sha(MANIFEST.read_bytes()),
                  'file_count':len(rows),'bytes':sum(r['bytes'] for r in rows)},indent=2))
