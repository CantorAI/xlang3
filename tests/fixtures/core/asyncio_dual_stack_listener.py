import asyncio
import socket


async def main():
    server = await asyncio.start_server(lambda reader, writer: None, host=None, port=0)
    try:
        print(socket.IPV6_V6ONLY)
        print(sorted(int(sock.family) for sock in server.sockets))
    finally:
        server.close()
        await server.wait_closed()


asyncio.run(main())
