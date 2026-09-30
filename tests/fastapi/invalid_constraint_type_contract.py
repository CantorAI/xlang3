from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field


class Model(BaseModel):
    values: list[int] = Field(multiple_of=0)


app = FastAPI()


@app.post("/invalid-constraint")
def invalid_constraint(values: list[int]):
    try:
        return Model(values=values).model_dump()
    except TypeError as error:
        return {"error": str(error)}


response = TestClient(app).post("/invalid-constraint", json=[1, 2])
print(response.status_code, response.json())
