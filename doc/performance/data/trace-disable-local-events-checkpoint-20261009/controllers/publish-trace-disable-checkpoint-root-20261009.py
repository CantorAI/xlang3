"""Publish verified trace repairs and raw evidence; stage only owned changes."""
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
PREFIX = 'trace-disable-local-events-checkpoint-20261009'
REPORT = ROOT / 'doc/performance' / (PREFIX + '.md')
PACKAGE = DATA / PREFIX
MANIFEST = DATA / (PREFIX + '-publication.json')
HEAD = '09b0500e44820a4e557ca2774459957534702ddd'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
EXPECTED = {
    'nested-trace-setting-applied-source-20261009.json': '3fc7e9f94c1bc238da76a561e85fe030dfdfdbdbe7a06d0a697166696f5dadd5',
    'trace-disable-local-events-applied-source-20261009.json': '1c6fe36676991905c8b033fdbe1cfe08c7eab917a2cdd81a1b078b418e367109',
    'trace-disable-local-events-build-20261009.json': 'f0176d1af489fbe230847352b39dfd60857dc3f5bf5a37b38b06eec2b3bfe72e',
    'trace-disable-local-events-correctness-20261009.json': '617a0841153d9f7e9620c7b23fb5c00ab55ad4e5c81b930a792e7ec773dc3e98',
    'trace-disable-local-events-performance-20261009.json': '36a7d5794a1a7233224dbd2a5c56c1ab0a82f835cc84989d61c91b073d7a538b',
    'trace-disable-local-events-performance-r2-20261009.json': '40ba17cb61f766b9cdaa02afc64b11e4a9d1a509f30f2c383e77a208af3e052f',
    'object-new-route-current-balanced-20261009.json': '1b03d8b174af02164d4d29ce3144137d0409c0cdca14e2e7163fcc10a03ecea0',
    'coverage-backend-untimed-20261009.json': '84bd2f556864e0a01c8469e382cd982da367ebf6f4d4cb55e1941c7828107a19',
    'coverage-first-exception-untimed-20261009.json': '60a61a44604b6e46027efe9810743d1a1483182db457a5f9905e1ee95690c5be',
    'coverage-trace-events-untimed-20261009.json': '60a5b742e369c6429120309325b7bd965d6bcdd2de87d62e0fc9ce767a9e42b3',
    'coverage-first-exception-after-trace-disable-20261009.json': 'ede01c40dc8597d5ef8f50fd8331371d4e9700b8e70475c86ed23e01774afe8a',
}
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == HEAD
assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
assert not REPORT.exists() and not PACKAGE.exists() and not MANIFEST.exists()
for name, expected in EXPECTED.items():
    assert sha(DATA / name) == expected, name
app = read(DATA / 'trace-disable-local-events-applied-source-20261009.json')
correct = read(DATA / 'trace-disable-local-events-correctness-20261009.json')
valid = read(DATA / 'trace-disable-local-events-performance-r2-20261009.json')
route = read(DATA / 'object-new-route-current-balanced-20261009.json')
backend = read(DATA / 'coverage-backend-untimed-20261009.json')
assert correct['status'] == 'correctness_passed_performance_pending' and correct['correctness_passed']
assert correct['source_count'] == 137 and correct['ctest_count'] == 9
assert correct['fixture_counts'] == {'core': 405, 'compat_sections': 11, 'expected_failures': 3}
assert valid['status'] == 'fixed_gate_passed_coverage_completed' and valid['terminal'] and valid['hashes_unchanged']
assert valid['fixed_gate']['passed'] and valid['coverage']['completed']
assert not valid['coverage']['trace_changed_warning_present'] and not valid['coverage']['print_exception_type_error_present']
gate_path = DATA / valid['fixed_gate']['output']
assert sha(gate_path) == valid['fixed_gate']['sha256']
gate = read(gate_path)
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .10)
assert all(c['status'] == 'pass' and len(c['attempts']) == 1 for c in gate['cases'].values())
assert route['status'] == 'diagnostic_passed' and route['terminal'] and route['hashes_unchanged']
assert len(route['phases']) == 18 and route['expected_timed_operations'] == 360000
for receipt in (valid, route):
    assert all(p['measurement_valid'] for p in receipt['phases'])
    for phase in receipt['phases']:
        watch = phase['external_process_watch']
        assert not watch['overlaps'] and not watch['scanner_errors'] and watch['measurement_valid']
        assert sha(DATA / watch['log']) == watch['sha256']
