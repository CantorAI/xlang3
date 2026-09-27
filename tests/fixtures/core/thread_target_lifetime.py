import threading
import weakref


references = []


def start_and_join():
    thread = threading.Thread(target=lambda: None)
    reference = weakref.ref(thread)
    thread.start()
    thread.join()
    return reference


for _ in range(20):
    references.append(start_and_join())

print("threads", sum(reference() is not None for reference in references))
print("dangling", len(threading._dangling))
