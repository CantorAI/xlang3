"""Compare generated CFFI struct field layouts and writes with CPython 3.14."""
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

poll = ffi.new('AFD_POLL_INFO *')
poll.Timeout = 9223372036854775807
poll.NumberOfHandles = 1
poll.Exclusive = 0
poll.Handles[0].Handle = ffi.cast('void *', 4660)
poll.Handles[0].Events = 19
poll.Handles[0].Status = 7
print(ffi.sizeof('AFD_POLL_INFO'), poll.Timeout, poll.NumberOfHandles,
      poll.Exclusive, len(poll.Handles))
print(int(ffi.cast('uintptr_t', poll.Handles[0].Handle)),
      poll.Handles[0].Events, poll.Handles[0].Status)
entry = ffi.new('OVERLAPPED_ENTRY[]', 2)
entry[1].dwNumberOfBytesTransferred = 37
print(len(entry), entry[0].dwNumberOfBytesTransferred,
      entry[1].dwNumberOfBytesTransferred)
count = ffi.new('PULONG')
print(type(count[0]).__name__, count[0])
pointer = ffi.new('HANDLE *')
left = ffi.cast('void *', pointer)
right = ffi.cast('void *', pointer)
print(left == right, hash(left) == hash(right), {left: 7}[right])
overlapped = ffi.new('LPOVERLAPPED')
overlapped.Internal = 5
print(overlapped.Internal)
for field, value in (('NumberOfHandles', -1), ('NumberOfHandles', 2**32), ('Timeout', 2**63)):
    try:
        setattr(poll, field, value)
    except OverflowError as exc:
        print(field, type(exc).__name__)