for receipt, mapname in ((valid, 'tracked_sha256_before'), (route, 'pins_before')):
    assert all(sha(p) == h for p, h in receipt[mapname].items())
assert all(sha(ROOT / p) == h for p, h in correct['source_sha256'].items())
assert all(sha(ROOT / p) == h for p, h in app['unowned_tracked_dirty_sha256'].items())
owned = app['owned_paths']
assert len(owned) == len(set(owned)) == 10
protected = dict(app['unowned_tracked_dirty_sha256'])
attribute_before = (ROOT / '.gitattributes').read_bytes()
assert sha(ROOT / '.gitattributes') == protected['.gitattributes']

def values(path):
    data = read(path)
    item = next(b for b in data['benchmarks'] if b.get('metadata', {}).get('name', data.get('metadata', {}).get('name')) == 'coverage')
    return [v for run in item['runs'] for v in run.get('values', [])]
cp_path = DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'
x_path = DATA / 'trace-disable-local-events-performance-r2-20261009-coverage-fast.json'
assert sha(cp_path) == 'ac474df5576ac85f6f5350363affecc439d4f658b4dfa3dbe291b46964276eb0'
assert sha(x_path) == valid['coverage']['output_sha256']
cp, x = values(cp_path), values(x_path)
assert len(cp) == len(x) == 20 and all(v > 0 for v in cp + x)
cp_mean, x_mean = statistics.mean(cp), statistics.mean(x)
speed, elapsed = cp_mean / x_mean, x_mean / cp_mean
route_values = []
for phase in route['phases']:
    result = phase['result']
    assert result['timed_operation_count'] == 20000 and not result['verify_only']
    for case, seconds in result['seconds'].items():
        route_values.append((phase['label'], phase['selection'], case, seconds, seconds * 1e6 / 10000))
assert len(route_values) == 36
for selection, cases in route['medians_us'].items():
    for case, median in cases.items():
        assert statistics.median(row[4] for row in route_values if row[1] == selection and row[2] == case) == median
assert backend['status'] == 'backend_selection_recorded' and backend['hashes_unchanged']
assert [p['result']['backend_name'] for p in backend['phases']] == ['CTracer', 'PyTracer']

# Collect exact fresh evidence, including the initial harness failure and gate
# inconclusive attempt. Do not replace older raw files or copy runtime binaries.
prefixes = ('nested-trace-setting-', 'trace-disable-local-events-', 'coverage-first-exception-',
    'coverage-trace-events-', 'coverage-backend-', 'object-new-route-')
data_paths = sorted(p for p in DATA.iterdir() if p.is_file() and p.name.startswith(prefixes))
prior_ir_dir = DATA / 'object-new-route-untimed-20261009'
data_paths += sorted(p for p in prior_ir_dir.rglob('*') if p.is_file())
PACKAGE.mkdir()
controllers = PACKAGE / 'controllers'
controllers.mkdir()
controller_names = [
    'apply-nested-trace-setting-root-20261009.py', 'build-nested-trace-setting-root-20261009.py',
    'check-nested-trace-setting-correctness-root-20261009.py', 'validate-nested-trace-setting-performance-root-20261009.py',
    'apply-trace-disable-local-events-root-20261009.py', 'build-trace-disable-local-events-root-20261009.py',
    'check-trace-disable-local-events-correctness-root-20261009.py', 'validate-trace-disable-local-events-performance-root-20261009.py',
    'validate-trace-disable-local-events-performance-r2-root-20261009.py', 'build-python-new-vm-continuation-r4-root-20261009.cmd',
    'check-coverage-first-exception-root-20261009.py', 'coverage-untimed-first-exception-child-20261009.py',
    'check-coverage-trace-events-root-20261009.py', 'coverage-trace-events-untimed-child-20261009.py',
    'check-coverage-after-trace-disable-root-20261009.py', 'check-coverage-backend-root-20261009.py', 'coverage-backend-untimed-child-20261009.py',
    'check-object-new-route-root-20261009.py', 'verify-object-new-route-ir-root-20261009.py', 'verify-current-object-new-route-root-20261009.py',
    'measure-current-object-new-route-root-20261009.py', 'object-new-static-lookup-diagnostic-child-20261009.py',
    'validate-call-ex-cross-activation-constructor-resume-r3-20261008.py', Path(__file__).name,
]
for name in controller_names:
    source = ROOT / 'scratch/performance' / name
    shutil.copyfile(source, controllers / name)
