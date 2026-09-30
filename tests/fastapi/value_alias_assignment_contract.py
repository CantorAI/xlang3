from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/classify')
def classify(value: str) -> dict[str, str]:
    url = str(value)
    url = url.lstrip()
    if ':' in url and not url.lower().startswith('http'):
        return {'kind': 'other', 'url': url}
    return {'kind': 'http', 'url': url}


with TestClient(app) as client:
    for address in ('http://example.com/', '  https://example.com/', 'data:text/plain,hello'):
        response = client.get('/classify', params={'value': address})
        print(response.status_code, response.json())
