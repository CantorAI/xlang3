"""Archive terminal measurements; never run workloads or modify engine files."""
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
prefix = 'vm-captured-lookup'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
record_path = data / (prefix + '-validation-20261007.json')
record = json.loads(record_path.read_text(encoding='utf-8'))
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
assert record['fixture_counts'] == {'core': 375, 'compatibility_sections': 11, 'expected_failures': 3}
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
for name, sha in record['source_sha256'].items():
    assert digest(root / name) == sha, name
for phase in record['phases']:
    assert digest(data / phase['log']) == phase['sha256']
    assert phase['name'] == 'official-bpe' or phase['exit_code'] == 0
gate_path = data / record['fixed_gate']['output']
assert digest(gate_path) == record['fixed_gate']['sha256']
gate = json.loads(gate_path.read_text(encoding='utf-8'))
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
early_path = data / record['early_evidence']['output']
assert digest(early_path) == record['early_evidence']['sha256']
early = json.loads(early_path.read_text(encoding='utf-8'))
assert early['status'] == 'terminal'
for phase in early['phases']:
    assert phase['exit_code'] == 0 and digest(data / phase['log']) == phase['sha256']
failed_path = data / (prefix + '-early-20261007.json')
failed = json.loads(failed_path.read_text(encoding='utf-8'))
assert failed['status'] == 'failed_xlang3'
assert digest(root / 'scratch/performance/vm-captured-lookup-before-fixture-repair-20261007.py') == failed['source_sha256']['tests/fixtures/core/vm_captured_lookup.py']
for phase in failed['phases']:
    assert digest(data / phase['log']) == phase['sha256']
paired_path = data / (prefix + '-paired-20261007.json')
paired = json.loads(paired_path.read_text(encoding='utf-8'))
assert paired['status'] == 'terminal' and paired['binaries_sha256']['candidate'] == record['candidate_binary_sha256']
control = root / 'build-repro/controls/captured-lookup-checkpoint-20261007'
control_info = json.loads((control / 'preserved-release-provenance.json').read_text(encoding='utf-8'))
assert control_info['commit'] == '3f44b72cb1f8b2bf3c013db896e03d2c6dd8a463'
for name, sha in control_info['files_sha256'].items():
    assert digest(control / name) == sha, name
control_evidence = data / (prefix + '-preserved-control-20261007.json')
control_evidence.write_bytes((control / 'preserved-release-provenance.json').read_bytes())
raw_rows, summary_rows = [], []
for probe in paired['probes']:
    assert len(probe['pairs']) == 7
    for pair_index, pair in enumerate(probe['pairs']):
        for label in pair['order']:
            for row_index, row in enumerate(pair['runs'][label]['rows']):
                assert len(row['samples_seconds']) == 5
                for sample_index, value in enumerate(row['samples_seconds']):
                    raw_rows.append([probe['name'], row_index, pair_index + 1, '/'.join(pair['order']),
                                     label, sample_index + 1, value])
    for row_index, row in enumerate(probe['summary']):
        summary_rows.append([probe['name'], row_index, json.dumps(row['identity'], sort_keys=True),
                             row['median_speedup'], row['pairs_favoring_candidate'],
                             *row['control_time_over_candidate_time']])
assert len(raw_rows) == 770 and len(summary_rows) == 11
samples_path, summary_path = (data / (prefix + suffix + '-20261007.csv') for suffix in ('-paired-samples', '-paired-summary'))
for path, header, rows in ((samples_path, ['probe', 'row', 'pair', 'order', 'runtime', 'sample', 'seconds'], raw_rows),
                            (summary_path, ['probe', 'row', 'identity', 'median_speedup', 'favorable_pairs', *['pair_' + str(i + 1) for i in range(7)]], summary_rows)):
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        writer.writerows(rows)
def scored(path):
    suite = json.loads(path.read_text(encoding='utf-8'))
    cases = [case for case in suite['benchmarks'] if case.get('metadata', {}).get('name', suite.get('metadata', {}).get('name')) == 'bpe_tokeniser']
    assert len(cases) == 1
    values = [value for run in cases[0]['runs'] for value in run.get('values', [])]
    assert len(values) == 20
    return values
official = record['official_bpe']
official_path = data / official['output']
comparison = {'scope': 'Official BPE descriptive comparison using reused references; not alternating paired official runs',
              'candidate_exit_code': official['exit_code'], 'candidate_binary_sha256': record['candidate_binary_sha256']}
