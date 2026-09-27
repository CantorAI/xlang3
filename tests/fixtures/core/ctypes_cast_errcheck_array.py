import sys
from ctypes import POINTER, c_char_p, c_int, c_void_p, cast, pointer


number = c_int(42)
address = cast(pointer(number), c_void_p)
typed = cast(pointer(number), POINTER(c_int))
print('cast', bool(address.value), typed[0])

item = c_char_p(b'serverAuth')
array = (c_char_p * 1)(item)
print('array', len(array), array[0])

if sys.platform == 'win32':
    from ctypes import WinDLL, c_wchar_p

    function = WinDLL('kernel32').GetModuleHandleW
    function.argtypes = (c_wchar_p,)
    function.restype = c_void_p
    function.errcheck = lambda result, native, args: args
    print('errcheck-original', bool(function(None)))
    function.errcheck = lambda result, native, args: ('checked', bool(result), len(args))
    print('errcheck-replace', function(None))
