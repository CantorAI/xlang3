from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, ConfigDict


def model_with_duplicate_name():
    class Payload(BaseModel):
        earlier: int
        """Earlier field"""
        model_config = ConfigDict(use_attribute_docstrings=True)

    if True:
        class Payload(BaseModel):
            current: int
            """Current field"""
            model_config = ConfigDict(use_attribute_docstrings=True)

    return Payload


Payload = model_with_duplicate_name()
app = FastAPI()


@app.post('/payload')
def payload_route(payload: Payload):
    return {'value': payload.current,
            'description': Payload.model_fields['current'].description}


response = TestClient(app).post('/payload', json={'current': 3})
print(response.status_code, response.json())
