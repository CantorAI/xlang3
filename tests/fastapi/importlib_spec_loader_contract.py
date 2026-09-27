import importlib.util

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


class Loader:
    pass


@app.get('/module-spec')
def module_spec() -> dict:
    loader = Loader()
    spec = importlib.util.spec_from_loader(
        'sample.child', loader, origin='generated', is_package=True)
    return {
        'name': spec.name,
        'origin': spec.origin,
        'has_location': spec.has_location,
        'cached': spec.cached,
        'parent': spec.parent,
        'package_locations': spec.submodule_search_locations,
    }


with TestClient(app) as client:
    response = client.get('/module-spec')
    print(response.status_code, response.json())
