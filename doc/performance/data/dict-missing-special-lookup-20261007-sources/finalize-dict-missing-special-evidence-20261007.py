"""Archive terminal validation only; never run workloads or modify the engine."""
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
prefix = 'dict-missing-special-lookup'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
record_path = data / (prefix + '-validation-20261007.json')
record = json.loads(record_path.read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
assert record['fixture_counts'] == {'core': 372, 'compatibility_sections': 11, 'expected_failures': 3}
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
for name, sha in record['source_sha256'].items():
    assert digest(root / name) == sha
for phase in record['phases']:
    assert digest(data / phase['log']) == phase['sha256']
    assert phase['name'] == 'official-bpe' or phase['exit_code'] == 0
gate_path = data / record['fixed_gate']['output']
assert digest(gate_path) == record['fixed_gate']['sha256']
gate = json.loads(gate_path.read_text())
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
before_path = data / (prefix + '-before-20261007.json')
after_path = data / (prefix + '-after-20261007.json')
before, after = (json.loads(path.read_text()) for path in (before_path, after_path))
assert after['matches_cpython3147'] and after['exit_code'] == 0
assert after['results'] == before['observations'][0]['results']
assert after['binary_sha256'] == record['candidate_binary_sha256']
assert after['probe_sha256'] == before['probe_sha256']
export_source = root / 'scratch/performance/export-native-trivial-callback-pairs-20261007.py'
export_code = export_source.read_text(encoding='utf-8').replace('native-trivial-callback', prefix)
exported = {}
exec(compile(export_code, str(export_source), 'exec'), exported)
paired_path = data / record['paired_callbacks']
paired = json.loads(paired_path.read_text())
assert paired['binaries_sha256']['candidate'] == record['candidate_binary_sha256']

def values(path):
    suite = json.loads(path.read_text())
    cases = [case for case in suite['benchmarks'] if case.get('metadata', {}).get('name', suite.get('metadata', {}).get('name')) == 'bpe_tokeniser']
    assert len(cases) == 1
    result = [value for run in cases[0]['runs'] for value in run.get('values', [])]
    assert len(result) == 20
    return result

official = record['official_bpe']
official_path = data / official['output']
reference = data / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'
reference_info = json.loads(reference.with_name(reference.stem + '-provenance.json').read_text())
assert reference_info['runtime_version'] == '3.14.7' and reference_info['exit_code'] == 0
assert reference_info['compatibility_hook_sha256'] == record['compatibility_hook_sha256']
benchmark_source = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_bpe_tokeniser\run_benchmark.py')
benchmark_sha = digest(benchmark_source)
assert benchmark_sha == reference_info['benchmark_python_sources'][r'bm_bpe_tokeniser\run_benchmark.py']
assert benchmark_sha == 'c7255345499e118181785370b0996e8cc1056491b9678b3fbeb02421e3f9b2df'
comparison = {'scope': 'Official BPE descriptive comparison; reused references, not alternating paired official runs',
              'candidate_exit_code': official['exit_code'], 'candidate_binary_sha256': record['candidate_binary_sha256'],
              'benchmark_source_sha256': benchmark_sha, 'compatibility_hook_sha256': record['compatibility_hook_sha256']}
official_text = 'Official BPE failed or timed out and is unscored. Its original log is retained.'
if official['exit_code'] == 0:
    assert digest(official_path) == official['sha256']
    comparison['status'] = 'completed'
    for label, path in (('cpython3147', reference), ('previous_xlang3', data / 'pyperformance-native-trivial-callback-bpe-fast-20261007.json'), ('candidate', official_path)):
        samples = values(path)
        comparison[label] = {'json': path.name, 'sha256': digest(path), 'values_seconds': samples,
                             'mean_seconds': statistics.mean(samples), 'sample_standard_deviation_seconds': statistics.stdev(samples)}
    current, python, previous = (comparison[label]['mean_seconds'] for label in ('candidate', 'cpython3147', 'previous_xlang3'))
    comparison['cpython_time_over_candidate_time_speed'] = python / current
    comparison['candidate_time_over_cpython_time'] = current / python
    comparison['previous_xlang3_time_over_candidate_time_speedup'] = previous / current
    comparison['candidate_pyperf_instability_warning'] = 'WARNING:' in (data / (prefix + '-validation-20261007-official-bpe.log')).read_text()
    official_text = (f'Official means: CPython 3.14.7 **{python:.3f} s**, preceding XLang3 **{previous:.3f} s**, '
                     f'candidate **{current:.3f} s**. Candidate speed relative to CPython is **{python/current:.3f}×** '
                     f'(**{current/python:.2f}× longer runtime**); nominal speedup over preceding XLang3 is '
                     f'**{previous/current:.3f}×**. Candidate sample SD is **{comparison["candidate"]["sample_standard_deviation_seconds"]:.3f} s**. '
                     f'Pyperf instability warning: **{comparison["candidate_pyperf_instability_warning"]}**. '
                     'These reused references do not establish an alternating-pair significance claim.')
else:
    comparison['status'] = 'candidate_failed_unscored'
comparison_path = data / (prefix + '-bpe-vs-cpython3147-20261007.json')
comparison_path.write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')

archive = data / (prefix + '-20261007-sources')
assert not archive.exists()
archive.mkdir()
manifest = {'warning': 'Historical one-off scripts; do not replay unchanged.', 'files': {}}
scripts = ['dict-missing-special-lookup-probe-20261007.py', 'observe-dict-missing-special-lookup-20261007.py',
           'verify-dict-missing-special-result-20261007.py', 'prepare-dict-missing-special-validation-20261007.py',
           'validate-dict-missing-special-lookup-20261007.py', 'compare-dict-missing-special-trial-20261007.py',
           'export-dict-missing-special-pairs-20261007.py', 'export-native-trivial-callback-pairs-20261007.py',
           'native-trivial-callback-probe-20261007.py', 'dict-callback-boundary-probe-20261007.py',
           'dict-composite-scaling-probe-20261007.py', 'finalize-dict-missing-special-evidence-20261007.py',
           'prepare-dict-missing-special-staging-20261007.py', 'stage-dict-missing-special-lookup-checkpoint-20261007.py']
for name in scripts:
    source, target = root / 'scratch/performance' / name, archive / name
    target.write_bytes(source.read_bytes())
    manifest['files'][name] = digest(target)
    assert digest(source) == digest(target)
assert manifest['files']['dict-missing-special-lookup-probe-20261007.py'] == before['probe_sha256']
for observed in paired['probes']:
    filename = 'native-trivial-callback-probe-20261007.py' if observed['name'] == 'trivial' else 'dict-callback-boundary-probe-20261007.py'
    assert observed['probe_sha256'] == manifest['files'][filename]
compiled = archive / 'compiled-sources'
compiled.mkdir()
source_info = {'compiler_input_sha256': record['source_sha256'], 'repository_lf_sha256': {}}
for name, sha in record['source_sha256'].items():
    raw = (root / name).read_bytes()
    target = compiled / Path(name).name
    assert not target.exists()
    target.write_bytes(raw)
    manifest['files']['compiled-sources/' + target.name] = sha
    source_info['repository_lf_sha256'][name] = hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest()
(archive / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
source_path = data / (prefix + '-source-provenance-20261007.json')
source_path.write_text(json.dumps(source_info, indent=2) + '\n', encoding='utf-8')
control_path = data / (prefix + '-preserved-control-20261007.json')
control_path.write_bytes((root / 'build-repro/controls/native-trivial-callback-checkpoint-20261007/preserved-release-provenance.json').read_bytes())
control = json.loads(control_path.read_text())
assert len(control['files_sha256']) == 140
assert control['files_sha256']['xlang3.exe'] == paired['binaries_sha256']['control']['exe']
assert control['files_sha256']['xlang3_runtime.dll'] == paired['binaries_sha256']['control']['dll']
report = root / 'doc/performance/dict-missing-special-lookup-checkpoint-20261007.md'
assert not report.exists()
text = '''# Dictionary missing-key special lookup

XLang3 now resolves dict-subclass __missing__ on the type, matching CPython
3.14.7 dict_subscript. Instance attributes cannot shadow this special method.
Ordinary raw Python/native methods receive owned self/key arguments directly,
avoiding a BoundMethod object and an owning argument vector on every miss.
Static/class methods, properties and user descriptors retain class-based binding;
binding failures keep their Python exception and traceback. Current class lookup
observes method replacement and deletion. Counter remains the original Python
library, and tracing/monitoring remain on the generic Python call path.

The same dispatcher serves both runtime miss branches and explicit native
dict.__getitem__. Native membership and setdefault query storage without invoking
subclass __getitem__ or __missing__, as do get and pop. Performance comments
explain the allocation avoided and the protocol/ownership constraints.

## Correctness and validation

The preserved pre-change differential shows instance shadowing, broken custom
descriptor binding, swallowed descriptor exceptions, and setdefault incorrectly
calling __getitem__. All **13** corrected protocol observations match CPython
3.14.7. A new six-part fixture additionally verifies live class mutation, static/
class/property binding, error tracebacks, storage semantics and tracing.

The candidate passed **372 core fixtures**, 11 compatibility sections, three
expected-failure checks, eight C++/SDK/graph checks, and the unchanged fixed
Release gate: 11 cases, 21 paired repeats, five warmups and 10% tolerance.
The accepted control preserves 140 Release files. The build/run directory remains
build-repro/main-verify-20261006/Release. No pure-Python library was translated
into C++ and no CPython native extension was reused.

''' + official_text + '''

Official BPE retains the original workload, fast mode, shared hook/dependency
site and 1,800-second cap. Warmups/calibration are excluded from scoring. This
case does not replace the separate complete 97-case comparison or establish a
whole-suite win against CPython.

## Paired diagnostics

Seven alternating process pairs, five samples and one warmup per row retain
**770 raw samples** and all 11 rows. Ratios above 1× mean faster than the preceding
XLang3. Missing-key reads improve in all seven pairs. Some unchanged controls
measure slower, including dict.get/default and direct lookup; those rows are
retained. Diagnostic ratios are separate from the fixed gate and official score.

| Probe | Mapping | Path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | --- | ---: | ---: |
''' + '\n'.join(exported['table']) + '''

## Evidence

- [Terminal validation](data/dict-missing-special-lookup-validation-20261007.json)
- [Fixed gate](data/release-dict-missing-special-lookup-fixed-gate-20261007.json)
- [Official log](data/dict-missing-special-lookup-validation-20261007-official-bpe.log)
- [Official comparison](data/dict-missing-special-lookup-bpe-vs-cpython3147-20261007.json)
- [Pre-change differential](data/dict-missing-special-lookup-before-20261007.json)
- [Corrected differential](data/dict-missing-special-lookup-after-20261007.json)
- [Original paired results](data/dict-missing-special-lookup-paired-20261007.json)
- [All samples](data/dict-missing-special-lookup-paired-samples-20261007.csv)
- [All summary rows](data/dict-missing-special-lookup-paired-summary-20261007.csv)
- [Compiler source provenance](data/dict-missing-special-lookup-source-provenance-20261007.json)
- [Archived scripts and compiler inputs](data/dict-missing-special-lookup-20261007-sources/manifest.json)
- [Preserved control](data/dict-missing-special-lookup-preserved-control-20261007.json)
- [Preceding checkpoint](native-trivial-callback-checkpoint-20261007.md)

Reference implementation: [CPython 3.14.7 Objects/dictobject.c](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c), dict_subscript.
'''
report.write_bytes(text.encode('utf-8'))
for target in re.findall(r'\]\(([^)]+)\)', text):
    assert target.startswith('https:') or (report.parent / target).exists(), target
evidence = [record_path, gate_path, before_path, after_path, paired_path, exported['sample_path'],
            exported['summary_path'], comparison_path, source_path, control_path, report]
evidence += [data / phase['log'] for phase in record['phases']]
evidence += [path for path in archive.rglob('*') if path.is_file()]
if official_path.exists():
    evidence.append(official_path)
inventory = {'status': 'terminal', 'raw_sample_count': 770,
             'files': {str(path.relative_to(root)).replace('\\', '/'): digest(path) for path in evidence}}
(data / (prefix + '-evidence-20261007.json')).write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Archived and verified', len(evidence), 'terminal evidence files')
