import pathlib
import socket
import ssl
import threading
import time

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()
certs = pathlib.Path(__file__).resolve().parents[1] / 'native' / 'ssl'


@app.get('/timed-tls')
def timed_tls() -> dict:
    server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server_context.load_cert_chain(str(certs / 'localhost-cert.pem'),
                                   str(certs / 'localhost-key.pem'))
    client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client_context.check_hostname = False
    client_context.verify_mode = ssl.CERT_NONE

    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    port = listener.getsockname()[1]
    server_errors = []

    def serve():
        try:
            connection, _ = listener.accept()
            time.sleep(0.1)
            with server_context.wrap_socket(connection, server_side=True) as secured:
                request = secured.recv(4)
                secured.sendall(b'pong' if request == b'ping' else b'bad!')
        except Exception as exc:
            server_errors.append(type(exc).__name__)
        finally:
            listener.close()

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        with socket.create_connection(('127.0.0.1', port), timeout=2.0) as connection:
            with client_context.wrap_socket(connection, server_hostname='localhost') as secured:
                secured.sendall(b'ping')
                reply = secured.recv(4).decode('ascii')
                version = secured.version()
    finally:
        thread.join(timeout=3)
    return {'reply': reply, 'version': version, 'server_errors': server_errors}


with TestClient(app) as client:
    response = client.get('/timed-tls')
    print(response.status_code, response.json())
