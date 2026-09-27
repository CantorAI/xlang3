"""CPython oracle for native ctypes array assignment and a public ASGI route."""

import json
from ctypes import c_int, c_void_p

from fastapi import FastAPI
from fastapi.testclient import TestClient


handles = (c_void_p * 2)()
handles[0] = 7
handles[-1] = c_void_p(8)
print("pointers", handles[0], handles[-1])

numbers = (c_int * 2)()
numbers[0] = c_int(3)
numbers[-1] = 4
print("integers", numbers[0], numbers[1])

try:
    handles[2] = 1
except IndexError as error:
    print("bounds", type(error).__name__, str(error))

app = FastAPI()


@app.get("/handle-array")
def handle_array():
    values = (c_void_p * 2)()
    values[0] = 11
    values[1] = c_void_p(12)
    return {"handles": [values[0], values[1]]}


response = TestClient(app).get("/handle-array")
print("fastapi", response.status_code, json.dumps(response.json(), sort_keys=True))
