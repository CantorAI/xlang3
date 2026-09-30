import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

xlang3 = Path(sys.argv[1]).resolve()
production = Path(sys.argv[2]).resolve()
test_packages = Path(sys.argv[3]).resolve()
server = Path(__file__).with_name("uvicorn_websocket_end_to_end.py")
sys.path[:0] = [str(test_packages), str(production)]

from wsproto import ConnectionType, WSConnection
from wsproto.events import AcceptConnection, CloseConnection, Request, TextMessage


def exchange(port, message):
    connection = WSConnection(ConnectionType.CLIENT)
    with socket.create_connection(("127.0.0.1", port), timeout=5) as channel:
        channel.settimeout(5)
        channel.sendall(connection.send(Request(host=f"127.0.0.1:{port}", target="/echo")))
        accepted = False
        answer = ""
        close_code = None
        while close_code is None:
            data = channel.recv(65536)
            if not data:
                raise AssertionError("WebSocket closed before close frame")
            connection.receive_data(data)
            for event in connection.events():
                if isinstance(event, AcceptConnection):
                    accepted = True
                    channel.sendall(connection.send(TextMessage(data=message)))
                elif isinstance(event, TextMessage):
                    answer += event.data
                elif isinstance(event, CloseConnection):
                    close_code = event.code
                    channel.sendall(connection.send(event.response()))
        assert accepted and answer == message.upper() and close_code == 1000, (
            accepted, len(answer), close_code
        )


with socket.socket() as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]

environment = os.environ.copy()
environment["PYTHONPATH"] = os.pathsep.join([str(test_packages), str(production)])
with tempfile.TemporaryFile() as stdout_file, tempfile.TemporaryFile() as stderr_file:
    process = subprocess.Popen(
        [str(xlang3), str(server), str(port)], cwd=server.parent,
        env=environment, stdout=stdout_file, stderr=stderr_file,
    )
    try:
        deadline = time.monotonic() + 30
        while True:
            if process.poll() is not None:
                raise RuntimeError(f"WebSocket server exited with {process.returncode}")
            try:
                exchange(port, "Hello, XLang3")
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)
        exchange(port, "Abc123" * 10000)
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        if process.returncode not in (0, 1):
            stderr_file.seek(0)
            print(stderr_file.read().decode("utf-8", errors="replace"), file=sys.stderr)

print("fastapi-uvicorn-websocket-ok")
