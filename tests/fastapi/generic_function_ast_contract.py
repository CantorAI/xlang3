import ast

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import TypeAdapter


source = "def pair[T](a: 'int', b: 'T'):\n    return (a, b)\n"
namespace = {}
exec(compile(ast.parse(source), "<generic function ast>", "exec"), namespace)
adapter = TypeAdapter(namespace["pair"])


app = FastAPI()


@app.get("/generic-function")
def generic_function():
    return {"result": adapter.validate_python({"a": "1", "b": True})}


response = TestClient(app).get("/generic-function")
print(response.status_code, response.json())
