"""CPython oracle for Windows ctypes out-pointer and wide-string semantics."""

import sys

if sys.platform == "win32":
    from ctypes import HRESULT, POINTER, Structure, WinDLL, byref, wintypes

    class GUID(Structure):
        _fields_ = [
            ("Data1", wintypes.DWORD),
            ("Data2", wintypes.WORD),
            ("Data3", wintypes.WORD),
            ("Data4", wintypes.BYTE * 8),
        ]

    ole32 = WinDLL("ole32")
    ole32.CLSIDFromString.restype = HRESULT
    ole32.CLSIDFromString.argtypes = [wintypes.LPCOLESTR, POINTER(GUID)]
    ole32.CoTaskMemFree.restype = None
    ole32.CoTaskMemFree.argtypes = [wintypes.LPVOID]

    shell32 = WinDLL("shell32")
    shell32.SHGetKnownFolderPath.restype = HRESULT
    shell32.SHGetKnownFolderPath.argtypes = [
        POINTER(GUID),
        wintypes.DWORD,
        wintypes.HANDLE,
        POINTER(wintypes.LPWSTR),
    ]

    guid = GUID()
    ole32.CLSIDFromString("{F1B32785-6FBA-4FCF-9D55-7B8E7F157091}", byref(guid))
    path_ptr = wintypes.LPWSTR()
    status = shell32.SHGetKnownFolderPath(byref(guid), 0x00004000, None, byref(path_ptr))
    result = path_ptr.value
    print("status", status)
    print("type", type(result).__name__)
    print("path", result.endswith("AppData\\Local"))
    print("wide", any(ord(c) > 255 for c in result))
    ole32.CoTaskMemFree(path_ptr)
