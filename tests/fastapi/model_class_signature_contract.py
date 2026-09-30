from inspect import signature

from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class CallableModel(BaseModel):
    value: int = 3

    def __call__(self, arg: int) -> bool:
        return arg == self.value


app = FastAPI()


@app.get('/model-signature')
def model_signature():
    instance = CallableModel()
    return {
        'class_signature': str(signature(CallableModel)),
        'instance_signature': str(signature(instance)),
        'instance_has_class_signature': hasattr(instance, '__signature__'),
        'call_result': instance(3),
    }


response = TestClient(app).get('/model-signature')
print(response.status_code, response.json())
