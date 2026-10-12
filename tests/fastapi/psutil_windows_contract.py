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
    current_process = psutil.Process(os.getpid())
    current_memory = current_process.memory_info()
    heaps = psutil.heap_info()
    memory = psutil.virtual_memory()
    disks = psutil.disk_io_counters(perdisk=True)
    return {
        "version": psutil.__version__,
        "has_loopback": any(
            address.family == socket.AF_INET and address.address == "127.0.0.1"
            for address in addresses
        ),
        "current_pid_exists": psutil.pid_exists(os.getpid()),
        "current_process_create_time_positive": current_process.create_time() > 0,
        "current_process_cpu_times_nonnegative": all(
            value >= 0 for value in current_process.cpu_times()
        ),
        "logical_cpu_count_positive": psutil.cpu_count() > 0,
        "native_logical_cpu_count_matches_psutil": (
            psutil._psutil_windows.cpu_count_logical() == psutil.cpu_count()
        ),
        "current_process_memory_positive": (
            current_memory.rss > 0 and current_memory.vms > 0
        ),
        "native_process_memory_info_has_ten_fields": (
            len(psutil._psutil_windows.proc_memory_info(os.getpid())) == 10
        ),
        "pid_zero_exists": psutil.pid_exists(0),
        "negative_pid_exists": psutil.pid_exists(-1),
        "heap_fields": list(heaps._fields),
        "heap_nonnegative": all(value >= 0 for value in heaps),
        "memory_total_positive": memory.total > 0,
        "memory_available_bounded": 0 <= memory.available <= memory.total,
        "disk_counters_present": bool(disks),
        "disk_counters_nonnegative": all(
            all(value >= 0 for value in counter) for counter in disks.values()
        ),
    }


print(TestClient(app).get("/system").json())
