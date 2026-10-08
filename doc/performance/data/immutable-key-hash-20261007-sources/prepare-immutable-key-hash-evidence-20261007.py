"""Prepare terminal evidence tools without touching the measured candidate."""
from pathlib import Path

scratch = Path.cwd() / 'scratch/performance'
code = (scratch / 'finalize-dict-missing-special-evidence-20261007.py').read_text(encoding='utf-8')
code = code.replace("prefix = 'dict-missing-special-lookup'", "prefix = 'immutable-key-hash'")
code = code.replace("'core': 372", "'core': 373")
start, end = code.index('before_path ='), code.index('paired_path =')
code = code[:start] + '''import runpy
exported = runpy.run_path(str(root / 'scratch/performance/export-immutable-key-hash-pairs-20261007.py'))
baseline_path = data / 'immutable-key-hash-baseline-20261007.json'
baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
assert baseline['status'] == 'terminal'
assert all(item['exit_code'] == 0 for item in baseline['observations'])
''' + code[end:]
code = code.replace("data / 'pyperformance-native-trivial-callback-bpe-fast-20261007.json'", "data / 'pyperformance-dict-missing-special-lookup-bpe-fast-20261007.json'")
start, end = code.index('scripts = ['), code.index('compiled = archive')
code = code[:start] + '''scripts = ['immutable-key-hash-probe-20261007.py', 'observe-immutable-key-hash-baseline-20261007.py',
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
''' + code[end:]
code = code.replace('controls/native-trivial-callback-checkpoint-20261007', 'controls/dict-missing-special-lookup-checkpoint-20261007')
start, end = code.index("report = root /"), code.index('report.write_bytes')
code = code[:start] + '''report = root / 'doc/performance/immutable-key-hash-checkpoint-20261007.md'
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
""" + '\\n'.join(exported['table']) + """

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
''' + code[end:]
code = code.replace('record_path, gate_path, before_path, after_path, paired_path', 'record_path, gate_path, baseline_path, paired_path')
code = code.replace("'raw_sample_count': 770", "'raw_sample_count': 1834")
target = scratch / 'finalize-immutable-key-hash-evidence-20261007.py'
assert not target.exists()
compile(code, str(target), 'exec')
target.write_text(code, encoding='utf-8')
stage = (scratch / 'stage-dict-missing-special-lookup-checkpoint-20261007.py').read_text(encoding='utf-8')
stage = stage.replace('dict-missing-special-lookup', 'immutable-key-hash').replace('770', '1834')
stage_target = scratch / 'stage-immutable-key-hash-checkpoint-20261007.py'
assert not stage_target.exists()
compile(stage, str(stage_target), 'exec')
stage_target.write_text(stage, encoding='utf-8')
print('Prepared terminal finalizer/report and exact-file stager; not executed')
