from fractions import Fraction

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import TypeAdapter


app = FastAPI()


@app.get('/fraction-bool')
def fraction_bool():
    adapter = TypeAdapter(Fraction)
    return {
        'true': str(adapter.validate_python(True)),
        'false': str(adapter.validate_python(False)),
        'numerator_type': type(True.numerator).__name__,
        'denominator': True.denominator,
    }


response = TestClient(app).get('/fraction-bool')
print(response.status_code, response.json())
