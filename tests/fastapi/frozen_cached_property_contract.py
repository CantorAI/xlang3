from functools import cached_property

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict


class FrozenModel(BaseModel):
    model_config = ConfigDict(frozen=True)
    value: int

    @cached_property
    def doubled(self):
        return self.value * 2


app = FastAPI()


@app.get('/frozen-cached-property')
def frozen_cached_property():
    model = FrozenModel(value=3)
    initial = model.doubled
    del model.doubled
    model.doubled = 9
    return {'initial': initial, 'updated': model.doubled}


response = TestClient(app).get('/frozen-cached-property')
print(response.status_code, response.json())
