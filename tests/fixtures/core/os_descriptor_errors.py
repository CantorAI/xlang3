import errno
import os
import signal


def error_matches(call, expected_class, expected_errno):
    try:
        call()
    except OSError as exc:
        return (
            type(exc) is expected_class
            and exc.errno == expected_errno
            and exc.args == (exc.errno, exc.strerror)
            and isinstance(exc.strerror, str)
            and exc.filename is None
        )
    return False


print("invalid-descriptor-metadata", all([
    error_matches(lambda: os.read(-1, 1), OSError, errno.EBADF),
    error_matches(lambda: os.readinto(-1, bytearray(1)), OSError, errno.EBADF),
    error_matches(lambda: os.write(-1, b"x"), OSError, errno.EBADF),
    error_matches(lambda: os.close(-1), OSError, errno.EBADF),
]))

if os.name == "posix":
    read_fd, write_fd = os.pipe()
    try:
        os.set_blocking(read_fd, False)
        os.set_blocking(write_fd, False)
        print("nonblocking-read-metadata", all([
            error_matches(lambda: os.read(read_fd, 1), BlockingIOError, errno.EAGAIN),
            error_matches(lambda: os.readinto(read_fd, bytearray(1)), BlockingIOError, errno.EAGAIN),
        ]))
        full = False
        for _ in range(8192):
            try:
                os.write(write_fd, b"x" * 4096)
            except OSError:
                full = True
                break
        print("nonblocking-write-metadata", full and error_matches(
            lambda: os.write(write_fd, b"x" * 4096), BlockingIOError, errno.EAGAIN))
    finally:
        os.close(read_fd)
        os.close(write_fd)

    previous = signal.signal(signal.SIGPIPE, signal.SIG_IGN)
    read_fd, write_fd = os.pipe()
    os.close(read_fd)
    try:
        print("broken-pipe-metadata", error_matches(
            lambda: os.write(write_fd, b"x"), BrokenPipeError, errno.EPIPE))
    finally:
        os.close(write_fd)
        signal.signal(signal.SIGPIPE, previous)
