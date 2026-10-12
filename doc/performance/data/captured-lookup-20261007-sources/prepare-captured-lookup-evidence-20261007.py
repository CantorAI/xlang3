"""Prepare terminal verification/reporting tools while measurement is live."""
from pathlib import Path

scratch = Path.cwd() / 'scratch/performance'
code = (scratch / 'finalize-immutable-key-hash-evidence-20261007.py').read_text(encoding='utf-8')
code = code.replace("prefix = 'immutable-key-hash'", "prefix = 'captured-lookup'")
code = code.replace("prefix + '-validation-20261007.json'", "prefix + '-validation-r2-20261007.json'")
code = code.replace("'core': 373", "'core': 374")
code = code.replace('export-immutable-key-hash-pairs-20261007.py', 'export-captured-lookup-pairs-20261007.py')
start, end = code.index('baseline_path ='), code.index('paired_path =')
code = code[:start] + '''failed_path = data / 'captured-lookup-validation-20261007.json'
failed = json.loads(failed_path.read_text(encoding='utf-8'))
assert failed['status'] == 'failed_cpp'
assert failed['candidate_binary_sha256'] == record['candidate_binary_sha256']
for phase in failed['phases']:
    assert digest(data / phase['log']) == phase['sha256']
''' + code[end:]
code = code.replace("prefix + '-validation-20261007-official-bpe.log'", "prefix + '-validation-r2-20261007-official-bpe.log'")
code = code.replace("data / 'pyperformance-dict-missing-special-lookup-bpe-fast-20261007.json'", "data / 'pyperformance-immutable-key-hash-bpe-fast-20261007.json'")
start, end = code.index('scripts = ['), code.index('compiled = archive')
code = code[:start] + '''scripts = ['preserve-captured-lookup-control-20261007.py', 'compare-captured-lookup-trial-20261007.py',
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
''' + code[end:]
code = code.replace('controls/dict-missing-special-lookup-checkpoint-20261007', 'controls/immutable-key-hash-checkpoint-20261007')
start, end = code.index('report = root /'), code.index('report.write_bytes')
code = code[:start] + '''report = root / 'doc/performance/captured-lookup-checkpoint-20261007.md'
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
""" + '\\n'.join(exported['table']) + """

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
''' + code[end:]
code = code.replace('record_path, gate_path, baseline_path, paired_path', 'record_path, gate_path, failed_path, paired_path')
code = code.replace("evidence += [data / phase['log'] for phase in record['phases']]", "evidence += [data / phase['log'] for phase in record['phases']]\nevidence += [data / phase['log'] for phase in failed['phases']]")
target = scratch / 'finalize-captured-lookup-evidence-20261007.py'
assert not target.exists()
compile(code, str(target), 'exec')
target.write_text(code, encoding='utf-8')
stage = (scratch / 'stage-immutable-key-hash-checkpoint-20261007.py').read_text(encoding='utf-8')
stage = stage.replace('immutable-key-hash', 'captured-lookup')
stage = stage.replace('captured-lookup-validation-20261007.json', 'captured-lookup-validation-r2-20261007.json')
stage_target = scratch / 'stage-captured-lookup-checkpoint-20261007.py'
assert not stage_target.exists()
compile(stage, str(stage_target), 'exec')
stage_target.write_text(stage, encoding='utf-8')
print('Prepared finalizer/report and exact-file stager; not executed')
