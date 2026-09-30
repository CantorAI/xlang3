import json

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


def raise_from_handler():
    try:
        raise ValueError('original')
    except ValueError as error:
        raise RuntimeError('wrapped') from error


@app.get('/except-target-cleanup')
def except_target_cleanup():
    try:
        raise_from_handler()
    except RuntimeError as caught:
        trace = caught.__traceback__
        while trace is not None and trace.tb_frame.f_code.co_name != 'raise_from_handler':
            trace = trace.tb_next
        return {'target_cleared': trace is not None and 'error' not in trace.tb_frame.f_locals}


response = TestClient(app).get('/except-target-cleanup')
print(response.status_code, json.dumps(response.json(), sort_keys=True))