sources = PACKAGE / 'measured-owned-sources'
for name in owned:
    target = sources / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / name, target)
assert all(sha(sources / p) == correct['source_sha256'][p] for p in owned)
coverage_csv = DATA / (PREFIX + '-coverage-values.csv')
with coverage_csv.open('x', encoding='utf-8', newline='') as stream:
    writer = csv.writer(stream); writer.writerow(('selection', 'index', 'seconds', 'comparison_scope'))
    for selection, vals in (('saved-cpython3147', cp), ('current-xlang3', x)):
        writer.writerows((selection, i, format(v, '.17g'), 'unpaired original fast benchmark') for i, v in enumerate(vals, 1))
route_csv = DATA / (PREFIX + '-route-values.csv')
with route_csv.open('x', encoding='utf-8', newline='') as stream:
    writer = csv.writer(stream); writer.writerow(('child', 'selection', 'case', 'seconds_per_10000', 'microseconds_per_call'))
    writer.writerows((label, selection, case, format(seconds, '.17g'), format(us, '.17g')) for label, selection, case, seconds, us in route_values)
rows = '\n'.join(f"| `{case}` | {route['medians_us']['cpython-python'][case]:.6f} | {route['medians_us']['xlang3-control'][case]:.6f} | {route['medians_us']['xlang3-current'][case]:.6f} |"
                 for case in ('original_lookup', 'saved_native_lookup'))
