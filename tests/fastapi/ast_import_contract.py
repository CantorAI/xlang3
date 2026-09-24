import ast

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/ast-import")
def ast_import():
    tree = ast.parse(
        "import math, json as js\n"
        "from collections import deque as Queue\n"
        "mapping = {'a': 1, **{'b': 2}}\n"
    )
    namespace = {}
    exec(compile(tree, "<ast-import>", "exec"), namespace)
    try:
        exec(compile(ast.parse("raise ValueError('raised')"),
                     "<ast-raise>", "exec"))
    except ValueError as error:
        raised = str(error)
    return {
        "sqrt": namespace["math"].sqrt(81),
        "json": namespace["js"].loads("[1, 2]"),
        "queue": list(namespace["Queue"]([3, 4])),
        "mapping": namespace["mapping"],
        "raise": raised,
    }


with TestClient(app) as client:
    response = client.get("/ast-import")
    assert response.status_code == 200, response.text
    assert response.json() == {
        "sqrt": 9.0,
        "json": [1, 2],
        "queue": [3, 4],
        "mapping": {"a": 1, "b": 2},
        "raise": "raised",
    }, response.text
    print(response.status_code, response.json())
