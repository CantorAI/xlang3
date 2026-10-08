"""Prepare a one-off serial validation controller without changing measurements."""
from pathlib import Path
import ast

root = Path.cwd()
source = root / 'scratch/performance/validate-inherited-subscript-cache-20261007.py'
output = root / 'scratch/performance/validate-minmax-streaming-20261007.py'
assert not output.exists()
text = source.read_text()
text = text.replace('Validate the inherited subscript cache once', 'Validate native min/max streaming and global deletion once')
text = text.replace('inherited-subscript-cache', 'minmax-streaming')
start = text.index('sources = (')
end = text.index('\nrecord = ', start)
text = text[:start] + '''sources = ('src/builtins/functional_builtins.cpp',
           'src/executor/xlang_vm/ops/xlang_vm_ops_variables.h',
           'src/executor/xlang_vm/xlang_vm_op_rows.h',
           'src/executor/xlang_vm/xlang_frame.h',
           'tests/fixtures/core/minmax_streaming.py',
           'tests/fixtures/expected/minmax_streaming.out',
           'tests/fixtures/core/global_delete_temporaries.py',
           'tests/fixtures/expected/global_delete_temporaries.out',
           'tests/run_fixtures.py')''' + text[end:]
text = text.replace("phase('paired-dispatch'", "phase('paired-callbacks'")
text = text.replace('compare-inherited-subscript-trial-20261007.py', 'compare-minmax-streaming-trial-20261007.py')
text = text.replace("record['paired_dispatch'] = 'minmax-streaming-paired-20261007.json'",
                    "record['paired_callbacks'] = 'minmax-streaming-paired-callbacks-20261007.json'")
ast.parse(text)
output.write_bytes(text.encode('utf-8'))
print('Prepared serial validation controller; original gate and BPE settings preserved')
