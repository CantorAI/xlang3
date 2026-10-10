"""Use an explicit buffer address for the diagnostic's void-pointer ABI."""
import hashlib
import json
from pathlib import Path

root = Path('D:/CantorAI/xlang3/scratch/performance')
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
names = ['coverage-original-body-allocation-child-proposed-20261010.py',
         'run-coverage-original-body-allocation-proposed-20261010.py',
         'coverage-original-body-allocation-provenance-proposed-20261010.json']
new = [s.replace('-proposed-', '-r2-proposed-') for s in names]
assert sha(root / names[0]) == 'bcaa680e4afdca7398eb1f0f1a9fc21b827b6851fe686de679da44012a33bd5c'
assert sha(root / names[1]) == 'bea0e4f205c92d89beb4fb31dd071358d493b9dfde84fd29c30a97e47750ae75'
assert sha(root / names[2]) == '7b1a12bbda45d90bd4cf3fed534be130779b1c4736bd8f1995d1700bdb44d82c'
assert all(not (root / s).exists() for s in new)
raw = (root / names[0]).read_bytes()
old_call = b'length = filename(library._handle, buffer, len(buffer))'
assert raw.count(old_call) == 1
(root / new[0]).write_bytes(raw.replace(old_call, b'length = filename(library._handle, ctypes.addressof(buffer), len(buffer))'))
raw = (root / names[1]).read_bytes()
for a, b in zip(names, new):
    raw = raw.replace(a.encode(), b.encode())
(root / new[1]).write_bytes(raw)
proof = json.loads((root / names[2]).read_bytes())
pins = proof['file_sha256']
for a, b in zip(names[:2], new[:2]):
    del pins[str(root / a)]
    pins[str(root / b)] = sha(root / b)
proof['preparation'] = 'Root R2 diagnostic ABI repair only: explicit ctypes.addressof(buffer) for GetModuleFileNameA void-pointer argument. Original body and frozen R1 retained.'
proof['parent_proof_sha256'] = sha(root / names[2])
proof['failed_parent_receipt_sha256'] = 'b34cdef4548bf11ead6a5c0bc6a89442b4beeb805f1cbf142e4f01400063d2b1'
proof['preparation_controller_sha256'] = sha(__file__)
(root / new[2]).write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8', newline='\n')
print('r2_proof_sha256', sha(root / new[2]), flush=True)
