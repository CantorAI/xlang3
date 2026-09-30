import json
from typing import Annotated

from fastapi import Depends, FastAPI, File, Form, UploadFile
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from fastapi.testclient import TestClient


app = FastAPI()
bearer = HTTPBearer()


@app.post('/upload')
async def upload(
    label: Annotated[str, Form()],
    attachment: Annotated[UploadFile, File()],
    credentials: Annotated[HTTPAuthorizationCredentials, Depends(bearer)],
):
    body = await attachment.read()
    return {
        'label': label,
        'filename': attachment.filename,
        'content_type': attachment.content_type,
        'body': body.decode('utf-8'),
        'token': credentials.credentials,
    }


client = TestClient(app)
files = {'attachment': ('note.txt', b'hello multipart', 'text/plain')}
authorized = client.post(
    '/upload',
    data={'label': 'sample'},
    files=files,
    headers={'Authorization': 'Bearer allowed'},
)
print('authorized', authorized.status_code, json.dumps(authorized.json(), sort_keys=True))

unauthorized = client.post('/upload', data={'label': 'sample'}, files=files)
print('unauthorized', unauthorized.status_code, json.dumps(unauthorized.json(), sort_keys=True))

missing_file = client.post(
    '/upload', data={'label': 'sample'}, headers={'Authorization': 'Bearer allowed'}
)
print('missing_file', missing_file.status_code, json.dumps(missing_file.json(), sort_keys=True))
