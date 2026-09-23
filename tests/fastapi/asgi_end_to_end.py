import asyncio
import json

from fastapi import FastAPI
from pydantic import BaseModel, Field


class CreateItem(BaseModel):
    name: str
    quantity: int = Field(ge=1)


class ItemResult(BaseModel):
    name: str
    quantity: int
    accepted: bool


app = FastAPI()


@app.post('/items', response_model=ItemResult)
async def create_item(item: CreateItem) -> ItemResult:
    return ItemResult(name=item.name, quantity=item.quantity, accepted=True)

async def request(path: str, payload: object) -> tuple[int, dict, list[dict]]:
    body = json.dumps(payload).encode('utf-8')
    request_sent = False
    messages: list[dict] = []

    async def receive() -> dict:
        nonlocal request_sent
        if request_sent:
            return {'type': 'http.disconnect'}
        request_sent = True
        return {'type': 'http.request', 'body': body, 'more_body': False}

    async def send(message: dict) -> None:
        messages.append(message)

    scope = {
        'type': 'http',
        'asgi': {'version': '3.0', 'spec_version': '2.3'},
        'http_version': '1.1',
        'method': 'POST',
        'scheme': 'http',
        'path': path,
        'raw_path': path.encode('ascii'),
        'query_string': b'',
        'root_path': '',
        'headers': [(b'host', b'testserver'), (b'content-type', b'application/json')],
        'client': ('127.0.0.1', 50000),
        'server': ('testserver', 80),
        'state': {},
    }
    await app(scope, receive, send)
    start = next(message for message in messages if message['type'] == 'http.response.start')
    response_body = b''.join(
        message.get('body', b'') for message in messages if message['type'] == 'http.response.body'
    )
    return start['status'], json.loads(response_body), messages


async def main() -> None:
    schema = app.openapi()
    operation = schema['paths']['/items']['post']
    assert operation['requestBody']['content']['application/json']['schema'] == {
        '$ref': '#/components/schemas/CreateItem'
    }
    create_schema = schema['components']['schemas']['CreateItem']
    assert create_schema['required'] == ['name', 'quantity']
    assert create_schema['properties']['quantity']['minimum'] == 1.0
    result_schema = schema['components']['schemas']['ItemResult']
    assert result_schema['required'] == ['name', 'quantity', 'accepted']

    status, body, messages = await request('/items', {'name': 'gear', 'quantity': 2})
    assert status == 200
    assert body == {'name': 'gear', 'quantity': 2, 'accepted': True}
    assert messages[-1]['type'] == 'http.response.body'

    status, body, _ = await request('/items', {'name': 'gear', 'quantity': 0})
    assert status == 422
    assert body['detail'][0]['loc'] == ['body', 'quantity']

    status, body, _ = await request('/missing', {'name': 'gear', 'quantity': 2})
    assert status == 404
    assert body == {'detail': 'Not Found'}

    print('fastapi-asgi-ok')


asyncio.run(main())