official_text = 'Official BPE failed or timed out and is unscored; its original log is retained.'
if official['exit_code'] == 0:
    assert digest(official_path) == official['sha256']
    reference_info = json.loads((data / 'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json').read_text(encoding='utf-8'))
    assert reference_info['runtime_version'] == '3.14.7' and reference_info['exit_code'] == 0
    assert reference_info['compatibility_hook_sha256'] == record['compatibility_hook_sha256']
    source = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_bpe_tokeniser\run_benchmark.py')
    assert digest(source) == 'c7255345499e118181785370b0996e8cc1056491b9678b3fbeb02421e3f9b2df'
    assert digest(source) == reference_info['benchmark_python_sources'][r'bm_bpe_tokeniser\run_benchmark.py']
    comparison['benchmark_source_sha256'] = digest(source)
    comparison['compatibility_hook_sha256'] = record['compatibility_hook_sha256']
    for label, path in (('cpython3147', data / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'),
                        ('previous_xlang3', data / 'pyperformance-captured-lookup-r2-bpe-fast-20261007.json'), ('candidate', official_path)):
        values = scored(path)
        comparison[label] = {'json': path.name, 'sha256': digest(path), 'values_seconds': values,
                             'mean_seconds': statistics.mean(values), 'sample_standard_deviation_seconds': statistics.stdev(values)}
    current, python, previous = (comparison[label]['mean_seconds'] for label in ('candidate', 'cpython3147', 'previous_xlang3'))
    warning = 'WARNING:' in (data / (prefix + '-validation-20261007-official-bpe.log')).read_text(encoding='utf-8')
    comparison.update(status='completed', previous_time_over_candidate_time_speedup=previous/current,
                      cpython_time_over_candidate_time_speed=python/current, candidate_time_over_cpython_time=current/python,
                      candidate_pyperf_instability_warning=warning)
    official_text = (f'Official BPE means: CPython 3.14.7 **{python:.3f} s**, preceding XLang3 **{previous:.3f} s**, '
                     f'candidate **{current:.3f} s**. Candidate speed versus CPython is **{python/current:.3f}×** '
                     f'(**{current/python:.2f}× longer runtime**). Nominal speedup versus preceding XLang3 is '
                     f'**{previous/current:.3f}×**. Sample SD **{comparison["candidate"]["sample_standard_deviation_seconds"]:.3f} s**; '
                     f'pyperf instability warning **{warning}**. Reused references do not prove paired significance.')
else:
    comparison['status'] = 'candidate_failed_unscored'
comparison_path = data / (prefix + '-bpe-vs-cpython3147-20261007.json')
comparison_path.write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
archive = data / (prefix + '-20261007-sources')
assert not archive.exists()
archive.mkdir()
scripts = ['preserve-vm-captured-lookup-control-20261007.py', 'compare-vm-captured-lookup-20261007.py',
           'check-vm-captured-lookup-20261007.py', 'prepare-vm-captured-lookup-r2-20261007.py',
           'check-vm-captured-lookup-r2-20261007.py', 'validate-vm-captured-lookup-20261007.py',
           'finalize-vm-captured-lookup-20261007.py', 'stage-vm-captured-lookup-20261007.py',
           'native-trivial-callback-probe-20261007.py', 'dict-callback-boundary-probe-20261007.py',
           'vm-captured-lookup-before-fixture-repair-20261007.py']
manifest = {'warning': 'Historical terminal evidence; do not replay unchanged scripts.', 'files': {}}
for name in scripts:
    source, target = root / 'scratch/performance' / name, archive / name
    target.write_bytes(source.read_bytes())
    manifest['files'][name] = digest(target)
for probe in paired['probes']:
    assert probe['probe_sha256'] == manifest['files'][probe['filename']]
source_info = {'warning': 'Archive preserves exact compiled/tested working bytes; Git normalizes source line endings.',
               'working_sha256': {}, 'repository_lf_sha256': {}}
for name, sha in record['source_sha256'].items():
    source, target = root / name, archive / 'compiled-sources' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(source.read_bytes())
    assert digest(target) == sha
    manifest['files'][target.relative_to(archive).as_posix()] = sha
    source_info['working_sha256'][name] = sha
    source_info['repository_lf_sha256'][name] = hashlib.sha256(source.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
(archive / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
source_path = data / (prefix + '-source-provenance-20261007.json')
source_path.write_text(json.dumps(source_info, indent=2) + '\n', encoding='utf-8')
table = '\n'.join(f'| {probe["name"]} | {row["identity"]["path"]} {row["identity"].get("mapping", "")} | {row["median_speedup"]:.3f}× | {row["pairs_favoring_candidate"]}/7 |'
                  for probe in paired['probes'] for row in probe['summary'])
report = root / 'doc/performance/vm-captured-lookup-checkpoint-20261007.md'
report.write_bytes(('''# Ordinary calls to captured dictionary lookup functions

Ordinary CALL, CALL_LOCAL and CALL_GLOBAL now share the captured-item IR shortcut
with native callback entry, including warmed UserFunction and saved bound-method
sites. Indexed names were already cheap: the cost removed is the Python frame's
register/local setup and ownership work for a proven nonfallible dictionary hit.
Current code/signature and live closure cells are checked every call. The Python
body and Counter remain Python; no CPython native implementation is reused.

Only ordinary CALL sites opt into the shared trampoline. Keywords/expansion,
missing/default arguments, generators/async functions, misses, overrides,
descriptors, custom key protocols and active observability keep original entry.
The existing canonical intrinsic dictionary hit guard remains unchanged. The
shortcut neither advances the instruction nor pushes a frame. Its caller uses
Next, preserving monitoring refresh after an old output finalizer; property,
subscription and constructor contexts retain their original frame/return paths.
Comments record these performance and correctness constraints.

## Correctness and fixed gate

The candidate passed **375 core fixtures**, 11 compatibility sections, three
expected failures, eight C++/SDK/graph checks, and the unchanged fixed gate:
11 cases, 21 paired repeats, five warmups, 10% tolerance. Python 3.14.7, preserved
control and candidate all passed the ten-part dedicated fixture. Coverage includes
warm direct/bound calls, cell/code replacement, binding/errors, overrides and key
protocols, return contexts, trace/profile/local monitoring and finalizer refresh.
The existing Release build/run path is unchanged. Accepted control preserves
140 Release files; validation fingerprints the compiled inputs and binary pair.

The first finalizer fixture constructed its object in a module-level dictionary
literal. Both control and candidate failed its prompt-release assertion, while
CPython passed. Construction in a completed function isolates CALL-output release
from module temporaries and all three then passed. The original fixture, failed
logs and source hashes are retained. **Module temporary lifetime remains an
unresolved correctness difference**, not a repaired engine behavior.

An unrelated xMind build delayed the early check. It was left alone; observation
timeouts did not trigger build/benchmark restarts. Engine/build/benchmark phases
were serial. The quick paired check preceded the expensive validation to reject
bad or ineffective candidates sooner; read-only reviews ran in parallel.

## Performance evidence

''' + official_text + '''

The original BPE source/workload, fast mode, shared hook/dependency site and
1,800-second cap remain. Calibration/warmups are unscored. BPE native key entry
was already optimized in the preceding checkpoint; the new ordinary-call path
does not imply another native callback gain. This single case neither replaces
the complete 97-case report nor proves a whole-suite CPython win. GC traversal
remains withheld from official comparisons.

Seven alternating diagnostic pairs preserve **770 samples and 11 rows**, including
neutral/slower controls. Each process checks outputs and warms each row. Ratios
above 1× mean faster than preceding XLang3, not faster than CPython. In particular,
dict_get_default is slower in every pair; this control remains visible even though
the unchanged fixed gate passed. Diagnostics include Python loops and comparisons;
they do not isolate the cost of one native operation.

| Probe | Path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
''' + table + '''

[Validation](data/vm-captured-lookup-validation-20261007.json),
[official comparison](data/vm-captured-lookup-bpe-vs-cpython3147-20261007.json),
[paired raw record](data/vm-captured-lookup-paired-20261007.json),
[samples](data/vm-captured-lookup-paired-samples-20261007.csv),
[summary](data/vm-captured-lookup-paired-summary-20261007.csv),
[fixed gate](data/release-vm-captured-lookup-fixed-gate-20261007.json),
[compiled source provenance](data/vm-captured-lookup-source-provenance-20261007.json),
[source archive](data/vm-captured-lookup-20261007-sources/manifest.json),
[last full comparison](pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007.md).
''').encode('utf-8'))
for target in re.findall(r'\]\(([^)]+)\)', report.read_text(encoding='utf-8')):
    assert (report.parent / target).exists(), target
files = [record_path, gate_path, early_path, failed_path, paired_path, samples_path,
         summary_path, control_evidence, comparison_path, source_path, report,
         data / 'build-vm-captured-lookup-Release-20261007.log',
         data / 'vm-captured-lookup-control-finalizer-check-20261007.log',
         *[data / phase['log'] for evidence in (record, early, failed) for phase in evidence['phases']],
         *[path for path in archive.rglob('*') if path.is_file()]]
if official_path.exists():
    files.append(official_path)
inventory = {'status': 'terminal', 'raw_sample_count': 770, 'summary_row_count': 11,
             'files': {path.relative_to(root).as_posix(): digest(path) for path in sorted(set(files))}}
(data / (prefix + '-evidence-20261007.json')).write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Verified and archived', len(inventory['files']), 'terminal evidence files')
print(official_text)
