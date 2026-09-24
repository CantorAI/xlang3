import asyncio
import socket

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/dual-stack")
async def dual_stack():
    server = await asyncio.start_server(lambda reader, writer: None, host=None, port=0)
    try:
        return {
            "ipv6_only": socket.IPV6_V6ONLY,
            "families": sorted(int(sock.family) for sock in server.sockets),
        }
    finally:
        server.close()
        await server.wait_closed()


with TestClient(app) as client:
    response = client.get("/dual-stack")
    assert response.status_code == 200
    assert response.json() == {"ipv6_only": socket.IPV6_V6ONLY, "families": [2, 23]}
    print(response.json())
