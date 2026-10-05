import os

if os.name == 'posix':
    import ctypes
    from _ctypes import dlopen
    library = ctypes.CDLL(None)
    library.getpid.argtypes = []
    library.getpid.restype = ctypes.c_int
    assert library.getpid() == os.getpid()
    library.strlen.argtypes = [ctypes.c_char_p]
    library.strlen.restype = ctypes.c_size_t
    assert library.strlen(b'CantorAI') == 8
    for name in ('/cantorai-nonexistent-library.so', b'/cantorai-nonexistent-library.so'):
        try:
            dlopen(name, ctypes.RTLD_LOCAL)
        except OSError:
            pass
        else:
            raise AssertionError('missing library loaded')
    try:
        dlopen('bad\0library')
    except ValueError:
        pass
    else:
        raise AssertionError('embedded null accepted')
    try:
        dlopen(None, 2**40)
    except OverflowError:
        pass
    else:
        raise AssertionError('invalid mode accepted')
print('native ctypes passed')
