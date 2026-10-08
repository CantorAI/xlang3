from pathlib import Path

root = Path.cwd()
source = root / 'scratch/performance/validate-dict-native-index-final-20261007.py'
target = root / 'scratch/performance/validate-inherited-subscript-cache-20261007.py'
assert not target.exists()
text = source.read_text(encoding='utf-8')
replacements = (
    ('Validate the index and finalizer checkpoint once', 'Validate the inherited subscript cache once'),
    ('dict-native-index-final-validation-20261007', 'inherited-subscript-cache-validation-20261007'),
    ('release-dict-native-index-final-fixed-gate-20261007', 'release-inherited-subscript-cache-fixed-gate-20261007'),
    ('pyperformance-dict-native-index-final-bpe-fast-20261007', 'pyperformance-inherited-subscript-cache-bpe-fast-20261007'),
    ("'--case-timeout', '1200'", "'--case-timeout', '1800'"),
    ('1260, required=False', '1860, required=False'),
    ("sources = ('src/runtime/mapping.cpp', 'src/internal/xlang3/mapping.h',\n           'tests/fixtures/core/dict_intrinsic_write_index.py',\n           'tests/cpp/mapping_iterator_ownership_cases.h')",
     "sources = ('src/runtime/object_model.cpp', 'src/executor/xlang_vm/ops/xlang_vm_ops_containers.h',\n           'tests/fixtures/core/inherited_subscript_cache.py',\n           'tests/fixtures/expected/inherited_subscript_cache.out', 'tests/run_fixtures.py')"),
    ("gate = data / 'release-inherited-subscript-cache-fixed-gate-20261007.json'",
     "phase('paired-dispatch', [sys.executable, 'scratch/performance/compare-inherited-subscript-trial-20261007.py'], 180)\n"
     "record['paired_dispatch'] = 'inherited-subscript-cache-paired-20261007.json'\n"
     "gate = data / 'release-inherited-subscript-cache-fixed-gate-20261007.json'"),
    ("record['compatibility_hook_sha256'] = digest(hook)",
     "record['compatibility_hook_sha256'] = digest(hook)\nsave()"),
)
for before, after in replacements:
    assert text.count(before) == 1, before
    text = text.replace(before, after)
target.write_text(text, encoding='utf-8')
print('Prepared correctness, paired diagnostics, fixed gate and official BPE at 1800s cap')
