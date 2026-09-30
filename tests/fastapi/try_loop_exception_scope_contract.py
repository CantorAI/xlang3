from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


class MarkerError(Exception):
    pass


@app.get('/nested-try-loop')
def nested_try_loop() -> dict:
    try:
        try:
            for item in ['version']:
                try:
                    if item != 'version':
                        raise MarkerError('mismatch')
                    break
                except MarkerError:
                    pass
        except MarkerError:
            pass
        else:
            raise MarkerError('after break')
    except MarkerError:
        return {'outer_handler': True}
    return {'outer_handler': False}


with TestClient(app) as client:
    response = client.get('/nested-try-loop')
    print(response.status_code, response.json())
