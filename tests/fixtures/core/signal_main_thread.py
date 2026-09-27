import signal
import threading


def check_worker():
    results = []
    for operation in (
        lambda: signal.signal(signal.SIGINT, signal.getsignal(signal.SIGINT)),
        lambda: signal.set_wakeup_fd(-1),
    ):
        try:
            operation()
        except Exception as exc:
            results.append(type(exc).__name__)
        else:
            results.append("accepted")
    return results


results = []
worker = threading.Thread(target=lambda: results.extend(check_worker()))
worker.start()
worker.join()
print(results)
