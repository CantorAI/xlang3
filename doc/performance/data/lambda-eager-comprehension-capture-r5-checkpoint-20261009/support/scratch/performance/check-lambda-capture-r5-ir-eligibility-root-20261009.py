"""Verify emitted captured-cell storage and retained ordinary/range fusion eligibility."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
OUT = DATA / 'lambda-eager-comprehension-capture-r5-ir-eligibility-20261009.json'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda root: {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--focused-receipt-sha256', required=True)
args = parser.parse_args()
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not OUT.exists()
focus_path = DATA / 'lambda-eager-comprehension-capture-r5-focused-complete-20261009.json'
assert sha(focus_path) == args.focused_receipt_sha256
focus = json.loads(focus_path.read_bytes())
assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
assert len(focus['phases']) == 22 and all(r['passed'] for r in focus['phases'])
old = ROOT / 'scratch/performance/lambda-capture-r4-emitted-ir-20261009/lambda_eager_comprehension_capture.ir.txt'
current = ROOT / 'scratch/performance/lambda-capture-r5-emitted-ir-20261009/lambda_eager_comprehension_capture/lambda_eager_comprehension_capture.ir.txt'
ranges = ROOT / 'scratch/performance/lambda-capture-r5-emitted-ir-20261009/nested_comprehension_capture/nested_comprehension_capture.ir.txt'
assert sha(old) == 'e54304badf6db148b63c52ecdc07adb62fd9dfe4e4e2aa45da2b1b50ad0542c8'
before = {str(ROOT / p): h for p, h in focus['source_sha256'].items()}
before.update({str(RELEASE / p): h for p, h in focus['binaries_sha256'].items()})
build_path = DATA / 'lambda-eager-comprehension-capture-r5-build-terminal-20261009.json'
app_path = DATA / 'lambda-eager-comprehension-capture-r5-applied-source-20261009.json'
assert sha(build_path) == focus['build_receipt_sha256']
assert sha(app_path) == focus['source_inventory_sha256']
for p in [old, current, ranges, focus_path, build_path, app_path, Path(__file__)]:
    before[str(p)] = sha(p)
handler = ROOT / 'src/executor/xlang_vm/ops/xlang_vm_ops_iteration.h'
assert sha(handler) == 'e0715f4a93414cd4835321eb18a0afa787baca78b5b12e84af65452017f49ed6'
before[str(handler)] = sha(handler)
assert all(sha(p) == h for p, h in before.items())
def blocks(path):
    parts = re.split(r'(?m)(?=^function #)', path.read_text())
    return {re.match(r'^function #(\d+)', block).group(1): block
            for block in parts if block.startswith('function #')}
old_blocks, new_blocks, range_blocks = blocks(old), blocks(current), blocks(ranges)
controls = {n: b for n, b in old_blocks.items() if '#comp.' in b
            and re.search(r'(?m)^  cells:[ \t]*$', b) and 'StoreLocalLoadLocal' in b}
record = dict(status='checking_ir_eligibility', terminal=False, timed=False, passed=False,
              eligibility_passed=False, performance_claim=False, source_sha256=focus['source_sha256'],
              source_inventory_sha256=focus['source_inventory_sha256'],
              candidate_binary_sha256=focus['candidate_binary_sha256'],
              focused_receipt_sha256=sha(focus_path), build_receipt_sha256=focus['build_receipt_sha256'],
              hashes_before=before,
              input_sha256={str(Path(p).relative_to(ROOT).as_posix()): h for p, h in before.items()}, checks=[])
try:
    assert len(controls) == 16
    assert all(n in new_blocks and new_blocks[n] == block for n, block in controls.items())
    record['checks'].append(dict(name='noncapturing_comprehension_fusion_retained', passed=True,
                                  function_ids=list(controls), identical_complete_function_blocks=True))
    broken, repaired = old_blocks['27'], new_blocks['27']
    assert 'first_line: 107' in repaired and 'escaping_factory.<locals>.<lambda>' in repaired
    assert 'StoreLocalLoadLocal' in broken and 'LoadCellObject' in broken and 'StoreCell ' not in broken
    assert 'StoreCell ' in repaired and repaired.index('StoreCell ') < repaired.index('LoadCellObject ')
    assert 'StoreLocalLoadLocal' not in repaired
    record['checks'].append(dict(name='captured_dictionary_target_stored_in_cell_before_closure', passed=True,
                                  function_id=27, old_block_sha256=hashlib.sha256(broken.encode()).hexdigest(),
                                  new_block_sha256=hashlib.sha256(repaired.encode()).hexdigest()))
    cell_ranges = []
    for n, block in range_blocks.items():
        match = re.search(r'(?m)^  cells:([^\n]*)', block)
        cell_slots = {int(x) for x in re.findall(r'%(\d+)', match.group(1))} if match else set()
        for line in block.splitlines():
            if 'ForRangeConstLocalNext ' in line:
                target = int(re.search(r'\ba=(\d+)', line).group(1))
                if target in cell_slots:
                    cell_ranges.append(dict(function_id=n, target_slot=target, instruction=line.strip()))
    assert cell_ranges
    record['checks'].append(dict(name='captured_constant_range_fast_path_retained', passed=True,
                                  examples=cell_ranges, unchanged_cell_aware_handler_sha256=sha(handler)))
    record.update(status='ir_eligibility_passed', passed=True, eligibility_passed=True)
except BaseException as error:
    record.update(status='ir_eligibility_failed', error=repr(error))
finally:
    record.update(terminal=True, hashes_unchanged=all(sha(p) == h for p, h in before.items()),
                  raw_ir_sha256={str(p.relative_to(ROOT)): sha(p) for p in [old, current, ranges]})
    OUT.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(record['status'], 'hashstable', record['hashes_unchanged'], sha(OUT))
if record.get('error'):
    print(record['error'])
raise SystemExit(0 if record['passed'] and record['hashes_unchanged'] else 1)
