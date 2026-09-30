from typing import Union

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, TypeAdapter
from pydantic_core import MISSING, PydanticSerializationUnexpectedValue


class Item(BaseModel):
    value: Union[int, MISSING] = MISSING
    other: MISSING = MISSING


app = FastAPI()


@app.get('/missing')
def missing_item():
    item = Item()
    return {'dump': item.model_dump(), 'json': item.model_dump_json()}


@app.get('/present')
def present_item():
    item = Item(value=7)
    return {'dump': item.model_dump(), 'json': item.model_dump_json()}


client = TestClient(app)
for path in ('/missing', '/present'):
    response = client.get(path)
    print(path, response.status_code, response.json())

adapter = TypeAdapter(MISSING)
print('adapter_identity', adapter.dump_python(MISSING) is MISSING)
try:
    adapter.dump_python(7)
except PydanticSerializationUnexpectedValue:
    print('adapter_rejects_other', True)
else:
    print('adapter_rejects_other', False)
