"""Stage only nine validated GC engine/test paths and their durable evidence."""
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
app_path = DATA / 'gc-generic-cycles-applied-source-r7b-20261009.json'
app = read(app_path)
preserved = DATA / 'gc-generic-cycles-r7b-preserved-validated-checkpoint-20261009.json'
assert sha(preserved) == '83b9d41ac39f066623327017b1024013d91348cc337f9ab5151d693981041950'
state = read(preserved)
assert state['terminal'] and state['correctness_passed'] and state['fixed_gate_passed'] and state['affected_originals_completed']
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
release = ROOT / 'build-repro/main-verify-20261006/Release'
assert all(sha(release / p) == h for p, h in state['release_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
assert all(sha(ROOT / 'build-repro/Release' / p) == h for p, h in app['fixed_baseline_sha256'].items())
review = DATA / 'gc-generic-cycles-r7b-static-review-20261009.json'
assert not review.exists()
review_hashes = {
    'src/runtime/modules/system/gc_plain_node_index.h': '51531f00bc9413ba95cd12462f469ede565bffde72379b9a78061455e772b6c4',
    'src/runtime/modules/system/gc_plain_cycles.h': '8bc966f7b13892d73811d28ecc907ea8a6247b8c47e8303193ae0f3ef97194a9',
    'tests/cpp/generic_gc_cases.h': 'fdddc80f9b7118513686cc86af7917cc486863d8bfb6a64f8cd1df385c4eb654'}
assert all(sha(ROOT / p) == h for p, h in review_hashes.items())
review.write_text(json.dumps({'reviewer': '/root/class_scope_design', 'status': 'static_READY_r7b',
    'source_sha256': review_hashes, 'mode': 'Read-only static review, no runtime claim',
    'transcript': 'Engine bytes match reviewed R7. Dense lookup bounds/identity guard and exact pointer fallback precede mutation. The coverage gap is fixed: first slot is valid (1), so the separate case reaches the second huge slot and verifies sparse lookup. No edits, builds or execution performed.',
    'root_runtime_evidence': state['performance_sha256']}, indent=2) + '\n', encoding='utf-8', newline='\n')
versions = {}
for version, name in (('R2', 'gc-generic-cycles-performance-20261009.json'),
    ('R3', 'gc-generic-cycles-performance-r3-20261009.json'), ('R4', 'gc-generic-cycles-performance-r4-activity-20261009.json'),
    ('R5b', 'gc-generic-cycles-performance-r5b-20261009.json'), ('R6', 'gc-generic-cycles-performance-r6-activity-20261009.json'),
    ('R7b', 'gc-generic-cycles-performance-r7b-activity-20261009.json')):
    doc = read(DATA / name)
    assert doc['terminal'] and doc['hashes_unchanged'] and all(p['measurement_valid'] for p in doc['phases'])
    assert all(p['completed'] for p in doc['official'].values())
    versions[version] = doc
assert versions['R7b']['fixed_gate']['passed']
csvpath = DATA / 'gc-generic-cycles-r2-r7b-scored-values-20261009.csv'
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
assert count == 480
svgpath = ROOT / 'doc/performance/generic-cycle-discovery-scan-cost-r2-r7b-20261009.svg'
assert not svgpath.exists()
elements = ['<svg xmlns="http://www.w3.org/2000/svg" width="900" height="565" viewBox="0 0 900 565" role="img" aria-labelledby="title desc">',
    '<title id="title">GC repair experiments through R7b: elapsed milliseconds, lower is faster</title>',
    '<desc id="desc">Separate original fast runs; CPython reference is fresh R7b. R7b passes the fixed gate; preceding candidates fail or are inconclusive. Full97 pending.</desc>',
    '<rect width="900" height="565" fill="white"/>', '<g font-family="Arial,sans-serif" font-size="14" fill="#182b43">',
    '<text x="24" y="28" font-size="19" font-weight="bold">GC repair experiments — elapsed time (lower is faster)</text>',
    '<text x="24" y="51">Unpaired fast runs. R7b checkpoint passes the unchanged gate; full97 pending.</text>']
colors = {'CPython 3.14.7 (R7b)': '#64748b', 'XLang3 R2': '#d97706', 'XLang3 R3': '#2563eb',
          'XLang3 R4': '#0d9488', 'XLang3 R5b': '#7c3aed', 'XLang3 R6': '#059669', 'XLang3 R7b': '#db2777'}
for block, benchmark in enumerate(('create_gc_cycles', 'gc_traversal')):
    top = 89 + block * 225
    elements.append(f'<text x="24" y="{top}" font-weight="bold">{html.escape(benchmark)}</text>')
    series = [('CPython 3.14.7 (R7b)', versions['R7b']['official']['cpython3147']['scores'][benchmark]['mean_seconds'])]
    series += [('XLang3 ' + v, versions[v]['official']['xlang3']['scores'][benchmark]['mean_seconds']) for v in versions]
    for i, (label, seconds) in enumerate(series):
        y = top + 15 + i * 27; ms = seconds * 1000
        assert 238 + ms * 65 + 90 < 900 and y + 20 < 520
        elements += [f'<text x="24" y="{y+15}">{html.escape(label)}</text>',
            f'<rect x="230" y="{y}" width="{ms*65:.4f}" height="19" fill="{colors[label]}"/>',
            f'<text x="{238+ms*65:.4f}" y="{y+15}">{ms:.3f} ms</text>']
elements += ['<text x="24" y="543">GC traversal: nominal 1.192× CPython speed. Cycle creation remains slower.</text>', '</g></svg>']
svgpath.write_text('\n'.join(elements) + '\n', encoding='utf-8', newline='\n')
archive = DATA / 'generic-gc-r7b-checkpoint-evidence-20261009'
assert not archive.exists(); archive.mkdir()
control = Path(state['control'])
for name in app['owned_paths']:
    source = control / 'sources' / name
    assert sha(source) == app['source_sha256'][name]
    target = archive / 'validated-source' / name
    target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
shutil.copyfile(control / 'provenance.json', archive / 'preserved-control-provenance.json')
prefixes = ('record-gc-', 'update-gc-r7-', 'build-gc-', 'check-gc-', 'prepare-gc-', 'validate-gc-', 'wait-gc-', 'preserve-gc-', 'stage-gc-')
for source in sorted((ROOT / 'scratch/performance').glob('*20261009.py')):
    if source.name.startswith(prefixes) and ('-r7-' in source.name or '-r7b-' in source.name):
        target = archive / 'controllers' / source.name
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
paths = [ROOT / name for name in app['owned_paths']]
paths += [ROOT / 'doc/performance/generic-cycle-discovery-and-scan-cost-20261009.md', svgpath, csvpath, review]
paths += [p for p in sorted(archive.rglob('*')) if p.is_file()]
paths += [p for p in sorted(DATA.rglob('*')) if p.is_file() and p.relative_to(DATA).parts[0].startswith('gc-generic-cycles-') and ('-r7-' in p.relative_to(DATA).parts[0] or '-r7b-' in p.relative_to(DATA).parts[0])]
maps = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
receipt = DATA / 'generic-gc-r7b-checkpoint-publication-20261009.json'
assert not receipt.exists()
receipt.write_text(json.dumps({'status': 'validated_generic_gc_r7b_engine_checkpoint',
    'engine_paths_staged': app['owned_paths'], 'artifact_sha256': maps, 'scored_value_count': count,
    'preserved_checkpoint_sha256': sha(preserved), 'controller_sha256': sha(__file__),
    'scope': 'Full correctness/default gate/affected original GC definitions pass. Fixed baseline unchanged. Full97 pending; no overall CPython win claimed.'}, indent=2) + '\n', encoding='utf-8', newline='\n')
maps[receipt.relative_to(ROOT).as_posix()] = sha(receipt)
for name, expected in maps.items():
    blob = subprocess.check_output(['git', 'hash-object', '-w', '--no-filters', name], cwd=ROOT, text=True).strip()
    subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name], cwd=ROOT, check=True)
    assert hashlib.sha256(subprocess.check_output(['git', 'show', ':' + name], cwd=ROOT)).hexdigest() == expected
staged = set(subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, text=True).splitlines())
assert all(p in maps and (p in app['owned_paths'] or p.startswith('doc/performance/')) for p in staged)
assert set(app['owned_paths']) <= staged
assert all(sha(ROOT / p) == h for p, h in app['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
assert all(sha(release / p) == h for p, h in state['release_sha256'].items())
print(json.dumps({'staged_files': len(staged), 'engine_files': len(app['owned_paths']), 'publication_sha256': sha(receipt)}, indent=2))
