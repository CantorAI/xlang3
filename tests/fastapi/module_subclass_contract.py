import json
import types

from fastapi import FastAPI
from fastapi.testclient import TestClient


class ForwardingModule(types.ModuleType):
    def __init__(self, source):
        super().__init__(source.__name__)
        self.source = source

    def __getattr__(self, name):
        return getattr(self.source, name)


app = FastAPI()


@app.get("/module-subclass")
def module_subclass():
    source = types.ModuleType("source")
    source.answer = 42
    wrapped = ForwardingModule(source)
    return {
        "name": wrapped.__name__,
        "answer": wrapped.answer,
        "source": wrapped.source is source,
    }


response = TestClient(app).get("/module-subclass")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
