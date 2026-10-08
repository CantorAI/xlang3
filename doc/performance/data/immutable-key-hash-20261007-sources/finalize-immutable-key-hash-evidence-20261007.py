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
prefix = 'immutable-key-hash'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
record_path = data / (prefix + '-validation-20261007.json')
record = json.loads(record_path.read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
assert record['fixture_counts'] == {'core': 373, 'compatibility_sections': 11, 'expected_failures': 3}
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
import runpy
exported = runpy.run_path(str(root / 'scratch/performance/export-immutable-key-hash-pairs-20261007.py'))
baseline_path = data / 'immutable-key-hash-baseline-20261007.json'
baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
assert baseline['status'] == 'terminal'
assert all(item['exit_code'] == 0 for item in baseline['observations'])
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
    for label, path in (('cpython3147', reference), ('previous_xlang3', data / 'pyperformance-dict-missing-special-lookup-bpe-fast-20261007.json'), ('candidate', official_path)):
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
scripts = ['immutable-key-hash-probe-20261007.py', 'observe-immutable-key-hash-baseline-20261007.py',
           'compare-immutable-key-hash-trial-20261007.py', 'prepare-immutable-key-hash-validation-20261007.py',
           'validate-immutable-key-hash-20261007.py', 'export-immutable-key-hash-pairs-20261007.py',
           'dict-composite-scaling-probe-20261007.py', 'native-trivial-callback-probe-20261007.py',
           'dict-callback-boundary-probe-20261007.py', 'prepare-immutable-key-hash-evidence-20261007.py',
           'finalize-immutable-key-hash-evidence-20261007.py', 'stage-immutable-key-hash-checkpoint-20261007.py']
for name in scripts:
    source, target = root / 'scratch/performance' / name, archive / name
    target.write_bytes(source.read_bytes())
    manifest['files'][name] = digest(target)
    assert digest(source) == digest(target)
assert manifest['files']['immutable-key-hash-probe-20261007.py'] == baseline['probe_sha256']
for observed in paired['probes']:
    assert observed['probe_sha256'] == manifest['files'][observed['filename']]
assert record['scaling_probe_sha256'] == manifest['files']['dict-composite-scaling-probe-20261007.py']
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
control_path.write_bytes((root / 'build-repro/controls/dict-missing-special-lookup-checkpoint-20261007/preserved-release-provenance.json').read_bytes())
control = json.loads(control_path.read_text())
assert len(control['files_sha256']) == 140
assert control['files_sha256']['xlang3.exe'] == paired['binaries_sha256']['control']['exe']
assert control['files_sha256']['xlang3_runtime.dll'] == paired['binaries_sha256']['control']['dll']
report = root / 'doc/performance/immutable-key-hash-checkpoint-20261007.md'
assert not report.exists()
text = """# Completed immutable-key hash caches

Exact immutable bytes now retain their native hash. Completed tuples cache hashes
only when every member is callback-free and immutable: None, bool, immediate int,
exact string/bytes/BigInt, or an eligible nested tuple. The native and runtime hash
paths share only this proven intrinsic result. User objects, classes, floats,
memoryviews and other unsupported members retain their existing hash path;
failures and Python callbacks are not cached. This is a narrower cache than
CPython 3.14.7's tuple cache for arbitrary Python objects, and does not claim to
close that existing conformance gap.

The gain comes from avoiding repeated component hashing in dict read/modify/write
and callback lookups. Hash values and the existing mixing algorithm are unchanged.
Atomic cache slots permit concurrent immutable readers. The tuple freelist resets
old caches before reuse. Reserved tuples and marshal placeholders remain uncached
until complete; VM/IR/graph builders explicitly finish construction, and graph
edge clearing invalidates the cache. Native bytes construction invalidates its
cache before requesting the mutable construction pointer. Published bytes and
tuples remain immutable. Code comments document these guards and lifecycle rules.

Counter, BPE and all pure-Python libraries remain Python. This generic runtime
optimization does not translate them into C++ or reuse CPython native modules.

## Validation

The candidate passed **373 core fixtures**, 11 compatibility sections, three
expected-failure checks, eight C++/SDK/graph checks, and the unchanged fixed
Release gate: 11 cases, 21 paired repeats, five warmups and 10% tolerance.
New C++ checks cover actual cache eligibility, nested hashes, freelist reuse,
partial builders, invalidation/failures, native-versus-runtime identity fallback,
and simultaneous read-only hashing. The Python fixture checks stable hashes,
numeric-key equality, unhashable members, recycling, frozen bytes, marshal/pickle
reconstruction, Python callbacks and tracing.

The accepted control preserves 140 Release files. Build/run paths remain
build-repro/main-verify-20261006/Release, and comparison Python is 3.14.7.

""" + official_text + """

Official BPE keeps the original source, vocabulary, input data, fast mode, shared
hook/dependency site and 1,800-second cap. Warmups/calibration are excluded from
scoring. This affected case does not replace the complete 97-case comparison or
establish a whole-suite win against CPython. The official GC traversal score is
still withheld; its fixed-gate row is only a baseline regression check.

## Paired diagnostics

Seven alternating control/candidate process pairs retain all **1,834 samples**
and **31 rows**. Hash/callback probes use five samples and dictionary update
scaling uses three; every row has a warmup and checks its outputs. Ratios above
1× mean faster than preceding XLang3, not CPython. The separate 80-sample baseline
includes Python loop/assertion overhead and is not an isolated native hash score.

All slower controls remain visible, including direct constant calls and a fresh
tuple hashed only once. A reused hash can pay back its caching cost; a fresh key
with one hash need not improve. No leaf ratio is presented as a whole-suite gain.

| Probe | Case | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
""" + '\n'.join(exported['table']) + """

## Evidence

- [Terminal validation](data/immutable-key-hash-validation-20261007.json)
- [Fixed gate](data/release-immutable-key-hash-fixed-gate-20261007.json)
- [Original official log](data/immutable-key-hash-validation-20261007-official-bpe.log)
- [Official comparison](data/immutable-key-hash-bpe-vs-cpython3147-20261007.json)
- [Initial CPython/XLang3 observations](data/immutable-key-hash-baseline-20261007.json)
- [Original paired results](data/immutable-key-hash-paired-20261007.json)
- [All samples](data/immutable-key-hash-paired-samples-20261007.csv)
- [All summary rows](data/immutable-key-hash-paired-summary-20261007.csv)
- [Compiler source provenance](data/immutable-key-hash-source-provenance-20261007.json)
- [Archived scripts and compiler inputs](data/immutable-key-hash-20261007-sources/manifest.json)
- [Preserved control](data/immutable-key-hash-preserved-control-20261007.json)
- [Preceding checkpoint](dict-missing-special-lookup-checkpoint-20261007.md)

Reference implementation: [CPython 3.14.7 tupleobject.c](https://github.com/python/cpython/blob/v3.14.7/Objects/tupleobject.c), tuple_hash and tuple_alloc.
"""
report.write_bytes(text.encode('utf-8'))
for target in re.findall(r'\]\(([^)]+)\)', text):
    assert target.startswith('https:') or (report.parent / target).exists(), target
evidence = [record_path, gate_path, baseline_path, paired_path, exported['sample_path'],
            exported['summary_path'], comparison_path, source_path, control_path, report]
evidence += [data / phase['log'] for phase in record['phases']]
evidence += [path for path in archive.rglob('*') if path.is_file()]
if official_path.exists():
    evidence.append(official_path)
inventory = {'status': 'terminal', 'raw_sample_count': 1834,
             'files': {str(path.relative_to(root)).replace('\\', '/'): digest(path) for path in evidence}}
(data / (prefix + '-evidence-20261007.json')).write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Archived and verified', len(evidence), 'terminal evidence files')
