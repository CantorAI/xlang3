import errno


for number in (2, 3, 4, 11, 13, 17, 32, 10035, 10053, 10054, 10060, 10061):
    error = OSError(number, 'probe')
    print(number, type(error).__name__, error.errno, error.strerror)

for name, expected in (
    ('ENOENT', FileNotFoundError),
    ('ESRCH', ProcessLookupError),
    ('EINTR', InterruptedError),
    ('EAGAIN', BlockingIOError),
    ('EWOULDBLOCK', BlockingIOError),
    ('EALREADY', BlockingIOError),
    ('EINPROGRESS', BlockingIOError),
    ('EACCES', PermissionError),
    ('EPERM', PermissionError),
    ('EEXIST', FileExistsError),
    ('EPIPE', BrokenPipeError),
    ('ESHUTDOWN', BrokenPipeError),
    ('ECHILD', ChildProcessError),
    ('EISDIR', IsADirectoryError),
    ('ENOTDIR', NotADirectoryError),
    ('ECONNABORTED', ConnectionAbortedError),
    ('ECONNRESET', ConnectionResetError),
    ('ECONNREFUSED', ConnectionRefusedError),
    ('ETIMEDOUT', TimeoutError),
    ('EINVAL', OSError),
    ('EBADF', OSError),
):
    number = getattr(errno, name)
    error = OSError(number, 'native-probe')
    print('native-errno', name, type(error) is expected,
          error.errno == number, error.strerror == 'native-probe')
