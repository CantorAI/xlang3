"""Exercise the official psutil layer through a public FastAPI route."""

import os
import socket

import psutil
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/system")
def system():
    interfaces = psutil.net_if_addrs()
    addresses = [address for entries in interfaces.values() for address in entries]
    heaps = psutil.heap_info()
    disks = psutil.disk_io_counters(perdisk=True)
    return {
        "version": psutil.__version__,
        "has_loopback": any(
            address.family == socket.AF_INET and address.address == "127.0.0.1"
            for address in addresses
        ),
        "current_pid_exists": psutil.pid_exists(os.getpid()),
        "pid_zero_exists": psutil.pid_exists(0),
        "negative_pid_exists": psutil.pid_exists(-1),
        "heap_fields": list(heaps._fields),
        "heap_nonnegative": all(value >= 0 for value in heaps),
        "disk_counters_present": bool(disks),
        "disk_counters_nonnegative": all(
            all(value >= 0 for value in counter) for counter in disks.values()
        ),
    }


print(TestClient(app).get("/system").json())
