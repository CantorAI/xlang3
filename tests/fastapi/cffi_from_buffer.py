"""Compare CFFI zero-copy buffer exports with CPython 3.14."""
import os
import runpy
import sys

generated = None
for root in sys.path:
    candidate = os.path.join(root, 'trio', '_core', '_generated_windows_ffi.py')
    if os.path.isfile(candidate):
        generated = candidate
        break
if generated is None:
    raise RuntimeError('Trio CFFI declarations unavailable')
ffi = runpy.run_path(generated)['ffi']

readonly = b'abc'
cbuf = ffi.from_buffer(readonly)
print(len(cbuf), cbuf[0], cbuf[2])
try:
    ffi.from_buffer(readonly, require_writable=True)
except (TypeError, BufferError) as exc:
    print(type(exc).__name__)

mutable = bytearray(b'xyz')
with ffi.from_buffer(mutable, require_writable=True) as cbuf:
    print(len(cbuf), cbuf[0], cbuf[2])
    try:
        mutable.append(33)
    except BufferError as exc:
        print(type(exc).__name__)
mutable.append(33)
print(bytes(mutable))
