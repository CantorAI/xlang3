import sys
import threading


previous_interval = sys.getswitchinterval()
sys.setswitchinterval(100.0)
try:
    ready = threading.Event()
    go = threading.Event()
    ran = []

    def worker():
        ready.set()
        go.wait()
        ran.append(True)

    thread = threading.Thread(target=worker)
    thread.start()
    ready.wait()
    go.set()
    iterator = iter(range(10000))
    for _ in range(10000):
        next(iterator)
    print("worker-ran-during-next-loop", bool(ran))
finally:
    sys.setswitchinterval(previous_interval)

thread.join()
print("worker-finished", not thread.is_alive())
