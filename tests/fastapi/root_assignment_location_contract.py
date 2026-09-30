from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import ConfigDict, RootModel, ValidationError


class Model(RootModel[int]):
    model_config = ConfigDict(validate_assignment=True)


app = FastAPI()


@app.get("/root-assignment-location")
def root_assignment_location():
    model = Model(42)
    try:
        model.root = "bad"
    except ValidationError as exc:
        return {"location": list(exc.errors(include_url=False)[0]["loc"]), "root": model.root}
    return {"location": ["missing-error"], "root": model.root}


response = TestClient(app).get("/root-assignment-location")
print(response.status_code, response.json())
