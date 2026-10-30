"""Kill only owned test clients; the same Linux listener must remain usable."""
import ctypes
import mmap
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def wait_marker(marker, process):
    deadline = time.monotonic() + 5
    while not marker.exists():
        if time.monotonic() >= deadline or process.poll() is not None:
            raise RuntimeError("Owned test process did not reach its marker")
        time.sleep(0.001)


def stop_owned(process):
    if process.poll() is None:
        process.kill()
    process.wait(timeout=5)


def hold_mutex(port, marker):
    # The first region field is the opaque process-shared pthread mutex.
    # This helper never touches the slots or condition implementation.
    with open("/dev/shm/xlang3_lrpc_" + port, "r+b") as stream:
        region = mmap.mmap(stream.fileno(), 0)
        address = ctypes.addressof(ctypes.c_char.from_buffer(region))
        lock = ctypes.CDLL(None).pthread_mutex_lock
        lock.argtypes = [ctypes.c_void_p]
        lock.restype = ctypes.c_int
        assert lock(address) == 0
        Path(marker).write_text("locked")
        time.sleep(60)  # The parent deliberately kills this lock owner.


def main():
    executable = sys.argv[1]
    root = Path(__file__).resolve().parent
    port = str(10000000 + os.getpid())
    shared = Path("/dev/shm/xlang3_lrpc_" + port)
    while shared.exists():
        port = str(int(port) + 1)
        shared = Path("/dev/shm/xlang3_lrpc_" + port)
    with tempfile.TemporaryDirectory(prefix="xlang3-client-death-") as folder:
        folder = Path(folder)
        ready = folder / "ready"
        server = subprocess.Popen(
            [executable, "-B", str(root / "dead_client_server.py"), port, str(ready)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            wait_marker(ready, server)
            for iteration in range(64):
                marker = folder / str(iteration)
                client = subprocess.Popen(
                    [executable, "-B", str(root / "dead_client.py"), port, str(marker)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                )
                try:
                    wait_marker(marker, client)
                    time.sleep(0.003)
                finally:
                    stop_owned(client)
                subprocess.run(
                    [executable, "-B", str(root / "dead_client.py"), port],
                    check=True, timeout=5, capture_output=True,
                )
            marker = folder / "mutex-owner"
            owner = subprocess.Popen([sys.executable, str(__file__), "--hold-mutex", port, str(marker)])
            try:
                wait_marker(marker, owner)
            finally:
                stop_owned(owner)
            subprocess.run(
                [executable, "-B", str(root / "dead_client.py"), port],
                check=True, timeout=5, capture_output=True,
            )
            subprocess.run(
                [executable, "-B", str(root / "dead_client.py"), port, "--shutdown"],
                check=True, timeout=5, capture_output=True,
            )
            assert server.wait(timeout=5) == 0, "Listener teardown failed"
            assert not shared.exists(), "Graceful teardown left its shared region"
            print("client-death-and-mutex-owner-recovery-passed")
        finally:
            stop_owned(server)
            # This unique endpoint belonged only to the Popen-owned server.
            shared.unlink(missing_ok=True)


if __name__ == "__main__":
    if sys.argv[1] == "--hold-mutex":
        hold_mutex(sys.argv[2], sys.argv[3])
    else:
        main()
