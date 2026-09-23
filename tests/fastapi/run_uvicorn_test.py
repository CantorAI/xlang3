import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import time
from urllib.error import URLError
from urllib.request import Request, urlopen


xlang3 = Path(sys.argv[1]).resolve()
site_packages = Path(sys.argv[2]).resolve()
script = Path(__file__).with_name("uvicorn_end_to_end.py")
certificate = Path(__file__).parents[1] / "native" / "ssl" / "localhost-cert.pem"
private_key = Path(__file__).parents[1] / "native" / "ssl" / "localhost-key.pem"

environment = os.environ.copy()
environment["PYTHONPATH"] = os.pathsep.join(
    [str(site_packages), environment.get("PYTHONPATH", "")]
).rstrip(os.pathsep)


def exercise_server(scheme: str) -> None:
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]

    command = [str(xlang3), str(script), str(port)]
    if scheme == "https":
        command.extend([str(certificate), str(private_key)])
    process = subprocess.Popen(
        command,
        cwd=script.parent,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        response = None
        last_error = None
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if process.poll() is not None:
                stdout, stderr = process.communicate()
                raise RuntimeError(
                    f"Uvicorn exited with {process.returncode}\nstdout:\n{stdout}\nstderr:\n{stderr}"
                )
            try:
                request = Request(
                    f"{scheme}://127.0.0.1:{port}/double",
                    data=json.dumps({"value": 21}).encode("utf-8"),
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                context = ssl._create_unverified_context() if scheme == "https" else None
                response = urlopen(request, timeout=1, context=context)
                break
            except (OSError, URLError) as error:
                last_error = error
                time.sleep(0.1)
        if response is None:
            process.terminate()
            stdout, stderr = process.communicate(timeout=5)
            raise RuntimeError(
                f"Uvicorn did not accept a {scheme} request: {last_error}\n"
                f"stdout:\n{stdout}\nstderr:\n{stderr}"
            )
        with response:
            assert response.status == 200
            assert json.loads(response.read()) == {"result": 42, "scheme": scheme}
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


exercise_server("http")
exercise_server("https")
print("fastapi-uvicorn-http-https-ok")
