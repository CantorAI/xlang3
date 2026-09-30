from decimal import Decimal

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, Field
from pydantic_core import SchemaValidator, core_schema


class Payload(BaseModel):
    amount: Decimal = Field(max_digits=3, decimal_places=1)


app = FastAPI()


@app.post("/decimal")
def decimal_endpoint(payload: Payload):
    return {"amount": str(payload.amount)}


client = TestClient(app)
for value in ("12.34", "1.22", "123"):
    response = client.post("/decimal", json={"amount": value})
    error = response.json()["detail"][0]
    print(response.status_code, error["type"], error["msg"], error["ctx"])

try:
    Payload.model_validate({"amount": Decimal("sNaN")})
except Exception as error:
    print(error.errors(include_url=False)[0]["type"])

try:
    SchemaValidator(core_schema.decimal_schema(allow_inf_nan=True, max_digits=4))
except Exception as error:
    print(type(error).__name__, str(error).splitlines()[-1].strip())
