import errno
import os
import signal
import threading
import time


number = signal.SIGUSR1
main_ident = threading.get_ident()
seen = []
threads = []


def received(signum, frame):
    seen.append(signum)
    threads.append(threading.get_ident())


previous_handler = signal.signal(number, received)
read_fd, write_fd = os.pipe()
previous_fd = -1
try:
    try:
        signal.set_wakeup_fd(write_fd)
    except ValueError:
        print("blocking-fd-rejected", True)
    else:
        print("blocking-fd-rejected", False)
    os.set_blocking(read_fd, False)
    os.set_blocking(write_fd, False)
    previous_fd = signal.set_wakeup_fd(write_fd)
    worker = threading.Thread(target=lambda: os.kill(os.getpid(), number))
    worker.start()
    worker.join()
    for _ in range(1000):
        if seen:
            break
        time.sleep(0.001)
    print("kernel-main-thread", seen == [number], threads == [main_ident])
    print("kernel-wakeup-byte", os.read(read_fd, 1) == bytes([number]))

    signal.signal(number, signal.SIG_IGN)
    os.kill(os.getpid(), number)
    time.sleep(0.01)
    try:
        os.read(read_fd, 1)
    except BlockingIOError:
        print("kernel-ignore", seen == [number])
    else:
        print("kernel-ignore", False)

    def failed(signum, frame):
        raise RuntimeError("signal-callback")

    signal.signal(number, failed)
    try:
        signal.raise_signal(number)
    except RuntimeError as exc:
        print("callback-error", str(exc) == "signal-callback")
    else:
        print("callback-error", False)
    try:
        signal.raise_signal(-1)
    except OSError as exc:
        print("invalid-raise", type(exc) is OSError, exc.errno == errno.EINVAL)
finally:
    signal.set_wakeup_fd(previous_fd)
    signal.signal(number, previous_handler)
    os.close(read_fd)
    os.close(write_fd)
print("handler-restored", signal.getsignal(number) == previous_handler)
