from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic import AnyUrl, BaseModel, HttpUrl, PostgresDsn
from datetime import datetime, timedelta, timezone, tzinfo
from pydantic_core import MultiHostUrl, TzInfo, Url


class Connection(BaseModel):
    endpoint: HttpUrl
    upstream: AnyUrl
    database: PostgresDsn


app = FastAPI()


@app.post("/connections", response_model=Connection)
async def connection(value: Connection) -> Connection:
    return value


with TestClient(app) as client:
    response = client.post(
        "/connections",
        json={
            "endpoint": "HTTP://User:pw@Example.COM:80/api?q=hello+world&q=2#frag",
            "upstream": "ftp://Example.COM/file",
            "database": "postgres://u:p@db1:5432,db2:5433/app?ssl=1",
        },
    )
    print(response.status_code, response.json())

    invalid = client.post(
        "/connections",
        json={
            "endpoint": "ftp://example.com",
            "upstream": "x:test",
            "database": "postgres://db/app",
        },
    )
    detail = invalid.json()["detail"][0]
    print(invalid.status_code, detail["type"], tuple(detail["loc"]))

model = Connection(
    endpoint="https://www.аррӏе.com/search?q=hello+world",
    upstream="ftp://puny£code.com/file",
    database="postgres://u:p@db1:5432,db2:5433/app?ssl=1",
)
print(ascii(str(model.endpoint)), ascii(model.endpoint.unicode_string()))
print(model.endpoint.query_params(), model.upstream.port, model.database.hosts())

url = Url.build(
    scheme="https", username="u", password="p", host="example.com",
    port=8443, path="x", query="a=1", fragment="f",
)
multi = MultiHostUrl.build(
    scheme="postgres",
    hosts=[{"username": "u", "password": "p", "host": "a", "port": 1}, {"host": "b"}],
    path="db",
)
print(str(url), repr(url), url.host, url.port, url.query_params())
print(str(multi), repr(multi), multi.hosts())

zone = TzInfo(19800)
print(str(zone), repr(zone), zone.utcoffset(None), zone.tzname(None), zone.dst(None), isinstance(zone, tzinfo))
print(zone == timezone(timedelta(hours=5, minutes=30)), datetime(2026, 1, 2, 3, 4, tzinfo=zone).isoformat())
