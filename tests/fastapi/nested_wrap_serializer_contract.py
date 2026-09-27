from typing import Any, Callable, Optional

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, model_serializer


class Model(BaseModel):
    x: int
    inner: Optional["Model"]

    @model_serializer(mode="wrap")
    def serialize_model(
        self, handler: Callable[["Model"], dict[str, Any]]
    ) -> dict[str, Any]:
        result = handler(self)
        result["x"] += 1
        return result


app = FastAPI()


@app.get("/nested-wrap-serializer")
def nested_wrap_serializer():
    value = Model(x=2, inner=Model(x=1, inner=Model(x=0, inner=None)))
    return value.model_dump()


response = TestClient(app).get("/nested-wrap-serializer")
print(response.status_code, response.json())
