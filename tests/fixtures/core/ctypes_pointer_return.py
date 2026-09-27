import sys
from ctypes import POINTER, c_int, pointer


local = pointer(c_int(42))
print('local', bool(local), local.contents.value, local[0])

empty = POINTER(c_int)()
print('null', bool(empty))
try:
    empty.contents
except ValueError as exc:
    print('null-error', str(exc))

if sys.platform == 'win32':
    from ctypes import c_wchar, windll

    command_line = windll.kernel32.GetCommandLineW
    command_line.restype = POINTER(c_wchar)
    returned = command_line()
    print('native', type(returned).__name__, bool(returned),
          len(returned.contents.value), returned.contents.value == returned[0])
