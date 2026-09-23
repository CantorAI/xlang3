import asyncio
import json
from typing import Annotated

from fastapi import Depends, FastAPI, Header, HTTPException, Query


app = FastAPI()


@app.middleware('http')
async def runtime_header(request, call_next):
    response = await call_next(request)
    response.headers['x-runtime'] = 'xlang3'
    return response


async def require_token(x_token: Annotated[str, Header()]) -> str:
    if x_token != 'allow':
        raise HTTPException(status_code=403, detail='denied')
    return x_token


@app.get('/items/{item_id}')
async def read_item(
    item_id: int,
    limit: Annotated[int, Query(ge=1)] = 3,
    token: str = Depends(require_token),
) -> dict:
    return {'item_id': item_id, 'limit': limit, 'token': token}


async def request(
    path: str,
    query: bytes = b'',
    headers: list[tuple[bytes, bytes]] | None = None,
) -> tuple[int, dict]:
    messages: list[dict] = []
    request_sent = False

    async def receive() -> dict:
        nonlocal request_sent
        if request_sent:
            return {'type': 'http.disconnect'}
        request_sent = True
        return {'type': 'http.request', 'body': b'', 'more_body': False}

    async def send(message: dict) -> None:
        messages.append(message)

    scope = {
        'type': 'http',
        'asgi': {'version': '3.0', 'spec_version': '2.3'},
        'http_version': '1.1',
        'method': 'GET',
        'scheme': 'http',
        'path': path,
        'raw_path': path.encode('ascii'),
        'query_string': query,
        'root_path': '',
        'headers': [(b'host', b'testserver')] + (headers or []),
        'client': ('127.0.0.1', 50000),
        'server': ('testserver', 80),
        'state': {},
    }
    await app(scope, receive, send)
    start = next(message for message in messages if message['type'] == 'http.response.start')
    assert dict(start['headers'])[b'x-runtime'] == b'xlang3'
    response_body = b''.join(
        message.get('body', b'') for message in messages if message['type'] == 'http.response.body'
    )
    return start['status'], json.loads(response_body)


async def main() -> None:
    status, body = await request('/items/7', b'limit=2', [(b'x-token', b'allow')])
    assert status == 200
    assert body == {'item_id': 7, 'limit': 2, 'token': 'allow'}

    status, body = await request('/items/7', b'limit=2')
    assert status == 422
    assert body['detail'][0]['loc'] == ['header', 'x-token']
    assert body['detail'][0]['msg'] == 'Field required'

    status, body = await request('/items/not-an-int', headers=[(b'x-token', b'allow')])
    assert status == 422
    assert body['detail'][0]['type'] == 'int_parsing'
    assert body['detail'][0]['loc'] == ['path', 'item_id']

    status, body = await request('/items/7', headers=[(b'x-token', b'deny')])
    assert status == 403
    assert body == {'detail': 'denied'}

    print('fastapi-framework-features-ok')


asyncio.run(main())
