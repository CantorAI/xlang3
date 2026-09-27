from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel
from pydantic_core import SchemaSerializer, core_schema


schema = core_schema.list_schema(core_schema.dict_schema(
    core_schema.str_schema(), core_schema.int_schema()))
serializer = SchemaSerializer(schema)
try:
    serializer.to_python([{'a': 1}], exclude={0: 1})
except TypeError as error:
    print(type(error).__name__, str(error))


class Item(BaseModel):
    values: list[dict[str, int]]


app = FastAPI()


@app.get('/nested-filter-type')
def nested_filter_type():
    item = Item(values=[{'a': 1}])
    try:
        item.model_dump(exclude={'values': {0: 1}})
    except TypeError as error:
        return {'error': str(error)}


response = TestClient(app).get('/nested-filter-type')
print(response.status_code, response.json())
