"""Reproduce SSLObject clean-EOF behavior with the same source on both runtimes.

This is a correctness diagnostic for the native _ssl path used by
pyperformance's asyncio_tcp_ssl case, not a timing benchmark.
"""

import pathlib
import ssl


CERTS = pathlib.Path(__file__).resolve().parents[2] / "tests" / "native" / "ssl"


def make_pair():
    client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client_context.check_hostname = False
    client_context.verify_mode = ssl.CERT_NONE
    client_context.maximum_version = ssl.TLSVersion.TLSv1_2
    server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server_context.maximum_version = ssl.TLSVersion.TLSv1_2
    server_context.load_cert_chain(str(CERTS / "localhost-cert.pem"),
                                   str(CERTS / "localhost-key.pem"))
    client_in, client_out = ssl.MemoryBIO(), ssl.MemoryBIO()
    server_in, server_out = ssl.MemoryBIO(), ssl.MemoryBIO()
    client = client_context.wrap_bio(client_in, client_out,
                                     server_hostname="localhost")
    server = server_context.wrap_bio(server_in, server_out, server_side=True)
    client_done = server_done = False
    for _ in range(20):
        if not client_done:
            try:
                client.do_handshake()
                client_done = True
            except ssl.SSLWantReadError:
                pass
        data = client_out.read()
        if data:
            server_in.write(data)
        if not server_done:
            try:
                server.do_handshake()
                server_done = True
            except ssl.SSLWantReadError:
                pass
        data = server_out.read()
        if data:
            client_in.write(data)
        if client_done and server_done:
            return client, server, client_in, client_out, server_in, server_out
    raise AssertionError("MemoryBIO TLS handshake did not finish")


client, server, client_in, client_out, server_in, server_out = make_pair()
assert client.write(b"hello") == 5
server_in.write(client_out.read())
assert server.read(5) == b"hello"
try:
    client.unwrap()
except ssl.SSLWantReadError:
    pass
server_in.write(client_out.read())
assert server.read(4) == b""
buffer = bytearray(b"keep")
assert server.read(4, buffer) == 0
assert buffer == b"keep"
assert server.read(4) == b""
server.unwrap()
client_in.write(server_out.read())
client.unwrap()
print("clean EOF: bytes, buffer, repeated read, shutdown passed")

client, server, client_in, client_out, server_in, server_out = make_pair()
server_in.write_eof()
try:
    server.read(4)
except ssl.SSLEOFError:
    print("abrupt EOF: SSLEOFError preserved")
else:
    raise AssertionError("abrupt EOF incorrectly accepted as clean shutdown")
