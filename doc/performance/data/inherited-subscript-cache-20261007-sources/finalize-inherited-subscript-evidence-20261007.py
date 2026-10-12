"""Archive terminal evidence without launching a runtime or a measurement."""
import hashlib
import json
from pathlib import Path
import runpy
import statistics
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
record_path = data / 'inherited-subscript-cache-validation-20261007.json'
record = json.loads(record_path.read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {
    'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert all(digest(root / name) == sha for name, sha in record['source_sha256'].items())
for phase in record['phases']:
    assert digest(data / phase['log']) == phase['sha256']
    if phase['name'] != 'official-bpe':
        assert phase['exit_code'] == 0
gate_path = data / record['fixed_gate']['output']
assert digest(gate_path) == record['fixed_gate']['sha256']
gate = json.loads(gate_path.read_text())
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
paired_path = data / record['paired_dispatch']
paired = json.loads(paired_path.read_text())
assert paired['status'] == 'terminal' and len(paired['pairs']) == 7
assert paired['binaries_sha256']['candidate'] == record['candidate_binary_sha256']
archive = data / 'inherited-subscript-cache-20261007-sources'
assert not archive.exists()
archive.mkdir()
scripts = [
    'validate-inherited-subscript-cache-20261007.py',
    'compare-inherited-subscript-trial-20261007.py',
    'inherited-subscript-dispatch-probe-20261007.py',
    'preserve-inherited-subscript-control-20261007.py',
    'prepare-inherited-subscript-validation-20261007.py',
    'class-method-publication-probe-20261007.py',
    'observe-class-method-publication-20261007.py',
    'finalize-inherited-subscript-evidence-20261007.py',
    'export-inherited-subscript-pairs-20261007.py',
    'stage-inherited-subscript-checkpoint-20261007.py',
]
manifest = {'warning': 'Historical one-off controllers; do not replay unchanged.', 'files': {}}
for name in scripts:
    source = root / 'scratch/performance' / name
    target = archive / name
    target.write_bytes(source.read_bytes())
    assert digest(target) == digest(source)
    manifest['files'][name] = digest(target)
assert paired['probe_sha256'] == manifest['files'][scripts[2]]
for source, name in [
    (root / 'scratch/performance/class-method-publication-before-20261007.json',
     'class-method-publication-before-20261007.json'),
    (root / 'build-repro/controls/dict-intrinsic-index-checkpoint-20261007/preserved-release-provenance.json',
     'preserved-release-provenance.json'),
]:
    target = data / ('inherited-subscript-cache-' + name)
    assert not target.exists()
    target.write_bytes(source.read_bytes())
    manifest['files']['../' + target.name] = digest(target)
(archive / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')

exported = runpy.run_path(str(root / 'scratch/performance/export-inherited-subscript-pairs-20261007.py'))
sample_path, summary_path, sample_count, table = (
    exported[name] for name in ('sample_path', 'summary_path', 'sample_count', 'table'))

official = record['official_bpe']
official_path = data / official['output']
if official['sha256'] is not None:
    assert digest(official_path) == official['sha256']
reference_path = data / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'
reference_provenance_path = reference_path.with_name(reference_path.stem + '-provenance.json')
reference_provenance = json.loads(reference_provenance_path.read_text())
assert reference_provenance['runtime_version'] == '3.14.7'
assert reference_provenance['exit_code'] == 0 and reference_provenance['status'] == 'finished'
assert reference_provenance['compatibility_hook_sha256'] == record['compatibility_hook_sha256']
workload_hash = reference_provenance['benchmark_python_sources']['bm_bpe_tokeniser\\run_benchmark.py']
assert digest(Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_bpe_tokeniser\run_benchmark.py')) == workload_hash
def bpe_values(path):
    suite = json.loads(path.read_text())
    cases = [case for case in suite['benchmarks']
             if case.get('metadata', {}).get('name', suite.get('metadata', {}).get('name')) == 'bpe_tokeniser']
    assert len(cases) == 1
    return [value for run in cases[0]['runs'] for value in run.get('values', [])]
reference_values = bpe_values(reference_path)
assert len(reference_values) == 20
comparison = {'scope': 'Official BPE only; CPython reference reused from same-day full fast run, not alternating pairs',
              'reference': {'json': reference_path.name, 'sha256': digest(reference_path),
                            'provenance_sha256': digest(reference_provenance_path),
                            'version': '3.14.7', 'values_seconds': reference_values,
                            'mean_seconds': statistics.mean(reference_values)},
              'candidate': {'json': official_path.name, 'sha256': official['sha256'],
                            'exit_code': official['exit_code']},
              'benchmark_source_sha256': workload_hash,
              'compatibility_hook_sha256': record['compatibility_hook_sha256']}
official_comparison_text = ''
if official['exit_code'] == 0:
    candidate_values = bpe_values(official_path)
    assert len(candidate_values) == 20
    candidate_mean = statistics.mean(candidate_values)
    reference_mean = statistics.mean(reference_values)
    comparison['status'] = 'completed'
    comparison['candidate'].update(values_seconds=candidate_values, mean_seconds=candidate_mean)
    comparison['cpython_time_over_xlang3_time_speed'] = reference_mean / candidate_mean
    comparison['xlang3_time_over_cpython_time'] = candidate_mean / reference_mean
    official_comparison_text = (f'\n\nThe completed official means are CPython 3.14.7 **{reference_mean:.3f} s** '
                               f'and XLang3 **{candidate_mean:.3f} s**. XLang3 speed relative to '
                               f'CPython is **{reference_mean / candidate_mean:.3f}×**, or '
                               f'**{candidate_mean / reference_mean:.2f}× longer runtime**. '
                               'The CPython reference is reused from the same-day full fast run; '
                               'this is not an alternating before/after comparison or a significance claim.')
else:
    comparison['status'] = 'candidate_failed_unscored'
comparison_path = data / 'inherited-subscript-cache-bpe-vs-cpython3147-20261007.json'
assert not comparison_path.exists()
comparison_path.write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
official_text = ('The official affected BPE run completed; see its raw JSON. '
                 'The paired diagnostic table below is a separate measurement.'
                 if official['exit_code'] == 0 else
                 'The official affected BPE run failed or timed out; see its original log. '
                 'No completed BPE score or full-suite speedup is claimed.')
report = root / 'doc/performance/inherited-subscript-cache-checkpoint-20261007.md'
assert not report.exists()
report.write_text('''# Inherited subscript cache checkpoint

This change reduces generic inherited subscript dispatch overhead. Python
`collections.Counter` and the BPE algorithm remain Python. The comparison
reference is CPython 3.14.7; diagnostic speedups below compare the new XLang3
candidate with the preceding XLang3 checkpoint, not with CPython.

## Implementation and lifetime rules

GetItem and SetItem already cached direct Python/native methods. Their miss
path now resolves inherited methods through the existing class lookup API.
Receiver class identity and the class version guard each weak cache entry.
Base mutation recursively invalidates derived class versions. Native eligibility
still requires ordinary descriptor binding, no expression capture, and a valid
callback; custom descriptors and static/class methods keep normal binding.
Python methods retain their VM frame and error traceback. Cache entries do not
create persistent owners of functions or classes.

Class attribute replacement now retains an aliased incoming value, detaches
the replaced owner, publishes the new value, and invalidates lookup caches
before releasing the old owner. Deletion similarly invalidates before release.
This prevents finalizer reentry from seeing stale methods or stale weak cache
pointers. The before probe observed the old descriptor during replacement in
XLang3 but the new descriptor in CPython. The new fixture verifies the corrected
ordering, inherited native/Python methods, mutation/deletion, base reassignment,
binding fallbacks, missing keys and traceback frames. Code comments explain the
allocation avoided and the required guards and lifetime ordering.

## Validation

The candidate passed 368 core fixtures, 11 compatibility sections, the three
expected-failure checks, and all eight selected C++/SDK/graph tests. The fixed
Release regression gate passed all 11 cases using its unchanged defaults:
21 paired repeats, five warmups and a 10% threshold. Candidate binary and source
hashes, commands, logs, baseline checks and outcomes are in the validation JSON.
Build and run paths remain `build-repro/main-verify-20261006/Release`.

''' + official_text + ''' The official run used the unchanged workload and fast
mode, with the same compatibility hook and dependency site. Its observation
cap was 1,800 seconds, increased from the preceding trial's retained 1,200-second
timeout because the unchanged full body had taken approximately 44 seconds
per invocation. This cap does not reduce workload or sample count.
''' + official_comparison_text + '''

## Paired dispatch diagnostics

Seven alternating control/candidate process pairs each ran one warmup and five
samples per path and key count. Every resulting key value was checked. Each
ratio divides the control median by its paired candidate median; the table
reports the median of those seven ratios. Higher than 1× is faster than the
preceding XLang3. Small changes in direct paths are descriptive, not statistical
significance claims. All 980 raw samples and all 14 summary rows are preserved.

| Dispatch path | Keys | Speedup over preceding XLang3 | Favorable pairs |
| --- | ---: | ---: | ---: |
''' + '\n'.join(table) + '''

These diagnostics do not establish a win over CPython or replace the full
97-case pyperformance comparison. The last full comparison and the preceding
dictionary checkpoint remain separately recorded.

## Evidence

- [Validation and provenance](data/inherited-subscript-cache-validation-20261007.json)
- [Unchanged fixed gate](data/release-inherited-subscript-cache-fixed-gate-20261007.json)
- [Original paired results](data/inherited-subscript-cache-paired-20261007.json)
- [All paired samples](data/inherited-subscript-cache-paired-samples-20261007.csv)
- [Paired summary](data/inherited-subscript-cache-paired-summary-20261007.csv)
- [Original official BPE log](data/inherited-subscript-cache-validation-20261007-official-bpe.log)
- [Official BPE comparison / failure record](data/inherited-subscript-cache-bpe-vs-cpython3147-20261007.json)
- [Archived scripts and hashes](data/inherited-subscript-cache-20261007-sources/manifest.json)
- [Before publication semantics](data/inherited-subscript-cache-class-method-publication-before-20261007.json)
- [Preserved control provenance](data/inherited-subscript-cache-preserved-release-provenance.json)
- [Preceding dictionary checkpoint](dict-intrinsic-index-checkpoint-20261007.md)
- [Last full CPython 3.14.7 comparison](pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007.md)
''', encoding='utf-8')
evidence = [record_path, paired_path, gate_path, sample_path, summary_path, report, comparison_path]
evidence += [data / phase['log'] for phase in record['phases']]
evidence += list(archive.iterdir())
evidence += [data / ('inherited-subscript-cache-' + name) for name in (
    'class-method-publication-before-20261007.json', 'preserved-release-provenance.json')]
if official_path.exists():
    evidence.append(official_path)
inventory = {'status': 'terminal', 'raw_sample_count': sample_count,
             'files': {str(path.relative_to(root)).replace('\\', '/'): digest(path) for path in evidence}}
(data / 'inherited-subscript-cache-evidence-20261007.json').write_text(
    json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Archived and verified', len(evidence), 'files;', sample_count, 'paired samples')
