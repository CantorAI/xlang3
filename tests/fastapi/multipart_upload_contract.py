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

# Cross the standard 1 MiB spooling boundary; small in-memory uploads cannot
# exercise tempfile's native descriptor metadata and asynchronous disk I/O.
large_body = b'upload-data-' * 120000
large_upload = client.post(
    '/upload',
    data={'label': 'spooled'},
    files={'attachment': ('large.txt', large_body, 'text/plain')},
    headers={'Authorization': 'Bearer allowed'},
)
large_result = large_upload.json()
print('spooled', large_upload.status_code, large_result.get('label'),
      large_result.get('filename'), large_result.get('body', '').encode() == large_body)
