"""Publish terminal R4 evidence and an exact restored, uncommitted engine identity."""
import csv
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
restore = DATA / 'gc-generic-cycles-r4-restored-after-diagnostic-refusals-20261009.json'
assert sha(restore) == '203be65173d8789b4fbbc266f3f9a2846171959f9a80f0e61eb9d75b0684cb00'
state = read(restore)
assert all(sha(ROOT / p) == h for p, h in state['restored_source_sha256'].items())
release = ROOT / 'build-repro/main-verify-20261006/Release'
assert all(sha(release / p) == h for p, h in state['restored_release_sha256'].items())
app = read(DATA / 'gc-generic-cycles-applied-source-r4-20261009.json')
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
assert subprocess.run(['git', 'diff', '--cached', '--quiet'], cwd=ROOT).returncode == 0
versions = {}
for version, name in (('R2', 'gc-generic-cycles-performance-20261009.json'),
        ('R3', 'gc-generic-cycles-performance-r3-20261009.json'),
        ('R4', 'gc-generic-cycles-performance-r4-activity-20261009.json')):
    doc = read(DATA / name)
    assert doc['terminal'] and doc['hashes_unchanged'] and not doc['fixed_gate']['passed']
    assert all(p['measurement_valid'] for p in doc['phases'])
    assert all(p['completed'] for p in doc['official'].values())
    versions[version] = doc
csvpath = DATA / 'gc-generic-cycles-r2-r3-r4-scored-values-20261009.csv'
assert not csvpath.exists()
count = 0
with csvpath.open('x', encoding='utf-8', newline='') as stream:
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(['version', 'runtime', 'benchmark', 'value_index', 'elapsed_seconds'])
    for version, doc in versions.items():
        for runtime, info in doc['official'].items():
            for benchmark, score in info['scores'].items():
                assert len(score['values']) == 20
                for index, value in enumerate(score['values'], 1):
                    writer.writerow([version, runtime, benchmark, index, repr(value)]); count += 1
assert count == 240
svgpath = ROOT / 'doc/performance/generic-cycle-discovery-and-scan-cost-20261009.svg'
assert not svgpath.exists()
elements = ['<svg xmlns="http://www.w3.org/2000/svg" width="880" height="455" viewBox="0 0 880 455" role="img" aria-labelledby="title desc">',
    '<title id="title">GC repair experiments: elapsed milliseconds, lower is faster</title>',
    '<desc id="desc">Separate fast runs. CPython 3.14.7 reference is from the R4 run. R2, R3 and R4 all fail the fixed gate; no whole-suite win is claimed.</desc>',
    '<rect width="880" height="455" fill="white"/>',
    '<g font-family="Arial,sans-serif" font-size="14" fill="#182b43">',
    '<text x="24" y="28" font-size="19" font-weight="bold">GC repair experiments — elapsed time (lower is faster)</text>',
    '<text x="24" y="51">Directional fast runs; engine candidates remain uncommitted.</text>']
colors = {'CPython 3.14.7 (R4)': '#64748b', 'XLang3 R2': '#d97706', 'XLang3 R3': '#2563eb', 'XLang3 R4': '#0d9488'}
for block, benchmark in enumerate(('create_gc_cycles', 'gc_traversal')):
    top = 94 + block * 163
    elements.append(f'<text x="24" y="{top}" font-weight="bold">{html.escape(benchmark)}</text>')
    series = [('CPython 3.14.7 (R4)', versions['R4']['official']['cpython3147']['scores'][benchmark]['mean_seconds'])]
    series += [('XLang3 ' + v, versions[v]['official']['xlang3']['scores'][benchmark]['mean_seconds']) for v in versions]
    for i, (label, seconds) in enumerate(series):
        y = top + 15 + i * 29
        ms = seconds * 1000
        elements += [f'<text x="24" y="{y+16}">{html.escape(label)}</text>',
            f'<rect x="210" y="{y}" width="{ms*68:.4f}" height="20" fill="{colors[label]}"/>',
            f'<text x="{218+ms*68:.4f}" y="{y+16}">{ms:.3f} ms</text>']
elements += ['<text x="24" y="423">R2/R3/R4 failed the unchanged gate. These rows do not update the full97 aggregate.</text>', '</g></svg>']
svgpath.write_text('\n'.join(elements) + '\n', encoding='utf-8', newline='\n')
archive = DATA / 'generic-gc-r4-terminal-evidence-20261009'
assert not archive.exists()
archive.mkdir()
for name in app['owned_paths']:
    target = archive / 'r4-failed-gate-source' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / name, target)
    assert sha(target) == app['source_sha256'][name]
diag = ROOT / 'build-repro/controls/gc-phase-diagnostic-ready-20261009'
for name in ('src/runtime/modules/system/gc_plain_cycles.h', 'src/runtime/modules/system/gc_module.cpp'):
    target = archive / 'temporary-diagnostic-source' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(diag / 'sources' / name, target)
    assert sha(target) == read(diag / 'provenance.json')['source_sha256'][name]
prefixes = ('apply-gc-', 'update-gc-', 'build-gc-', 'check-gc-', 'validate-gc-', 'prepare-gc-',
            'resume-gc-', 'run-gc-', 'preserve-gc-', 'gc-dormant-', 'gc-phase-', 'publish-gc-r4-')
for source in sorted((ROOT / 'scratch/performance').glob('*20261009.py')):
    if source.name.startswith(prefixes):
        target = archive / 'controllers' / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
paths = [ROOT / 'doc/performance/generic-cycle-discovery-and-scan-cost-20261009.md', svgpath, csvpath]
paths += [p for p in sorted(archive.rglob('*')) if p.is_file()]
paths += [p for p in sorted(DATA.rglob('*')) if p.is_file() and p.relative_to(DATA).parts[0].startswith('gc-generic-cycles-')]
maps = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
receipt = DATA / 'generic-gc-r4-terminal-publication-20261009.json'
assert not receipt.exists()
receipt.write_text(json.dumps({'status': 'docs_only_terminal_r4_failed_gate', 'engine_paths_staged': [],
    'artifact_sha256': maps, 'scored_value_count': count, 'restoration_sha256': sha(restore),
    'controller_sha256': sha(__file__), 'scope': 'Terminal R4 measurements, diagnostic refusals and exact restoration; no engine acceptance or full97 relabel.'}, indent=2) + '\n', encoding='utf-8', newline='\n')
maps[receipt.relative_to(ROOT).as_posix()] = sha(receipt)
for name, expected in maps.items():
    blob = subprocess.check_output(['git', 'hash-object', '-w', '--no-filters', name], cwd=ROOT, text=True).strip()
    subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name], cwd=ROOT, check=True)
    assert hashlib.sha256(subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT)).hexdigest() == expected
staged = set(subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, text=True).splitlines())
assert all(p in maps and p.startswith('doc/performance/') for p in staged)
assert all(sha(ROOT / p) == h for p, h in state['restored_source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
print(json.dumps({'staged_files': len(staged), 'engine_files': 0, 'publication_sha256': sha(receipt)}, indent=2))
