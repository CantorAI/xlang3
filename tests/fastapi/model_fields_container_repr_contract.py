from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel


class Payload(BaseModel):
    amount: int


app = FastAPI()


@app.get('/field-repr')
def field_repr():
    fields_text = str(Payload.model_fields)
    return {'has_field_repr': 'FieldInfo(annotation=int, required=True)' in fields_text,
            'has_object_repr': ' object at ' in fields_text}


response = TestClient(app).get('/field-repr')
print(response.status_code, response.json())
