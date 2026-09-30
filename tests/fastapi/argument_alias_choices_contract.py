from typing import Annotated

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import AliasChoices, Field, validate_call


@validate_call
def add(
    a: Annotated[int, Field(validation_alias="b")],
    c: Annotated[int, Field(validation_alias=AliasChoices("d", "e"))],
) -> int:
    return a + c


app = FastAPI()


@app.get("/add")
def add_endpoint(choice: str = "e"):
    return {"result": add(b=1, **{choice: 4})}


client = TestClient(app)
for choice in ("d", "e"):
    response = client.get("/add", params={"choice": choice})
    print(response.status_code, response.json())

try:
    add(b=1)
except Exception as error:
    print(error.errors(include_url=False)[0]["loc"])
