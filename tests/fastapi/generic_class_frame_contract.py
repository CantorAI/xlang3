from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import validate_call


class Handler[T]:
    @validate_call(validate_return=True)
    def format(self, value: T) -> T:
        return str(value)


app = FastAPI()


@app.get("/generic-class-frame")
def generic_class_frame():
    handler = Handler[int]()
    return {"integer": handler.format(1), "string": handler.format("1")}


response = TestClient(app).get("/generic-class-frame")
print(response.status_code, response.json())
