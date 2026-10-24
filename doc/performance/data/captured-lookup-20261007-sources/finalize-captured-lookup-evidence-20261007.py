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
prefix = 'captured-lookup'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
record_path = data / (prefix + '-validation-r2-20261007.json')
record = json.loads(record_path.read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
assert record['fixture_counts'] == {'core': 374, 'compatibility_sections': 11, 'expected_failures': 3}
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
exported = runpy.run_path(str(root / 'scratch/performance/export-captured-lookup-pairs-20261007.py'))
failed_path = data / 'captured-lookup-validation-20261007.json'
failed = json.loads(failed_path.read_text(encoding='utf-8'))
assert failed['status'] == 'failed_cpp'
assert failed['candidate_binary_sha256'] == record['candidate_binary_sha256']
for phase in failed['phases']:
    assert digest(data / phase['log']) == phase['sha256']
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
    for label, path in (('cpython3147', reference), ('previous_xlang3', data / 'pyperformance-immutable-key-hash-bpe-fast-20261007.json'), ('candidate', official_path)):
        samples = values(path)
        comparison[label] = {'json': path.name, 'sha256': digest(path), 'values_seconds': samples,
                             'mean_seconds': statistics.mean(samples), 'sample_standard_deviation_seconds': statistics.stdev(samples)}
    current, python, previous = (comparison[label]['mean_seconds'] for label in ('candidate', 'cpython3147', 'previous_xlang3'))
    comparison['cpython_time_over_candidate_time_speed'] = python / current
    comparison['candidate_time_over_cpython_time'] = current / python
    comparison['previous_xlang3_time_over_candidate_time_speedup'] = previous / current
    comparison['candidate_pyperf_instability_warning'] = 'WARNING:' in (data / (prefix + '-validation-r2-20261007-official-bpe.log')).read_text()
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
scripts = ['preserve-captured-lookup-control-20261007.py', 'compare-captured-lookup-trial-20261007.py',
           'prepare-captured-lookup-validation-20261007.py', 'validate-captured-lookup-20261007.py',
           'prepare-captured-lookup-r2-20261007.py', 'validate-captured-lookup-r2-20261007.py',
           'export-captured-lookup-pairs-20261007.py', 'export-immutable-key-hash-pairs-20261007.py',
           'immutable-key-hash-probe-20261007.py', 'dict-composite-scaling-probe-20261007.py',
           'native-trivial-callback-probe-20261007.py', 'dict-callback-boundary-probe-20261007.py',
           'prepare-captured-lookup-evidence-20261007.py', 'finalize-captured-lookup-evidence-20261007.py',
           'stage-captured-lookup-checkpoint-20261007.py', 'prepare-bpe-callback-ir-20261007.py',
           'observe-bpe-callback-ir-20261007.py', 'bpe-callback-ir-source-20261007.py',
           'bpe-callback-ir-source-20261007.json', 'bpe-callback-ir-20261007.log',
           'captured-lookup-cpp-before-fixture-repair-20261007.h',
           'captured-lookup-python-before-tuple-coverage-20261007.py']
for name in scripts:
    source, target = root / 'scratch/performance' / name, archive / name
    target.write_bytes(source.read_bytes())
    manifest['files'][name] = digest(target)
    assert digest(source) == digest(target)
for observed in paired['probes']:
    assert observed['probe_sha256'] == manifest['files'][observed['filename']]
assert record['scaling_probe_sha256'] == manifest['files']['dict-composite-scaling-probe-20261007.py']
assert failed['source_sha256']['tests/cpp/mapping_iterator_ownership_cases.h'] == manifest['files']['captured-lookup-cpp-before-fixture-repair-20261007.h']
assert failed['source_sha256']['tests/fixtures/core/native_captured_lookup.py'] == manifest['files']['captured-lookup-python-before-tuple-coverage-20261007.py']
ir_record = json.loads((archive / 'bpe-callback-ir-source-20261007.json').read_text(encoding='utf-8'))
assert ir_record['exit_code'] == 0 and ir_record['original_sha256'] == benchmark_sha
assert manifest['files']['bpe-callback-ir-source-20261007.py'] == ir_record['derived_sha256']
assert manifest['files']['bpe-callback-ir-20261007.log'] == ir_record['log_sha256']
for name, sha in ir_record['ir_files_sha256'].items():
    assert digest(root / name) == sha
    target = archive / Path(name).name
    target.write_bytes((root / name).read_bytes())
    manifest['files'][target.name] = sha
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
control_path.write_bytes((root / 'build-repro/controls/immutable-key-hash-checkpoint-20261007/preserved-release-provenance.json').read_bytes())
control = json.loads(control_path.read_text())
assert len(control['files_sha256']) == 140
assert control['files_sha256']['xlang3.exe'] == paired['binaries_sha256']['control']['exe']
assert control['files_sha256']['xlang3_runtime.dll'] == paired['binaries_sha256']['control']['dll']
report = root / 'doc/performance/captured-lookup-checkpoint-20261007.md'
assert not report.exists()
text = """# Native entry for captured dictionary lookup functions

The original BPE key callback compiles to five IR instructions: LoadFree stats,
LoadLocal x, GetItem, Return, and implicit ReturnConst None. Its names already
bind by index. Native callbacks now recognize that generic current IR shape and
resolve the live captured cell directly, avoiding repeated Interpreter/frame
creation only for a proven nonfallible dictionary hit. The original Python
function and Counter library remain authoritative; no library is translated
into C++ and no CPython native module is reused.

The lookup requires an already valid intrinsic hash index, a callback-free query
and key set, and exact native dict storage semantics. Dict subclasses also
require their currently resolved __getitem__ to have the registered canonical
callback and fast adapter, ordinary descriptor binding and no contextual user
data. Display names alone cannot authorize this path. Invalid indices, misses,
overrides, custom hash/equality, descriptors and fallible cases use the original
Python call without speculative user-code execution or replay.

The analyzer checks current code/signature on entry, so code replacement is
observed without a persistent body cache. Generators, async/coroutine functions,
unsupported signatures and own cell locals are excluded. Debugging, tracing,
profiling, function monitoring and pending asynchronous work retain normal
entry. Receiver/key remain borrowed during the callback-free query; the result
is owned before old-output release, and no dictionary/key storage is touched
after a finalizer can run. Comments explain the cost avoided and these guards.

## Validation and retained failure

The corrected candidate passed **374 core fixtures**, 11 compatibility sections,
three expected-failure checks, eight C++/SDK/graph checks and the unchanged fixed
Release gate: 11 cases, 21 paired repeats, five warmups and 10% tolerance.
The seven-part fixture uses tuple-key hits to exercise eligible general-index
lookups, and covers live cells, overrides/misses, key protocols/descriptors,
errors/tracebacks, code replacement, handled exceptions, trace/profile and
monitoring. C++ checks cover eligibility, unchanged miss output, invalid indices,
unproven query types, canonical getter identity, finalizer mutation and receiver/
output aliasing.

The first full validation passed Python fixtures but failed two C++ assertions:
the lifetime tests used an integer-only dictionary, which uses its separate
scalar index and is intentionally ineligible for this shortcut. The corrected
tests use the tuple-key general index. The original source snapshot and failure
logs remain. An unrelated xMind build was observed and left alone; the mutation
guard delayed engine editing until that build was terminal.

The accepted control preserves 140 Release files. Build/run paths remain
build-repro/main-verify-20261006/Release and comparison Python is 3.14.7.

""" + official_text + """

Official BPE retains the original source, vocabulary, input, fast mode, shared
hook/dependency site and 1,800-second cap. Warmups/calibration are excluded from
scoring. The separate pre-change IR diagnostic disabled only the __main__ runner
and preserved the trainer source bytes; it was unscored. This case does not
replace the complete 97-case comparison or establish a whole-suite CPython win.
The official GC traversal score remains withheld.

## Paired diagnostics

Seven alternating control/candidate process pairs retain all **1,834 samples**
and **31 rows**. Hash/callback probes use five samples; update scaling uses three.
Every row has a warmup and checked outputs. Ratios above 1× mean faster than the
preceding XLang3, not CPython. All neutral and slower controls remain. Native-vs-
direct loop differences include comparisons/ownership and are not isolated
frame-creation measurements.

| Probe | Case | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
""" + '\n'.join(exported['table']) + """

## Evidence

- [Corrected terminal validation](data/captured-lookup-validation-r2-20261007.json)
- [Fixed gate](data/release-captured-lookup-r2-fixed-gate-20261007.json)
- [Original official log](data/captured-lookup-validation-r2-20261007-official-bpe.log)
- [Official comparison](data/captured-lookup-bpe-vs-cpython3147-20261007.json)
- [First failed validation](data/captured-lookup-validation-20261007.json)
- [Original paired results](data/captured-lookup-paired-20261007.json)
- [All samples](data/captured-lookup-paired-samples-20261007.csv)
- [All summary rows](data/captured-lookup-paired-summary-20261007.csv)
- [Compiler source provenance](data/captured-lookup-source-provenance-20261007.json)
- [Archived scripts, original IR and source inputs](data/captured-lookup-20261007-sources/manifest.json)
- [Preserved control](data/captured-lookup-preserved-control-20261007.json)
- [Preceding checkpoint](immutable-key-hash-checkpoint-20261007.md)
"""
report.write_bytes(text.encode('utf-8'))
for target in re.findall(r'\]\(([^)]+)\)', text):
    assert target.startswith('https:') or (report.parent / target).exists(), target
evidence = [record_path, gate_path, failed_path, paired_path, exported['sample_path'],
            exported['summary_path'], comparison_path, source_path, control_path, report]
evidence += [data / phase['log'] for phase in record['phases']]
evidence += [data / phase['log'] for phase in failed['phases']]
evidence += [path for path in archive.rglob('*') if path.is_file()]
if official_path.exists():
    evidence.append(official_path)
inventory = {'status': 'terminal', 'raw_sample_count': 1834,
             'files': {str(path.relative_to(root)).replace('\\', '/'): digest(path) for path in evidence}}
(data / (prefix + '-evidence-20261007.json')).write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Archived and verified', len(evidence), 'terminal evidence files')
