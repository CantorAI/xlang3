from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import TypeAdapter


app = FastAPI()


@app.get('/recursive-alias')
def recursive_alias():
    Json = list['Json']
    try:
        TypeAdapter(Json)
    except RecursionError as error:
        return {
            'error': type(error).__name__,
            'hint': any(
                'implicit recursive type alias' in note
                for note in getattr(error, '__notes__', ())
            ),
        }
    return {'error': 'missing'}


response = TestClient(app).get('/recursive-alias')
print(response.status_code, response.json())
