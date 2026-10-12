"""Finalize terminal checked evidence without running any workload."""
import hashlib
import json
from pathlib import Path
import runpy
import statistics
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
record_path = data / 'native-trivial-callback-validation-20261007.json'
record = json.loads(record_path.read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
assert record['fixture_counts']['core'] == 371
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
for name, sha in record['source_sha256'].items():
    assert digest(root / name) == sha
for phase in record['phases']:
    assert digest(data / phase['log']) == phase['sha256']
    if phase['name'] != 'official-bpe':
        assert phase['exit_code'] == 0
gate_path = data / record['fixed_gate']['output']
assert digest(gate_path) == record['fixed_gate']['sha256']
gate = json.loads(gate_path.read_text())
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
exported = runpy.run_path(str(root / 'scratch/performance/export-native-trivial-callback-pairs-20261007.py'))
paired_path = data / record['paired_callbacks']
paired = json.loads(paired_path.read_text())
assert paired['binaries_sha256']['candidate'] == record['candidate_binary_sha256']
archive = data / 'native-trivial-callback-20261007-sources'
assert not archive.exists()
archive.mkdir()
scripts = [
    'native-trivial-callback-probe-20261007.py',
    'observe-native-trivial-callback-baseline-20261007.py',
    'preserve-native-trivial-callback-control-20261007.py',
    'native-trivial-callback-fixture-next-20261007.py',
    'dict-callback-boundary-probe-20261007.py',
    'compare-native-trivial-callback-trial-20261007.py',
    'prepare-native-trivial-callback-validation-20261007.py',
    'validate-native-trivial-callback-20261007.py',
    'export-native-trivial-callback-pairs-20261007.py',
    'finalize-native-trivial-callback-evidence-20261007.py',
    'prepare-native-trivial-callback-staging-20261007.py',
    'stage-native-trivial-callback-checkpoint-20261007.py',
]
manifest = {'warning': 'Historical one-off scripts; do not replay unchanged.', 'files': {}}
for name in scripts:
    source, target = root / 'scratch/performance' / name, archive / name
    target.write_bytes(source.read_bytes())
    assert digest(target) == digest(source)
    manifest['files'][name] = digest(target)
for observed in paired['probes']:
    filename = 'native-trivial-callback-probe-20261007.py' if observed['name'] == 'trivial' else 'dict-callback-boundary-probe-20261007.py'
    assert observed['probe_sha256'] == manifest['files'][filename]
compiled = archive / 'compiled-sources'
compiled.mkdir()
source_info = {'compiler_input_sha256': record['source_sha256'], 'repository_lf_sha256': {},
               'note': 'Archived compiler input bytes are exact; repository hashes refer to Git LF-normalized equivalents.'}
for name, sha in record['source_sha256'].items():
    raw = (root / name).read_bytes()
    target = compiled / Path(name).name
    assert not target.exists()
    target.write_bytes(raw)
    manifest['files']['compiled-sources/' + target.name] = sha
    source_info['repository_lf_sha256'][name] = hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest()
(archive / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
source_path = data / 'native-trivial-callback-source-provenance-20261007.json'
source_path.write_text(json.dumps(source_info, indent=2) + '\n', encoding='utf-8')
control_source = root / 'build-repro/controls/minmax-streaming-checkpoint-20261007/preserved-release-provenance.json'
control_path = data / 'native-trivial-callback-preserved-control-20261007.json'
control_path.write_bytes(control_source.read_bytes())
ir_source = root / 'scratch/performance/counter-trivial-ir-20261007/__init__.ir.txt'
ir_text = ir_source.read_text()
qualname = ir_text.index('  qualname: Counter.__missing__')
begin = ir_text.rfind('\nfunction #', 0, qualname)
end = ir_text.index('\nfunction #', qualname)
ir_path = data / 'native-trivial-callback-counter-missing-20261007.ir.txt'
ir_path.write_bytes(ir_text[begin + 1:end].encode('utf-8'))
assert 'ReturnConst' in ir_path.read_text() and 'free_vars:\n' in ir_path.read_text()

def values(path):
    suite = json.loads(path.read_text())
    cases = [case for case in suite['benchmarks'] if case.get('metadata', {}).get('name', suite.get('metadata', {}).get('name')) == 'bpe_tokeniser']
    assert len(cases) == 1
    result = [value for run in cases[0]['runs'] for value in run.get('values', [])]
    assert len(result) == 20
    return result

reference = data / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'
reference_provenance = reference.with_name(reference.stem + '-provenance.json')
reference_info = json.loads(reference_provenance.read_text())
assert reference_info['runtime_version'] == '3.14.7' and reference_info['exit_code'] == 0
assert reference_info['compatibility_hook_sha256'] == record['compatibility_hook_sha256']
previous = data / 'pyperformance-minmax-streaming-bpe-fast-20261007.json'
official = record['official_bpe']
official_path = data / official['output']
comparison = {'scope': 'Official BPE descriptive comparison; references reused, not alternating paired official runs',
              'candidate_exit_code': official['exit_code'], 'candidate_binary_sha256': record['candidate_binary_sha256']}
official_text = 'Official BPE failed or timed out and remains unscored; keep the original failure log.'
if official['exit_code'] == 0:
    assert digest(official_path) == official['sha256']
    comparison['status'] = 'completed'
    for label, path in (('cpython3147', reference), ('previous_xlang3', previous), ('candidate', official_path)):
        samples = values(path)
        comparison[label] = {'json': path.name, 'sha256': digest(path), 'values_seconds': samples,
                             'mean_seconds': statistics.mean(samples), 'sample_standard_deviation_seconds': statistics.stdev(samples)}
    current, python, previous_mean = (comparison[label]['mean_seconds'] for label in ('candidate', 'cpython3147', 'previous_xlang3'))
    comparison['cpython_time_over_candidate_time_speed'] = python / current
    comparison['candidate_time_over_cpython_time'] = current / python
    comparison['previous_xlang3_time_over_candidate_time_speedup'] = previous_mean / current
    comparison['candidate_pyperf_instability_warning'] = 'WARNING:' in (data / 'native-trivial-callback-validation-20261007-official-bpe.log').read_text()
    official_text = (f'Official means: CPython 3.14.7 **{python:.3f} s**, preceding XLang3 **{previous_mean:.3f} s**, '
                     f'candidate **{current:.3f} s**. Candidate speed relative to CPython is **{python/current:.3f}×** '
                     f'(**{current/python:.2f}× longer runtime**); nominal speedup over preceding XLang3 is '
                     f'**{previous_mean/current:.3f}×**. Candidate sample SD is '
                     f'**{comparison["candidate"]["sample_standard_deviation_seconds"]:.3f} s**. '
                     f'Pyperf instability warning: **{comparison["candidate_pyperf_instability_warning"]}**. '
                     'These reused references do not establish an alternating-pair official significance claim.')
else:
    comparison['status'] = 'candidate_failed_unscored'
comparison_path = data / 'native-trivial-callback-bpe-vs-cpython3147-20261007.json'
comparison_path.write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
report = root / 'doc/performance/native-trivial-callback-checkpoint-20261007.md'
assert not report.exists()
text = '''# Generic trivial Python functions at native callback entry

The ordinary VM call path already avoids a Python frame for eligible functions
that return a constant or argument. Native runtime_call_callable now reuses that
same IR analyzer/executor and shares its observability guard with the ordinary
VM path. This removes repeated Interpreter/frame creation for eligible native
callbacks. Python Counter and all other pure-Python libraries remain Python.
There is no native translation of their library implementation.

The analyzer inspects current code and signature on each entry. It requires
exact positional binding and a capture-free trivial body. Native entry also
excludes generators, async functions and coroutines. Debug stepping, tracing,
profiling, function monitoring and pending asynchronous work retain normal
entry. The asynchronous guard prevents a long native loop from starving queued
work when no surrounding VM safepoint is running. Unsupported signatures,
defaults requiring binding, nontrivial bodies and fallible operations use the
original Interpreter path. Comments explain the cost avoided and guards.

Counter.__missing__ was compiled from the CPython 3.14.7 library and inspected
in a saved IR dump: two positional parameters, no captures/cells, non-generator,
non-async, non-coroutine, and ReturnConst. The fixture verifies identity, ties,
live defaults, invalid arguments, closures, generators/coroutines, overrides,
static/classmethod binding, code replacement, error traceback, caller handled
exception state, trace/profile events and local monitoring.

## Validation and evidence limits

The candidate passed **371 core fixtures**, 11 compatibility sections, three
expected-failure checks, and all eight C++/SDK/graph checks. The unchanged fixed
gate passed all 11 cases with 21 paired repeats, five warmups and a 10% threshold.
The accepted control preserves 140 Release files. Build/run paths remain
`build-repro/main-verify-20261006/Release`; comparison Python is 3.14.7.

''' + official_text + '''

Official BPE uses the original workload, fast mode, common hook/dependency site,
and the unchanged 1,800-second observation cap. Warmups/calibration are excluded
from scoring. This affected case does not replace the separate complete 97-case
comparison or establish a whole-suite win over CPython.

## Paired diagnostics

Each probe uses seven alternating control/candidate process pairs, one warmup
and five samples per row, with checked outputs and unchanged operation counts.
The median of paired control-time/candidate-time ratios is shown below. Higher
than 1× is faster than the preceding XLang3, not CPython. Distinct loop shapes
are diagnostic only. All **770 raw samples** and all 11 summary rows remain.

Native constant-key callbacks and Counter missing reads improve in all seven
pairs. Ordinary nontrivial callback controls are roughly unchanged. The direct
constant-call control is roughly 3% slower in all seven pairs; that observation
is retained rather than excluded from the report. It is separate from the fixed
gate's pass, and small descriptive changes are not significance claims.

| Probe | Mapping | Path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | --- | ---: | ---: |
''' + '\n'.join(exported['table']) + '''

## Raw evidence

- [Terminal validation](data/native-trivial-callback-validation-20261007.json)
- [Fixed gate](data/release-native-trivial-callback-fixed-gate-20261007.json)
- [Original official log](data/native-trivial-callback-validation-20261007-official-bpe.log)
- [Official comparison / failure record](data/native-trivial-callback-bpe-vs-cpython3147-20261007.json)
- [Original paired results](data/native-trivial-callback-paired-20261007.json)
- [All samples](data/native-trivial-callback-paired-samples-20261007.csv)
- [All summary rows](data/native-trivial-callback-paired-summary-20261007.csv)
- [Initial CPython/native route observations](data/native-trivial-callback-baseline-20261007.json)
- [Counter missing IR](data/native-trivial-callback-counter-missing-20261007.ir.txt)
- [Compiler source provenance](data/native-trivial-callback-source-provenance-20261007.json)
- [Archived scripts and byte-exact compiler inputs](data/native-trivial-callback-20261007-sources/manifest.json)
- [Preserved control provenance](data/native-trivial-callback-preserved-control-20261007.json)
- [Preceding checkpoint](minmax-streaming-checkpoint-20261007.md)
'''
report.write_bytes(text.encode('utf-8'))
evidence = [record_path, paired_path, gate_path, exported['sample_path'], exported['summary_path'],
            source_path, control_path, ir_path, comparison_path, report,
            data / 'native-trivial-callback-baseline-20261007.json']
evidence += [data / phase['log'] for phase in record['phases']]
evidence += [path for path in archive.rglob('*') if path.is_file()]
if official_path.exists():
    evidence.append(official_path)
inventory = {'status': 'terminal', 'raw_sample_count': 770,
             'files': {str(path.relative_to(root)).replace('\\', '/'): digest(path) for path in evidence}}
(data / 'native-trivial-callback-evidence-20261007.json').write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Archived and verified', len(evidence), 'terminal evidence files')
