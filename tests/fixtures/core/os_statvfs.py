# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
import errno
import os
from pathlib import Path
import pickle
import shutil

fields = ('f_bsize', 'f_frsize', 'f_blocks', 'f_bfree', 'f_bavail',
          'f_files', 'f_ffree', 'f_favail', 'f_flag', 'f_namemax')
info = os.statvfs(Path('.'))
assert isinstance(info, tuple) and type(info) is os.statvfs_result
assert len(info) == 10 and all(info[i] == getattr(info, name) for i, name in enumerate(fields))
assert info.f_bsize > 0 and info.f_frsize > 0 and info.f_blocks > 0
assert info.f_bfree >= info.f_bavail >= 0 and isinstance(info.f_fsid, int)
assert os.statvfs(path=b'.').f_fsid == info.f_fsid
assert os.statvfs in os.supports_fd
with open(__file__, 'rb') as stream:
    fd = stream.fileno()
    class Descriptor:
        def __index__(self): return fd
    assert os.fstatvfs(fd).f_fsid == os.statvfs(Descriptor()).f_fsid == info.f_fsid
    assert os.fstatvfs(fd).f_blocks == info.f_blocks
print('paths-and-descriptors', True)

constructed = os.statvfs_result(range(11))
assert tuple(constructed) == tuple(range(10)) and constructed.f_fsid == 10
assert os.statvfs_result(range(10)).f_fsid is None
assert os.statvfs_result(range(10), {'f_fsid': 2**64 - 1}).f_fsid == 2**64 - 1
assert constructed == tuple(range(10)) and hash(constructed) == hash(tuple(range(10)))
assert (constructed.n_sequence_fields, constructed.n_fields, constructed.n_unnamed_fields) == (10, 11, 0)
assert repr(constructed).startswith('os.statvfs_result(f_bsize=0, f_frsize=1, ')
for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
    restored = pickle.loads(pickle.dumps(info, protocol=protocol))
    assert type(restored) is os.statvfs_result and restored == info and restored.f_fsid == info.f_fsid
for name in ('f_bsize', 'f_fsid', 'extra'):
    try: setattr(constructed, name, 99)
    except AttributeError: pass
    else: raise AssertionError('result must be immutable')
print('result-contract', True)

for function, error_type, number, filename in (
    (lambda: os.statvfs(''), FileNotFoundError, errno.ENOENT, ''),
    (lambda: os.statvfs(b'nonexistent-v155-statvfs'), FileNotFoundError, errno.ENOENT, b'nonexistent-v155-statvfs'),
    (lambda: os.fstatvfs(-1), OSError, errno.EBADF, None),
    (lambda: os.statvfs(-1), OSError, errno.EFAULT, -1),
):
    try: function()
    except error_type as error:
        assert error.errno == number and error.filename == filename
    else: raise AssertionError('expected filesystem error')
for function in (lambda: os.statvfs(None), lambda: os.statvfs(1.5), lambda: os.statvfs(),
                 lambda: os.statvfs('.', path='.'), lambda: os.statvfs(other='.'),
                 lambda: os.statvfs_result(range(9)), lambda: os.statvfs_result(range(12))):
    try: function()
    except TypeError: pass
    else: raise AssertionError('expected TypeError')
try: os.statvfs('bad\0path')
except ValueError: pass
else: raise AssertionError('expected embedded NUL rejection')
print('errors', True)

class MissingPath:
    calls = 0
    def __fspath__(self):
        self.calls += 1
        return 'nonexistent-v155-pathlike'
missing = MissingPath()
try: os.statvfs(missing)
except FileNotFoundError as error:
    assert error.filename == 'nonexistent-v155-pathlike' and missing.calls == 1
else: raise AssertionError('expected missing path')

usage = shutil.disk_usage(Path('.'))
assert usage.total == info.f_blocks * info.f_frsize
assert usage.used >= 0 and usage.free >= 0 and usage.total >= usage.used + usage.free
assert usage._fields == ('total', 'used', 'free')
print('python-shutil-disk-usage', True)
