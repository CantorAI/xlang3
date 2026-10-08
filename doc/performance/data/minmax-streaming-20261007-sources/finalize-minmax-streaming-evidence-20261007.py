"""Archive terminal min/max evidence; never launches a runtime workload."""
import hashlib
import json
from pathlib import Path
import runpy
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
record_path = data / 'minmax-streaming-validation-20261007.json'
record = json.loads(record_path.read_text())
assert record['status'] in ('validated', 'correctness_and_gate_passed_official_failed')
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert record['candidate_binary_sha256'] == {'exe': digest(candidate), 'dll': digest(candidate.with_name('xlang3_runtime.dll'))}
assert all(digest(root / name) == sha for name, sha in record['source_sha256'].items())
assert record['fixture_counts']['core'] == 370
for phase in record['phases']:
    assert digest(data / phase['log']) == phase['sha256']
    if phase['name'] != 'official-bpe':
        assert phase['exit_code'] == 0
gate_path = data / record['fixed_gate']['output']
assert digest(gate_path) == record['fixed_gate']['sha256']
gate = json.loads(gate_path.read_text())
assert gate['status'] == 'pass' and len(gate['cases']) == 11
assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
exported = runpy.run_path(str(root / 'scratch/performance/export-minmax-streaming-pairs-20261007.py'))
assert exported['sample_count'] == 420
paired_path = data / record['paired_callbacks']
paired = json.loads(paired_path.read_text())
assert paired['binaries_sha256']['candidate'] == record['candidate_binary_sha256']

archive = data / 'minmax-streaming-20261007-sources'
assert not archive.exists()
archive.mkdir()
scripts = [
    'prepare-minmax-streaming-trial-20261007.py',
    'apply-minmax-streaming-trial-20261007.py',
    'minmax-streaming-function-next-20261007.cpp',
    'minmax-streaming-fixture-next-20261007.py',
    'minmax-result-lifetime-probe-20261007.py',
    'observe-minmax-result-lifetime-20261007.py',
    'observe-minmax-global-delete-r1-20261007.py',
    'global-loop-delete-lifetime-probe-20261007.py',
    'dict-callback-boundary-probe-20261007.py',
    'compare-minmax-streaming-trial-20261007.py',
    'prepare-minmax-streaming-validation-20261007.py',
    'validate-minmax-streaming-20261007.py',
    'export-minmax-streaming-pairs-20261007.py',
    'finalize-minmax-streaming-evidence-20261007.py',
    'stage-minmax-streaming-checkpoint-20261007.py',
]
manifest = {'warning': 'Historical one-off scripts and prepared fragments; do not replay unchanged.', 'files': {}}
for name in scripts:
    source, target = root / 'scratch/performance' / name, archive / name
    target.write_bytes(source.read_bytes())
    assert digest(source) == digest(target)
    manifest['files'][name] = digest(target)
assert paired['probe_sha256'] == manifest['files']['dict-callback-boundary-probe-20261007.py']
compiled = archive / 'compiled-sources'
compiled.mkdir()
source_provenance = {'compiler_input_sha256': record['source_sha256'], 'repository_lf_sha256': {},
                     'note': 'Archived compiler inputs are byte-exact; repository equivalents differ only by Git newline normalization.'}
for name, sha in record['source_sha256'].items():
    raw = (root / name).read_bytes()
    target = compiled / Path(name).name
    assert not target.exists()
    target.write_bytes(raw)
    assert digest(target) == sha
    manifest['files']['compiled-sources/' + target.name] = sha
    source_provenance['repository_lf_sha256'][name] = hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest()
