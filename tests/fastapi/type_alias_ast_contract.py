import ast

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/alias-ast")
def alias_ast():
    alias = ast.parse("type Alias[T] = list[T]").body[0]
    return {
        "node": type(alias).__name__,
        "name": alias.name.id,
        "parameter": alias.type_params[0].name,
        "source": ast.unparse(alias),
    }


response = TestClient(app).get("/alias-ast")
print(response.status_code, response.json())
