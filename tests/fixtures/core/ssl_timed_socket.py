import pathlib
import socket
import ssl
import threading
import time


certs = pathlib.Path(__file__).resolve().parents[2] / 'native' / 'ssl'
certificate = certs / 'localhost-cert.pem'
key = certs / 'localhost-key.pem'

server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
server_context.load_cert_chain(str(certificate), str(key))
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
with socket.create_connection(('127.0.0.1', port), timeout=2.0) as connection:
    with client_context.wrap_socket(connection, server_hostname='localhost') as secured:
        secured.sendall(b'ping')
        print('timed-tls', secured.recv(4).decode('ascii'), secured.version())
thread.join()
print('server-errors', server_errors)
