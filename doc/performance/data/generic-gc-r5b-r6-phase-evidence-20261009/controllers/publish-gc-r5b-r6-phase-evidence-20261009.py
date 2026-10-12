"""Publish completed R5b/R6 measurements, phase attribution and experimental sources."""
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
assert subprocess.run(['git', 'diff', '--cached', '--quiet'], cwd=ROOT).returncode == 0
current = read(DATA / 'gc-generic-cycles-applied-source-r7-20261009.json')
assert all(sha(ROOT / p) == h for p, h in current['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in current['unowned_tracked_dirty_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in current['fixed_baseline_sha256'].items())
versions = {}
for version, name in (
    ('R2', 'gc-generic-cycles-performance-20261009.json'),
    ('R3', 'gc-generic-cycles-performance-r3-20261009.json'),
    ('R4', 'gc-generic-cycles-performance-r4-activity-20261009.json'),
    ('R5b', 'gc-generic-cycles-performance-r5b-20261009.json'),
    ('R6', 'gc-generic-cycles-performance-r6-activity-20261009.json')):
    doc = read(DATA / name)
    assert doc['terminal'] and doc['hashes_unchanged'] and not doc['fixed_gate']['passed']
    assert all(p['measurement_valid'] for p in doc['phases'])
    assert all(p['completed'] for p in doc['official'].values())
    versions[version] = doc
csvpath = DATA / 'gc-generic-cycles-r2-r6-scored-values-20261009.csv'
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
assert count == 400
phasepath = DATA / 'gc-generic-cycles-r4-r5b-phase-values-20261009.csv'
assert not phasepath.exists()
phasecount = 0
with phasepath.open('x', encoding='utf-8', newline='') as stream:
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(['version', 'process', 'group', 'phase', 'elapsed_nanoseconds', 'count'])
    for version, name in (('R4', 'gc-generic-cycles-phase-diagnostic-window-20261009.json'),
                           ('R5b', 'gc-generic-cycles-r5b-phase-diagnostic-20261009.json')):
        doc = read(DATA / name)
        assert doc['terminal'] and doc['status'] == 'diagnostic_completed' and doc['sources_and_releases_unchanged']
        assert all(p['external_process_watch']['measurement_valid'] for p in doc['phases'])
        assert len(doc['raw_phase_rows']) == 260
        for row in doc['raw_phase_rows']:
            writer.writerow([version, row['process'], row['group'], row['phase'], row['ns'], row['count']]); phasecount += 1
assert phasecount == 520
svgpath = ROOT / 'doc/performance/generic-cycle-discovery-scan-cost-r2-r6-20261009.svg'
assert not svgpath.exists()
elements = ['<svg xmlns="http://www.w3.org/2000/svg" width="900" height="535" viewBox="0 0 900 535" role="img" aria-labelledby="title desc">',
    '<title id="title">GC repair experiments through R6: elapsed milliseconds, lower is faster</title>',
    '<desc id="desc">Separate original fast runs. CPython 3.14.7 reference shown is the fresh R6 run. R2 through R5b fail the fixed gate; R6 is inconclusive. No engine acceptance or full-suite win.</desc>',
    '<rect width="900" height="535" fill="white"/>', '<g font-family="Arial,sans-serif" font-size="14" fill="#182b43">',
    '<text x="24" y="28" font-size="19" font-weight="bold">GC repair experiments — elapsed time (lower is faster)</text>',
    '<text x="24" y="51">Unpaired fast runs; all candidates remain uncommitted.</text>']
colors = {'CPython 3.14.7 (R6)': '#64748b', 'XLang3 R2': '#d97706', 'XLang3 R3': '#2563eb',
          'XLang3 R4': '#0d9488', 'XLang3 R5b': '#7c3aed', 'XLang3 R6': '#059669'}
for block, benchmark in enumerate(('create_gc_cycles', 'gc_traversal')):
    top = 89 + block * 205
    elements.append(f'<text x="24" y="{top}" font-weight="bold">{html.escape(benchmark)}</text>')
    series = [('CPython 3.14.7 (R6)', versions['R6']['official']['cpython3147']['scores'][benchmark]['mean_seconds'])]
    series += [('XLang3 ' + v, versions[v]['official']['xlang3']['scores'][benchmark]['mean_seconds']) for v in versions]
    for i, (label, seconds) in enumerate(series):
        y = top + 15 + i * 27
        ms = seconds * 1000
        assert 238 + ms * 65 + 90 < 900 and y + 20 < 490
        elements += [f'<text x="24" y="{y+15}">{html.escape(label)}</text>',
            f'<rect x="230" y="{y}" width="{ms*65:.4f}" height="19" fill="{colors[label]}"/>',
            f'<text x="{238+ms*65:.4f}" y="{y+15}">{ms:.3f} ms</text>']
elements += ['<text x="24" y="508">R2–R5b gates failed; R6 gate inconclusive. Individual cases do not update full97.</text>', '</g></svg>']
svgpath.write_text('\n'.join(elements) + '\n', encoding='utf-8', newline='\n')
archive = DATA / 'generic-gc-r5b-r6-phase-evidence-20261009'
assert not archive.exists()
archive.mkdir()
for version, control_name in (('r5b', 'gc-generic-cycles-r5b-failed-gate-20261009'),
                              ('r6', 'gc-generic-cycles-r6-inconclusive-gate-20261009')):
    app = read(DATA / f'gc-generic-cycles-applied-source-{version}-20261009.json')
    control = ROOT / 'build-repro/controls' / control_name
    for name in app['owned_paths']:
        source = control / 'sources' / name
        assert sha(source) == app['source_sha256'][name]
        target = archive / (version + '-experimental-source') / name
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
    shutil.copyfile(control / 'provenance.json', archive / (version + '-control-provenance.json'))
for version, control_name in (('r4', 'gc-phase-diagnostic-ready-20261009'), ('r5b', 'gc-r5b-phase-diagnostic-ready-20261009')):
    control = ROOT / 'build-repro/controls' / control_name
    manifest = read(control / 'provenance.json')
    for name in ('src/runtime/modules/system/gc_plain_cycles.h', 'src/runtime/modules/system/gc_module.cpp'):
        source = control / 'sources' / name
        assert sha(source) == manifest['source_sha256'][name]
        target = archive / (version + '-temporary-diagnostic-source') / name
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
prefixes = ('apply-gc-', 'update-gc-', 'build-gc-', 'check-gc-', 'validate-gc-', 'prepare-gc-',
            'wait-gc-', 'run-gc-', 'preserve-gc-', 'gc-phase-', 'gc-dormant-', 'publish-gc-')
for source in sorted((ROOT / 'scratch/performance').glob('*20261009.py')):
    if source.name.startswith(prefixes) and '-r7-' not in source.name:
        target = archive / 'controllers' / source.name
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
paths = [ROOT / 'doc/performance/generic-cycle-discovery-and-scan-cost-20261009.md', svgpath, csvpath, phasepath]
paths += [p for p in sorted(archive.rglob('*')) if p.is_file()]
paths += [p for p in sorted(DATA.iterdir()) if p.is_file() and p.name.startswith('gc-generic-cycles-') and '-r7-' not in p.name]
maps = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
receipt = DATA / 'generic-gc-r5b-r6-phase-publication-20261009.json'
assert not receipt.exists()
receipt.write_text(json.dumps({'status': 'docs_only_r5b_failed_r6_inconclusive_and_phase_attribution',
    'engine_paths_staged': [], 'artifact_sha256': maps, 'scored_value_count': count, 'diagnostic_row_count': phasecount,
    'controller_sha256': sha(__file__), 'scope': 'Experimental sources archived as evidence only. No engine acceptance; normal source143 R7 remains experimental. No full97 relabel.'}, indent=2) + '\n', encoding='utf-8', newline='\n')
maps[receipt.relative_to(ROOT).as_posix()] = sha(receipt)
for name, expected in maps.items():
    blob = subprocess.check_output(['git', 'hash-object', '-w', '--no-filters', name], cwd=ROOT, text=True).strip()
    subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name], cwd=ROOT, check=True)
    assert hashlib.sha256(subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT)).hexdigest() == expected
staged = set(subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, text=True).splitlines())
assert all(p in maps and p.startswith('doc/performance/') for p in staged)
assert all(sha(ROOT / p) == h for p, h in current['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in current['unowned_tracked_dirty_sha256'].items())
print(json.dumps({'staged_files': len(staged), 'engine_files': 0, 'publication_sha256': sha(receipt)}, indent=2))
