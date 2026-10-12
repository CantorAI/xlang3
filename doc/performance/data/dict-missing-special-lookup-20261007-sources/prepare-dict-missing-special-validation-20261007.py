"""Prepare a serial validation using unchanged workloads and measurement gates."""
from pathlib import Path

root = Path.cwd()
scratch = root / 'scratch/performance'
pair = (scratch / 'compare-native-trivial-callback-trial-20261007.py').read_text()
pair = pair.replace('native-trivial-callback-paired-20261007.json', 'dict-missing-special-lookup-paired-20261007.json')
pair = pair.replace('controls/minmax-streaming-checkpoint-20261007', 'controls/native-trivial-callback-checkpoint-20261007')
pair_target = scratch / 'compare-dict-missing-special-trial-20261007.py'
assert not pair_target.exists()
pair_target.write_text(pair, encoding='utf-8')
validation = (scratch / 'validate-native-trivial-callback-20261007.py').read_text()
validation = validation.replace("prefix = 'native-trivial-callback-validation-20261007'", "prefix = 'dict-missing-special-lookup-validation-20261007'")
start = validation.index("sources = (")
end = validation.index("record =", start)
validation = validation[:start] + '''sources = ('src/runtime/mapping.cpp',
           'src/internal/xlang3/mapping.h',
           'src/runtime/methods/dict_methods.cpp',
           'tests/fixtures/core/dict_missing_special_lookup.py',
           'tests/fixtures/expected/dict_missing_special_lookup.out',
           'tests/run_fixtures.py')
''' + validation[end:]
validation = validation.replace('compare-native-trivial-callback-trial-20261007.py', 'compare-dict-missing-special-trial-20261007.py')
validation = validation.replace('native-trivial-callback-paired-20261007.json', 'dict-missing-special-lookup-paired-20261007.json')
validation = validation.replace('release-native-trivial-callback-fixed-gate-20261007.json', 'release-dict-missing-special-lookup-fixed-gate-20261007.json')
validation = validation.replace('pyperformance-native-trivial-callback-bpe-fast-20261007.json', 'pyperformance-dict-missing-special-lookup-bpe-fast-20261007.json')
target = scratch / 'validate-dict-missing-special-lookup-20261007.py'
assert not target.exists()
target.write_text(validation, encoding='utf-8')
compile(pair, str(pair_target), 'exec')
compile(validation, str(target), 'exec')
print('Prepared sequential correctness, paired diagnostics, unchanged fixed gate and official BPE')