report = f'''# Trace disabling and Coverage checkpoint

The VM now stops all trace events when `sys.settrace(None)` is called, including
events for frames that retain a local trace hook. The local hooks remain intact
so tracing can resume when enabled again. This fixes a reproduced Coverage
stack-underflow failure. The original official Coverage benchmark now completes;
it remains substantially slower than CPython 3.14.7.

The preceding runtime repair propagates a nested callback's trace-setting change
to saved frame states. It publishes every replacement before retiring old hook
owners, so finalizer reentry cannot restore a stale setting. Event dispatch owns
its selected callback through materialization and completion. C++ audits cover
last-owner self-disable, replacement, and cleanup reentry across saved states.
Both comments and regression fixtures are included. No pure-Python library body,
IR format, instruction shape, or benchmark workload was replaced.

## Cause and validation

The two-group regression records only `outer_off:call` and `disable:call` on
CPython after the callback disables tracing. The previous X candidate also
emitted two local line events and `outer_off:return`. In an instrumented Python
Coverage tracer, those extra returns popped `data_stack` after tracing stopped,
eventually raising `IndexError: pop from empty list`. The VM callback failure
left no valid result exception for the CLI formatter, obscuring the original
error with its `print_exception` complaint.

The new candidate matches both CPython groups, including reactivation. Fresh
validation passed **405 core fixtures, 11 compatibility sections, three expected
failures, nine selected CTests, and both SQLite API checks**. The complete default
11-case fixed gate passed with **21 repeats, five warmups and the unchanged 10%
threshold**. The first complete gate was inconclusive for `json_dumps`: its first
interval crossed 1.10 while its confirmation passed. Both attempts are preserved;
one fresh complete gate then passed. No thresholds, baseline, cases, or loops
were changed. No further gate retry was needed.

The guard lives inside event emission. It adds no new check to ordinary
unmonitored opcode execution. The sticky trace-capability hint is not current
thread enablement. Existing local hook ownership is preserved during suspension.

## Original Coverage result

| Original official case | Saved CPython 3.14.7 mean | Current XLang3 mean | X speed, CP = 1× | X elapsed, CP = 1× |
|---|---:|---:|---:|---:|
| `coverage` | {cp_mean:.9f} s | {x_mean:.9f} s | {speed:.6f}× | {elapsed:.3f}× |

All 20 values from each runtime are retained. CPython is the saved October 7
full run; XLang3 is the fresh affected-case run. These fast-mode measurements
are unpaired, not evidence of statistical significance. X standard deviation
is {statistics.stdev(x):.9f} s; raw warnings remain in the log. The trace-changed
warning and exception-formatting error are absent from the completed run.

A separate **current, untimed** backend probe used the same Coverage 7.3.2
package and no core override: CPython selected native `coverage.CTracer` from
its CPython extension; XLang3 selected `coverage.pytracer.PyTracer`. This is a
real current native-backend gap. It is not independent proof of the saved
October 7 worker's backend. Any native replacement must be XLang3's own native
module with the compatible import/API; CPython's extension is not reused.

The previously published full97 matrix remains the older **75 completed / 22
failed** capture. This checkpoint retested Coverage only; it does not relabel
that full run as 76/97 or recompute its aggregate from a mixed capture.

## Remaining call-path evidence

The balanced artificial diagnostic compares a Python wrapper calling
`object.__new__(cls)` with another calling a saved native callable. Both retain
ordinary Python function entry, the same class and allocation, and the same
10000-operation loop. All six runtime permutations and mirrored case orders
produced 18 serial children and 36 timed loops; every value remains available.

| Artificial route, µs/call | CPython 3.14.7 | Accepted X R5 control | Current X |
|---|---:|---:|---:|
{rows}

The altered saved-call policy intentionally does not preserve dynamic attribute
replacement. It is diagnostic, not an accepted engine optimization. Its
difference includes lookup, dispatch and different global/register loads;
it is not a pure lookup CPU share, pickle workload fraction or predicted suite
gain. The source135 IR was retained for the identical child and unchanged
recorded compiler inputs; fresh source137/control semantics were checked without
timing. No fresh IR emission or dynamic optimization-hit count is claimed.

## Evidence and scope

[Current full correctness](data/trace-disable-local-events-correctness-20261009.json),
[fixed gate and original Coverage](data/trace-disable-local-events-performance-r2-20261009.json),
[first inconclusive gate](data/trace-disable-local-events-performance-20261009.json),
[all 40 Coverage values](data/{coverage_csv.name}),
[all 36 route loops](data/{route_csv.name}),
[balanced route receipt](data/object-new-route-current-balanced-20261009.json),
[current backend observation](data/coverage-backend-untimed-20261009.json).

The measured build is the exact recorded source137 worktree and Release178
receipt at `build-repro/main-verify-20261006/Release/xlang3.exe`; the fixed
baseline177 remains unchanged. Only ten owned engine/test paths are staged.
Unrelated dirty files are preserved and excluded. The selected source inventory
and exact owned-source archive are partial provenance, not a clean-checkout
reproduction claim. Runtime/control binaries are not checked in. Engine index
line endings are normalized to LF; measured owned bytes are archived exactly.

Strict idle checks preceded/followed timing. The one-second compiler/CTest
watcher observed no overlap or scanner failure; processes between samples may
be missed. Official execution pins cover the recorded sources, binaries,
runner and gate cases; this receipt does not retrospectively prove every
transitive benchmark/dependency/native byte. The backend/minimal probes retain
their separate scope. There is no broad CPython speed win or whole-goal
completion claim.
'''
REPORT.write_text(report, encoding='utf-8', newline='\n')
attributes = b'\n/doc/performance/data/nested-trace-setting-* -text\n/doc/performance/data/trace-disable-local-events-* -text\n/doc/performance/data/trace-disable-local-events-*/** -text\n/doc/performance/data/coverage-first-exception-* -text\n/doc/performance/data/coverage-trace-events-* -text\n/doc/performance/data/coverage-backend-* -text\n/doc/performance/data/object-new-route-* -text\n/doc/performance/data/object-new-route-*/** -text\n/doc/performance/trace-disable-local-events-checkpoint-20261009.md text eol=lf\n'
head_attributes = subprocess.check_output(['git', 'show', 'HEAD:.gitattributes'], cwd=ROOT)
assert b'/doc/performance/data/trace-disable-local-events-*' not in head_attributes
attribute_working = attribute_before + attributes
(ROOT / '.gitattributes').write_bytes(attribute_working)
paths = sorted(set([*owned, '.gitattributes', REPORT.relative_to(ROOT).as_posix(), coverage_csv.relative_to(ROOT).as_posix(), route_csv.relative_to(ROOT).as_posix(),
                    *(p.relative_to(ROOT).as_posix() for p in data_paths), *(p.relative_to(ROOT).as_posix() for p in PACKAGE.rglob('*') if p.is_file())]))