source_provenance_path = data / 'minmax-streaming-source-provenance-20261007.json'
source_provenance_path.write_text(json.dumps(source_provenance, indent=2) + '\n', encoding='utf-8')
(archive / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
control_source = root / 'build-repro/controls/inherited-subscript-checkpoint-20261007/preserved-release-provenance.json'
control_target = data / 'minmax-streaming-preserved-control-20261007.json'
control_target.write_bytes(control_source.read_bytes())

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
previous = data / 'pyperformance-inherited-subscript-cache-bpe-fast-20261007.json'
official = record['official_bpe']
official_path = data / official['output']
comparison = {'scope': 'Official BPE descriptive comparison; references reused, not alternating paired official runs',
              'candidate_exit_code': official['exit_code'], 'sources': {
                  'cpython3147': {'json': reference.name, 'sha256': digest(reference)},
                  'previous_xlang3': {'json': previous.name, 'sha256': digest(previous)}},
              'candidate_binary_sha256': record['candidate_binary_sha256']}
official_text = 'The official affected BPE run failed or timed out. It remains unscored; see the original log.'
if official['exit_code'] == 0:
    assert digest(official_path) == official['sha256']
    comparison['status'] = 'completed'
    for label, path in (('cpython3147', reference), ('previous_xlang3', previous), ('candidate', official_path)):
        samples = values(path)
        comparison[label] = {'json': path.name, 'sha256': digest(path), 'values_seconds': samples,
                             'mean_seconds': statistics.mean(samples),
                             'sample_standard_deviation_seconds': statistics.stdev(samples)}
    current, python, previous_mean = (comparison[label]['mean_seconds'] for label in ('candidate', 'cpython3147', 'previous_xlang3'))
    comparison['cpython_time_over_candidate_time_speed'] = python / current
    comparison['candidate_time_over_cpython_time'] = current / python
    comparison['previous_xlang3_time_over_candidate_time_speedup'] = previous_mean / current
    official_text = (f'Official means: CPython 3.14.7 **{python:.3f} s**, preceding XLang3 **{previous_mean:.3f} s**, '
                     f'candidate **{current:.3f} s**. Candidate speed relative to CPython is **{python/current:.3f}×** '
                     f'(**{current/python:.2f}× longer runtime**); the descriptive speedup over preceding XLang3 is '
                     f'**{previous_mean/current:.3f}×**. Candidate sample SD is '
                     f'**{comparison["candidate"]["sample_standard_deviation_seconds"]:.3f} s**. '
                     'Preserve the pyperf warning in the original log; this is not a statistical significance claim.')
else:
    comparison['status'] = 'candidate_failed_unscored'
comparison_path = data / 'minmax-streaming-bpe-vs-cpython3147-20261007.json'
comparison_path.write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
report = root / 'doc/performance/minmax-streaming-checkpoint-20261007.md'
assert not report.exists()
text = '''# Native min/max streaming and global lifetime checkpoint

Native min/max now advances one item, calls its key, compares and releases
discarded owners before fetching another item. The previous implementation
collected all items and all keys into two owning vectors before comparing.
That delayed callbacks and finalizers, hid list growth during key callbacks,
and fetched later items after an earlier callback should have raised.

The new path retains O(1) temporary owners and preserves first-item ties,
original result identity, defaults, positional/iterable forms, original
iteration/key/comparison/truth exceptions, and native owner release order.
Comments explain the allocation avoided and observable sequencing. Python
Counter and the BPE algorithm remain Python; min/max is a native CPython builtin.

## VM lifetime repair

The new finalizer test exposed an existing global-object lifetime defect in
both the preserved accepted Release and the first streaming trial. Local
objects finalized promptly, but module-level objects remained retained after
attribute access and deletion, including objects constructed without min/max.
The original before observations and failed intermediate observation remain.

Global/module deletion now publishes the missing binding and updated version
before releasing owners. At this cold deletion boundary it clears obsolete
expression/container registers, completed native-call scratch arguments and
matching owning global-cache values. Future and loop-carried registers remain
protected. Real Python aliases retain their ordinary namespace/container owners.
This adds no scan to ordinary VM instructions.

The fused LoadModuleAttr receiver register is written from the live module slot
before the instruction reads it. Its internal read was incorrectly classified
as requiring an old loop-carried value. Metadata now excludes that internal
read from loop-carried classification while retaining its linear last-use;
later external reads still establish real liveness. The minimal IR dump and
regression fixtures cover immediate loop finalization, aliases, dead native
container temporaries, absent bindings during reentrant finalizers, and min/max
release order. No threshold, workload or correctness assertion was weakened.

## Validation and measurements

The candidate passed **370 core fixtures**, 11 compatibility sections, the
three expected-failure checks, and all eight C++/SDK/graph checks. The unchanged
fixed gate passed all 11 cases at 21 paired repeats, five warmups and a 10%
threshold. The accepted control preserves 140 Release files. Build/run paths
remain `build-repro/main-verify-20261006/Release`; CPython is 3.14.7.

''' + official_text + '''

The official run uses the original BPE workload, fast settings, common
dependency site/hook and the same 1,800-second observation cap as the preceding
completed run. Calibration and warmup values are excluded from scoring.
References are reused from completed same-day runs, so nominal changes do not
establish a paired official engine gain. The last full 97-case comparison is
separate; this checkpoint does not establish a whole-suite win over CPython.

Seven alternating control/candidate diagnostic process pairs each used a
warmup and five samples for six paths. All checksums and lookup counts were
verified. Higher than 1× is faster than the preceding XLang3, not CPython.
These rows do not demonstrate a callback speed win: the Counter native callback
was roughly 3% slower, and every measured pair favored the control for that row.
All **420 raw samples** are retained.

| Mapping | Diagnostic path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
''' + '\n'.join(exported['table']) + '''

## Evidence

- [Terminal validation](data/minmax-streaming-validation-20261007.json)
- [Fixed gate](data/release-minmax-streaming-fixed-gate-20261007.json)
- [Original official BPE log](data/minmax-streaming-validation-20261007-official-bpe.log)
- [Official comparison / failure record](data/minmax-streaming-bpe-vs-cpython3147-20261007.json)
- [Original paired diagnostics](data/minmax-streaming-paired-callbacks-20261007.json)
- [All paired samples](data/minmax-streaming-paired-samples-20261007.csv)
- [Paired summary](data/minmax-streaming-paired-summary-20261007.csv)
- [Original streaming defect](data/minmax-streaming-fixture-before-20261007.json)
- [Independent before lifetime probe](data/minmax-result-lifetime-observations-20261007.json)
- [Intermediate incomplete lifetime repair](data/minmax-global-delete-r1-lifetime-observations-20261007.json)
- [Minimal loop IR before liveness repair](data/minmax-streaming-global-loop-before-20261007.ir.txt)
- [Compiler source provenance](data/minmax-streaming-source-provenance-20261007.json)
- [Archived scripts and exact compiler inputs](data/minmax-streaming-20261007-sources/manifest.json)
- [Preserved control provenance](data/minmax-streaming-preserved-control-20261007.json)
- [Preceding checkpoint](inherited-subscript-cache-checkpoint-20261007.md)
'''
report.write_bytes(text.encode('utf-8'))
evidence = [record_path, paired_path, gate_path, exported['sample_path'], exported['summary_path'],
            report, source_provenance_path, control_target, comparison_path]
evidence += [data / phase['log'] for phase in record['phases']]
evidence += [data / name for name in ('minmax-streaming-fixture-before-20261007.json',
                                     'minmax-result-lifetime-observations-20261007.json',
                                     'minmax-global-delete-r1-lifetime-observations-20261007.json')]
evidence += [path for path in archive.rglob('*') if path.is_file()]
ir_source = root / 'scratch/performance/global-loop-delete-ir-20261007/global-loop-delete-lifetime-probe-20261007.ir.txt'
ir_target = data / 'minmax-streaming-global-loop-before-20261007.ir.txt'
ir_target.write_bytes(ir_source.read_bytes())
evidence.append(ir_target)
if official_path.exists():
    evidence.append(official_path)
inventory = {'status': 'terminal', 'raw_sample_count': 420,
             'files': {str(path.relative_to(root)).replace('\\', '/'): digest(path) for path in evidence}}
(data / 'minmax-streaming-evidence-20261007.json').write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
print('Archived terminal evidence:', len(evidence), 'files')
