import _thread
import threading


finished = [threading.Event() for _ in range(3)]


def worker(event):
    event.set()


for event in finished:
    _thread.start_new_thread(worker, (event,))

for event in finished:
    assert event.wait(5)

print("daemon-workers-finished")
