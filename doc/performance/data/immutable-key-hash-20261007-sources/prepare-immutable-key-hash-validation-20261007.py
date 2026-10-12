"""Prepare serial validation; unchanged full gate and official workload."""
from pathlib import Path

scratch = Path.cwd() / 'scratch/performance'
source = scratch / 'validate-dict-missing-special-lookup-20261007.py'
code = source.read_text(encoding='utf-8')
code = code.replace("prefix = 'dict-missing-special-lookup-validation-20261007'", "prefix = 'immutable-key-hash-validation-20261007'")
start, end = code.index('sources = ('), code.index('record =')
code = code[:start] + '''sources = ('src/internal/xlang3/value.h',
           'src/runtime/value.cpp', 'src/runtime/value_hash.cpp',
           'src/executor/xlang_vm/ops/xlang_vm_ops_containers.h',
           'src/ir/codec.cpp', 'src/serialize/value_graph_reader.cpp',
           'src/serialize/value_graph_lifetime.cpp',
           'src/runtime/modules/system/marshal_module.cpp',
           'tests/cpp/mapping_iterator_ownership_cases.h', 'tests/run_fixtures.py',
           'tests/fixtures/core/immutable_key_hash.py',
           'tests/fixtures/expected/immutable_key_hash.out')
''' + code[end:]
code = code.replace("phase('protocol', [sys.executable, 'scratch/performance/verify-dict-missing-special-result-20261007.py'], 60)\n", '')
code = code.replace('compare-dict-missing-special-trial-20261007.py', 'compare-immutable-key-hash-trial-20261007.py')
code = code.replace('dict-missing-special-lookup-paired-20261007.json', 'immutable-key-hash-paired-20261007.json')
code = code.replace('release-dict-missing-special-lookup-fixed-gate-20261007.json', 'release-immutable-key-hash-fixed-gate-20261007.json')
code = code.replace('pyperformance-dict-missing-special-lookup-bpe-fast-20261007.json', 'pyperformance-immutable-key-hash-bpe-fast-20261007.json')
code = code.replace("'scratch/performance/compare-immutable-key-hash-trial-20261007.py'], 180)", "'scratch/performance/compare-immutable-key-hash-trial-20261007.py'], 300)")
target = scratch / 'validate-immutable-key-hash-20261007.py'
assert not target.exists()
compile(code, str(target), 'exec')
target.write_text(code, encoding='utf-8')
print('Prepared full correctness, paired diagnostics, unchanged fixed gate, official BPE')
