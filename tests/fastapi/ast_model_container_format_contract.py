import ast

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Model(BaseModel):
    value: int


namespace = {}
tree = ast.parse("def render(payload):\n    return f'{payload}'\n")
exec(compile(tree, '<model-container-format>', 'exec'), namespace)

app = FastAPI()


@app.get('/ast-model-container-format')
def ast_model_container_format():
    payload = {'model': Model(value=3)}
    return {'ast_fstring': namespace['render'](payload), 'format': format(payload)}


response = TestClient(app).get('/ast-model-container-format')
print(response.status_code, response.json())
