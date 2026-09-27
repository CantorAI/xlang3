import math

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/math-real-type-error")
def math_real_type_error():
    errors = []
    for function in (math.isfinite, math.isinf, math.isnan):
        try:
            function("2")
        except Exception as exc:
            errors.append([function.__name__, type(exc).__name__, str(exc)])

    return {"errors": errors}


response = TestClient(app).get("/math-real-type-error")
print(response.status_code, response.json())
