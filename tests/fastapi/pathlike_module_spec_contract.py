import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/module')
def load_module():
    with TemporaryDirectory() as directory:
        source = Path(directory) / 'loaded_model.py'
        source.write_text(
            'from pydantic import BaseModel\n'
            'class Payload(BaseModel):\n'
            '    value: int\n'
        )
        spec = importlib.util.spec_from_file_location('loaded_model', source)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.Payload(value='42').model_dump()


response = TestClient(app).get('/module')
print(response.status_code, response.json())
