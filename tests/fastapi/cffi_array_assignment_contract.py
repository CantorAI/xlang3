"""CPython oracle for CFFI array stores used by Windows async subprocesses."""

import json

from fastapi import FastAPI
from fastapi.testclient import TestClient
from trio._core._generated_windows_ffi import ffi


handles = ffi.new("HANDLE[2]")
handles[0] = ffi.cast("HANDLE", 7)
handles[1] = ffi.cast("HANDLE", 8)
print("handles", int(ffi.cast("uintptr_t", handles[0])), int(ffi.cast("uintptr_t", handles[1])))

numbers = ffi.new("DWORD[2]")
numbers[0] = 3
numbers[1] = 4
print("numbers", numbers[0], numbers[1])

for value in (-1, 4294967296):
    try:
        numbers[0] = value
    except OverflowError as error:
        print("overflow", value, type(error).__name__)

try:
    handles[0] = None
except TypeError as error:
    print("pointer-type", type(error).__name__)

try:
    handles[2] = ffi.cast("HANDLE", 9)
except IndexError as error:
    print("bounds", type(error).__name__)

app = FastAPI()


@app.get("/cffi-handles")
def cffi_handles():
    values = ffi.new("HANDLE[2]")
    values[0] = ffi.cast("HANDLE", 11)
    values[1] = ffi.cast("HANDLE", 12)
    return {
        "handles": [
            int(ffi.cast("uintptr_t", values[0])),
            int(ffi.cast("uintptr_t", values[1])),
        ]
    }


response = TestClient(app).get("/cffi-handles")
print("fastapi", response.status_code, json.dumps(response.json(), sort_keys=True))
