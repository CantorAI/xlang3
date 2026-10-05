import _thread
import threading


class Node:
    def __init__(self, depth):
        self.remaining = depth

    @property
    def depth(self):
        if self.remaining == 0:
            return 0
        return 1 + getattr(Node(self.remaining - 1), 'depth')


results = []
finished = threading.Event()


def worker():
    results.append(Node(20).depth)
    finished.set()


thread = threading.Thread(target=worker)
thread.start()
thread.join(timeout=10)
assert not thread.is_alive()
assert results == [20]
finished.clear()
_thread.start_new_thread(worker, ())
assert finished.wait(timeout=10)
assert results == [20, 20]
print('nested worker properties passed')
