from fastapi import FastAPI
from fastapi.testclient import TestClient


namespace = {"bar": "baz"}
exec(
    "from pydantic import TypeAdapter\n"
    "adapter = TypeAdapter(list[str])\n"
    "validated = adapter.validate_python(['alpha', 'beta'])\n"
    "name_missing = '__name__' not in globals()\n",
    namespace,
)

app = FastAPI()


@app.get("/exec-missing-name")
def exec_missing_name():
    return {"validated": namespace["validated"], "name_missing": namespace["name_missing"]}


response = TestClient(app).get("/exec-missing-name")
print(response.status_code, response.json())
