"""Repair the IR reader using retained executions, without rerunning children."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CAPTURE = DATA / 'object-new-route-untimed-20261009.json'
OUT = DATA / 'object-new-route-untimed-ir-verification-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(CAPTURE) == 'bd6fd41c0f8f05952d529f6ac85c7a036dbe5684090815edef511edca13618c7'
capture = json.loads(CAPTURE.read_bytes())
assert capture['terminal'] and capture['hashes_unchanged'] and len(capture['phases']) == 2
assert capture['error'].startswith("AssertionError(('EntryOnly.__new__'")
assert all(sha(p) == h for p, h in capture['pins_before'].items())
folder = DATA / 'object-new-route-untimed-20261009'
assert all(sha(folder / p) == h for p, h in capture['artifacts_sha256'].items())
assert all(p['exit_code'] == 0 and p['semantic_result']['verify_only'] and
           p['semantic_result']['timed_operation_count'] == 0 and
           p['semantic_result']['seconds'] == {} for p in capture['phases'])
ir = folder / 'ir/object-new-static-lookup-diagnostic-child-20261009.ir.txt'
text = ir.read_text(encoding='utf-8')
blocks = {}
for match in re.finditer(r'^function #\d+ ([^\n]+)\n.*?(?=^function #|\Z)', text, re.M | re.S):
    body = match.group(0)
    qualname = re.search(r'^  qualname: (.+)$', body, re.M)
    blocks[qualname.group(1) if qualname else match.group(1)] = body
original, saved, main = (blocks[n] for n in ('EntryOnly.__new__', 'saved_native_wrapper', 'main'))
checks = {
    'original_real_native_method_call': 'CallMethod line=31' in original and '#0=object #1=__new__' in original,
    'saved_real_native_direct_call': 'Call line=39' in saved and 'CallMethod' not in saved,
    'both_fixed_two_argument_python_functions': all('params: %0=cls %1=state' in b and
        'generator: false' in b and 'async: false' in b for b in (original, saved)),
    'shared_selected_python_function_local': '%13=function' in main and
        'LoadLocal line=169 pos=169:25-169:51 dst=299 a=13' in main,
    'same_class_and_state_arguments': all(s in main for s in
        ('LoadModuleSlot line=169 pos=169:25-169:51 dst=300 a=17',
         'LoadModuleSlot line=169 pos=169:25-169:51 dst=301 a=15', 'call_args #48: r300 r301')),
    'timed_branch_retains_ordinary_python_call': 'Call line=169 pos=169:25-169:51 dst=302 a=299 b=48' in main,
    'same_loop_result_replacement': 'StoreLocal line=169 pos=169:25-169:51 dst=20 a=302' in main and
        'Jump line=168 pos=168:13-170:13 dst=327' in main,
}
assert all(checks.values()), checks
record = {'terminal': True, 'status': 'untimed_semantics_and_ir_verified', 'timed': False,
          'checks': checks, 'capture_sha256': sha(CAPTURE), 'ir_sha256': sha(ir),
          'controller_sha256': sha(__file__), 'child_sha256': capture['pins_before'][str(ROOT / 'scratch/performance/object-new-static-lookup-diagnostic-child-20261009.py')],
          'corrected_reader': 'IR function heading is __new__; qualified name is on its separate qualname line.',
          'scope': 'CPython 3.14.7 and current XLang3 untimed route semantics passed. The emitted timed branch has ordinary Python Call entry and native calls in both bodies; no timings or dynamic fast-path hit count.',
          'report_limits': 'Child loop-included fields describe its dormant timed branch. verify_only=True, empty seconds and zero timed operations prove this capture did not run the benchmark loops.',
          'hashes_unchanged': all(sha(p) == h for p, h in capture['pins_before'].items())}
assert not OUT.exists()
OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({'status': record['status'], 'checks': checks, 'receipt_sha256': sha(OUT)}, indent=2))
