"""Character defaults, native wide-buffer writeback and indexed NUL values."""
import ctypes
import sys

assert ctypes.c_wchar().value == "\0"
assert ctypes.c_char().value == b"\0"
buffer = ctypes.create_unicode_buffer(16)
assert len(buffer) == 16
assert [ord(character) for character in buffer] == [0] * 16

if sys.platform == "win32":
    kernel32 = ctypes.WinDLL("kernel32")
    copy = kernel32.lstrcpyW
    copy.argtypes = [ctypes.POINTER(ctypes.c_wchar), ctypes.c_wchar_p]
    copy.restype = ctypes.c_void_p
    text = "A\u03a9"
    try:
        copy((ctypes.c_int * 16)(), text)
    except TypeError:
        pass
    else:
        raise AssertionError("Mismatched pointer element must be refused before native dispatch")
    assert copy(buffer, text)
    assert buffer.value == text
    assert [buffer[index] for index in range(3)] == ["A", "\u03a9", "\0"]
    assert any(ord(character) > 255 for character in buffer)
    pointer = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_wchar))
    assert pointer[0] == "A"
    assert pointer[1] == "\u03a9"
    assert pointer[2] == "\0"
    assert pointer[15] == "\0"
    emoji = "\U0001f600"
    assert copy(buffer, emoji)
    assert buffer.value == emoji
    assert buffer[0] == "\ud83d" and buffer[1] == "\ude00"
    assert copy(buffer, "B")
    assert buffer.value == "B"
    classify = ctypes.WinDLL("msvcrt").iswalpha
    classify.argtypes = [ctypes.c_wchar]
    classify.restype = ctypes.c_int
    assert classify(ctypes.c_wchar("A")) != 0
    assert classify(ctypes.c_wchar()) == 0

print("ctypes Unicode buffer characters preserved")
