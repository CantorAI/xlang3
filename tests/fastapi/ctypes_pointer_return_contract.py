import sys
from ctypes import POINTER, c_int, pointer

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/pointers')
def pointers() -> dict:
    local = pointer(c_int(42))
    empty = POINTER(c_int)()
    result = {'local': [bool(local), local.contents.value, local[0]],
              'null': bool(empty)}
    try:
        empty.contents
    except ValueError as exc:
        result['null_error'] = str(exc)
    if sys.platform == 'win32':
        from ctypes import c_wchar, windll

        command_line = windll.kernel32.GetCommandLineW
        command_line.restype = POINTER(c_wchar)
        returned = command_line()
        result['native'] = [type(returned).__name__, bool(returned),
                            len(returned.contents.value), returned.contents.value == returned[0]]
    return result


with TestClient(app) as client:
    response = client.get('/pointers')
    print(response.status_code, response.json())
