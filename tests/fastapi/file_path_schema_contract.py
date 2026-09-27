from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import BaseModel, FilePath


class FileModel(BaseModel):
    path: FilePath


app = FastAPI()


@app.get('/file-path-schema')
def file_path_schema():
    field = FileModel.model_json_schema()['properties']['path']
    return {'field': field, 'merged': (lambda value: {**value, 'format': 'path'})({'type': 'string'})}


response = TestClient(app).get('/file-path-schema')
print(response.status_code, response.json())
