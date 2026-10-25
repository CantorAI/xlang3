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

    from ctypes import c_void_p, create_unicode_buffer

    module_handle = windll.kernel32.GetModuleHandleW
    module_handle.restype = c_void_p
    print('untyped-null', bool(module_handle(None)))
    folder = create_unicode_buffer(260)
    print('untyped-buffer', windll.shell32.SHGetFolderPathW(None, 26, None, 0, folder) == 0, bool(folder.value))
    module_handle.argtypes = [c_int]
    try:
        module_handle(None)
    except TypeError:
        print('typed-null', 'rejected')
