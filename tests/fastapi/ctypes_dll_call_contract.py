"""CPython oracle for general Windows DLL loading and scalar libffi calls."""

import os
import json

if os.name == "nt":
    from ctypes import WinDLL, c_char_p, c_int, wintypes

    kernel32 = WinDLL("kernel32")
    process_id = kernel32.GetCurrentProcessId
    process_id.argtypes = []
    process_id.restype = wintypes.DWORD
    print("process", process_id() == os.getpid())

    codepage = kernel32.GetACP
    codepage.argtypes = []
    codepage.restype = wintypes.UINT
    print("codepage", isinstance(codepage(), int) and codepage() > 0)

    multiply = WinDLL("kernel32").MulDiv
    multiply.argtypes = [c_int, c_int, c_int]
    multiply.restype = c_int
    print("multiply", multiply(6, 7, 3))

    from fastapi import FastAPI
    from fastapi.testclient import TestClient

    app = FastAPI()

    @app.get("/native-ffi")
    def native_ffi():
        multiply.argtypes = (c_int, c_int, c_int)
        strlen = kernel32.lstrlenA
        strlen.argtypes = (c_char_p,)
        strlen.restype = c_int
        return {
            "result": multiply(6, 7, 3),
            "tuple_argtypes": isinstance(multiply.argtypes, tuple),
            "bytes_length": strlen(b"hello"),
        }

    response = TestClient(app).get("/native-ffi")
    print("tuple-fastapi", response.status_code, json.dumps(response.json(), sort_keys=True))
