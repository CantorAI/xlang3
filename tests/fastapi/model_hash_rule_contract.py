from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict


class MutableModel(BaseModel):
    value: int


class FrozenModel(BaseModel):
    model_config = ConfigDict(frozen=True)
    value: int


app = FastAPI()


@app.get('/model-hash-rule')
def model_hash_rule():
    try:
        hash(MutableModel(value=1))
    except TypeError as error:
        mutable_error = str(error)
    return {
        'mutable_error': mutable_error,
        'frozen_equal_hash': hash(FrozenModel(value=1)) == hash(FrozenModel(value=1)),
    }


response = TestClient(app).get('/model-hash-rule')
print(response.status_code, response.json())
