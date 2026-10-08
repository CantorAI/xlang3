from pathlib import Path
import ast

root = Path.cwd()
output = root / 'scratch/performance/validate-native-trivial-callback-20261007.py'
assert not output.exists()
text = (root / 'scratch/performance/validate-minmax-streaming-20261007.py').read_text()
text = text.replace('Validate native min/max streaming and global deletion once', 'Validate generic native trivial-function entry once')
text = text.replace('minmax-streaming', 'native-trivial-callback')
start = text.index('sources = (')
end = text.index('\nrecord = ', start)
text = text[:start] + '''sources = ('src/runtime/functional_iterators.cpp',
           'src/executor/xlang_vm/xlang_vm_inline_call.h',
           'src/executor/xlang_vm/ops/xlang_vm_ops_call.h',
           'tests/fixtures/core/native_trivial_callback.py',
           'tests/fixtures/expected/native_trivial_callback.out',
           'tests/run_fixtures.py')''' + text[end:]
text = text.replace('compare-native-trivial-callback-trial-20261007.py', 'compare-native-trivial-callback-trial-20261007.py')
text = text.replace("record['paired_callbacks'] = 'native-trivial-callback-paired-callbacks-20261007.json'",
                    "record['paired_callbacks'] = 'native-trivial-callback-paired-20261007.json'")
ast.parse(text)
output.write_bytes(text.encode('utf-8'))
print('Prepared serial validation with unchanged fixed gate and official BPE settings')