manifest = {'terminal': True, 'status': 'verified_checkpoint_ready_to_commit', 'parent_head': HEAD,
    'owned_engine_paths': owned, 'core': 405, 'source_count': 137, 'fixed_gate_passed': True, 'original_coverage_completed': True,
    'coverage_mean_seconds': {'saved_cpython3147': cp_mean, 'current_xlang3': x_mean}, 'coverage_speed': speed,
    'coverage_elapsed': elapsed, 'evidence_expected_sha256': EXPECTED,
    'exact_measured_owned_source_sha256': {p: correct['source_sha256'][p] for p in owned},
    'artifact_sha256': {p: sha(ROOT / p) for p in paths if p not in owned and p != '.gitattributes'},
    'attribute_unowned_prefix_sha256': hashlib.sha256(attribute_before).hexdigest(),
    'attribute_working_sha256': sha(ROOT / '.gitattributes'),
    'attribute_staged_sha256': hashlib.sha256(head_attributes + attributes).hexdigest(),
    'whole_goal_complete': False, 'broad_speed_gain_claimed': False, 'staged_paths': paths + [MANIFEST.relative_to(ROOT).as_posix()]}
MANIFEST.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
paths = sorted(manifest['staged_paths'])
assert all(sha(ROOT / p) == h for p, h in protected.items() if p != '.gitattributes')
assert (ROOT / '.gitattributes').read_bytes().startswith(attribute_before)
assert all(sha(ROOT / p) == h for p, h in correct['source_sha256'].items())
# Construct an exact temporary index. Raw evidence bypasses filters; owned
# engine/test entries use canonical LF, without rewriting measured work bytes.
git_dir = Path(subprocess.check_output(['git', 'rev-parse', '--absolute-git-dir'], cwd=ROOT, text=True).strip())
index = git_dir / 'index'
assert not (git_dir / 'index.lock').exists()
with tempfile.TemporaryDirectory(prefix='trace-checkpoint-index-', dir=ROOT / 'scratch/performance') as temporary:
    temp_index = Path(temporary) / 'index'
    env = dict(os.environ, GIT_INDEX_FILE=str(temp_index))
    subprocess.run(['git', 'read-tree', 'HEAD'], cwd=ROOT, env=env, check=True)
    for p in paths:
        raw = (ROOT / p).read_bytes()
        if p == '.gitattributes': raw = head_attributes + attributes
        elif p in owned: raw = raw.replace(b'\r\n', b'\n')
        oid = subprocess.check_output(['git', 'hash-object', '-w', '--stdin', '--no-filters'], cwd=ROOT, input=raw).decode().strip()
        subprocess.run(['git', 'update-index', '--add', '--cacheinfo', '100644', oid, p], cwd=ROOT, env=env, check=True)
    staged = subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, env=env, text=True).splitlines()
    assert set(staged) == set(paths)
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == HEAD
    assert not subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT)
    lock = git_dir / 'index.lock'
    with lock.open('xb') as stream: stream.write(temp_index.read_bytes())
    os.replace(lock, index)
assert set(subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=ROOT, text=True).splitlines()) == set(paths)
print(json.dumps({'status': 'owned_checkpoint_staged', 'staged_paths': len(paths), 'owned_engine_paths': len(owned),
    'publication_sha256': sha(MANIFEST), 'coverage_elapsed_vs_saved_cpython3147': elapsed}, indent=2))
