"""Prepare unchanged paired controls and serial full validation."""
from pathlib import Path

scratch = Path.cwd() / 'scratch/performance'
pair = (scratch / 'compare-immutable-key-hash-trial-20261007.py').read_text(encoding='utf-8')
pair = pair.replace('immutable-key-hash-paired-20261007.json', 'captured-lookup-paired-20261007.json')
pair = pair.replace('controls/dict-missing-special-lookup-checkpoint-20261007', 'controls/immutable-key-hash-checkpoint-20261007')
pair_target = scratch / 'compare-captured-lookup-trial-20261007.py'
assert not pair_target.exists()
compile(pair, str(pair_target), 'exec')
pair_target.write_text(pair, encoding='utf-8')
code = (scratch / 'validate-immutable-key-hash-20261007.py').read_text(encoding='utf-8')
code = code.replace("prefix = 'immutable-key-hash-validation-20261007'", "prefix = 'captured-lookup-validation-20261007'")
start, end = code.index('sources = ('), code.index('record =')
code = code[:start] + '''sources = ('src/internal/xlang3/builtin_methods.h', 'src/internal/xlang3/mapping.h',
           'src/runtime/methods/dict_methods.cpp', 'src/runtime/mapping.cpp',
           'src/executor/xlang_vm/xlang_vm_inline_call.h', 'src/runtime/functional_iterators.cpp',
           'tests/cpp/mapping_iterator_ownership_cases.h', 'tests/run_fixtures.py',
           'tests/fixtures/core/native_captured_lookup.py',
           'tests/fixtures/expected/native_captured_lookup.out')
''' + code[end:]
code = code.replace('compare-immutable-key-hash-trial-20261007.py', 'compare-captured-lookup-trial-20261007.py')
code = code.replace('immutable-key-hash-paired-20261007.json', 'captured-lookup-paired-20261007.json')
code = code.replace('release-immutable-key-hash-fixed-gate-20261007.json', 'release-captured-lookup-fixed-gate-20261007.json')
code = code.replace('pyperformance-immutable-key-hash-bpe-fast-20261007.json', 'pyperformance-captured-lookup-bpe-fast-20261007.json')
target = scratch / 'validate-captured-lookup-20261007.py'
assert not target.exists()
compile(code, str(target), 'exec')
target.write_text(code, encoding='utf-8')
print('Prepared original paired workloads, full correctness, fixed gate and official BPE')
