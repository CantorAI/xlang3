import errno
import os
import signal
import subprocess
import sys

if os.name == 'posix':
    assert os.kill(os.getpid(), 0) is None
    for function in (os.killpg,):
        for args, expected in (((), TypeError), (('bad', 0), TypeError),
                               ((1 << 100, 0), OverflowError),
                               ((os.getpid(), 1 << 100), OverflowError)):
            try:
                function(*args)
            except expected:
                pass
            else:
                raise AssertionError('invalid signal arguments accepted')
    child = subprocess.Popen([sys.executable, '-B', '-c',
                              'import time; time.sleep(60)'], start_new_session=True)
    try:
        assert os.killpg(child.pid, 0) is None
        assert os.killpg(child.pid, signal.SIGTERM) is None
        assert child.wait(timeout=10) == -signal.SIGTERM
        try:
            os.killpg(child.pid, 0)
        except OSError as exc:
            assert exc.errno == errno.ESRCH
        else:
            raise AssertionError('dead process group still exists')
    finally:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=10)
print('process signal contracts: ok')
