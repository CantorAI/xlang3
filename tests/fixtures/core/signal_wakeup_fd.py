import signal
import socket
import time


reader, writer = socket.socketpair()
reader.setblocking(False)
writer.setblocking(False)
seen = []
old_handler = signal.signal(signal.SIGINT, lambda signum, frame: seen.append(signum))
old_fd = signal.set_wakeup_fd(writer.fileno(), warn_on_full_buffer=False)
try:
    signal.raise_signal(signal.SIGINT)
    time.sleep(0.05)
    print(old_fd, reader.recv(1), seen)
finally:
    print(signal.set_wakeup_fd(old_fd, warn_on_full_buffer=True) == writer.fileno())
    signal.signal(signal.SIGINT, old_handler)
    reader.close()
    writer.close()
