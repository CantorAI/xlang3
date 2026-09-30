from ctypes import Structure, Union, alignment, c_char, c_int, c_short, sizeof

from fastapi import FastAPI
from fastapi.testclient import TestClient


class Header(Structure):
    _fields_ = (('tag', c_char), ('count', c_int))


class Packet(Structure):
    _fields_ = (('size', c_short), ('header', Header))


class Packed(Structure):
    _pack_ = 1
    _fields_ = (('tag', c_char), ('count', c_int))


class Choice(Union):
    _fields_ = (('tag', c_char), ('count', c_int))


app = FastAPI()


@app.get('/layout')
def layout() -> dict[str, list[int]]:
    return {
        kind.__name__: [sizeof(kind), alignment(kind)]
        for kind in (Header, Packet, Packed, Choice)
    }


with TestClient(app) as client:
    response = client.get('/layout')
    print(response.status_code, response.json())
