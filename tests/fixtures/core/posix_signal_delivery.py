"""Real kernel signals, VM-thread delivery, selector wakeup, and dispositions."""
import asyncio
import _signal
import errno
import os
import select
import signal
import socket
import subprocess
import sys
import threading
import time
import warnings


if os.name == 'posix':
    seen = []
    main_ident = threading.get_ident()

    def handler(signum, frame):
        assert threading.get_ident() == main_ident
        seen.append(signum)

    old = signal.signal(signal.SIGTERM, handler)
    try:
        os.kill(os.getpid(), signal.SIGTERM)
        deadline = time.monotonic() + 3
        while not seen and time.monotonic() < deadline:
            pass
        assert seen == [signal.SIGTERM], seen
        seen.clear()
        worker = threading.Thread(target=lambda: signal.raise_signal(signal.SIGTERM))
        worker.start()
        worker.join(timeout=3)
        deadline = time.monotonic() + 3
        while not seen and time.monotonic() < deadline:
            pass
        assert seen == [signal.SIGTERM] and not worker.is_alive(), seen
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        os.kill(os.getpid(), signal.SIGTERM)
        signal.raise_signal(signal.SIGTERM)
        assert seen == [signal.SIGTERM]
        _signal.signal(signal.SIGTERM, False)
        assert signal.getsignal(signal.SIGTERM) == signal.SIG_DFL
        _signal.signal(signal.SIGTERM, True)
        assert signal.getsignal(signal.SIGTERM) == signal.SIG_IGN
        for invalid in (None, 'handler', 9, object()):
            try:
                signal.signal(signal.SIGTERM, invalid)
            except TypeError:
                pass
            else:
                raise AssertionError('invalid handler accepted')
        assert signal.getsignal(signal.SIGTERM) == signal.SIG_IGN
        for uncatchable in (signal.SIGKILL, signal.SIGSTOP):
            try:
                signal.signal(uncatchable, handler)
            except OSError as exc:
                assert exc.errno == errno.EINVAL
            else:
                raise AssertionError('uncatchable signal handler accepted')
        for invalid in (None, 1.0, 'yes'):
            try:
                signal.siginterrupt(signal.SIGTERM, invalid)
            except TypeError:
                pass
            else:
                raise AssertionError('invalid restart flag accepted')
        for invalid in (-1, signal.NSIG):
            try:
                signal.siginterrupt(invalid, False)
            except ValueError:
                pass
            else:
                raise AssertionError('invalid restart signal accepted')
        restarted = []
        worker = threading.Thread(target=lambda: restarted.append(signal.siginterrupt(signal.SIGTERM, False)))
        worker.start()
        worker.join(timeout=3)
        assert not worker.is_alive() and restarted == [None]
    finally:
        signal.signal(signal.SIGTERM, old)

    async def selector_wakeup():
        loop = asyncio.get_running_loop()
        result = loop.create_future()

        def receive():
            assert threading.get_ident() == main_ident
            if not result.done():
                result.set_result('delivered')

        loop.add_signal_handler(signal.SIGUSR1, receive)
        worker = threading.Thread(target=lambda: (time.sleep(0.1), os.kill(os.getpid(), signal.SIGUSR1)))
        worker.start()
        try:
            assert await asyncio.wait_for(result, 3) == 'delivered'
        finally:
            worker.join(timeout=3)
            assert loop.remove_signal_handler(signal.SIGUSR1)

    left, right = socket.socketpair()
    interrupts = []
    old = signal.signal(signal.SIGUSR1, lambda signum, frame: interrupts.append(signum))
    def interrupt_select():
        for _ in range(8):
            time.sleep(0.03)
            os.kill(os.getpid(), signal.SIGUSR1)
    worker = threading.Thread(target=interrupt_select)
    worker.start()
    try:
        started = time.monotonic()
        assert select.select([left], [], [], 0.30) == ([], [], [])
        elapsed = time.monotonic() - started
        worker.join(timeout=3)
        assert 0.24 <= elapsed < 0.46, elapsed
        assert len(interrupts) == 8, interrupts
    finally:
        worker.join(timeout=3)
        signal.signal(signal.SIGUSR1, old)
        left.close()
        right.close()

    def raise_from_signal(signum, frame):
        raise RuntimeError('select signal exception')
    old = signal.signal(signal.SIGUSR1, raise_from_signal)
    worker = threading.Thread(target=lambda: (time.sleep(0.05), os.kill(os.getpid(), signal.SIGUSR1)))
    worker.start()
    try:
        started = time.monotonic()
        try:
            select.select([], [], [], None)
        except RuntimeError as exc:
            assert str(exc) == 'select signal exception'
        else:
            raise AssertionError('signal exception was swallowed')
        assert time.monotonic() - started < 1.0
    finally:
        worker.join(timeout=3)
        signal.signal(signal.SIGUSR1, old)

    asyncio.run(selector_wakeup())
    reader, writer = socket.socketpair()
    try:
        try:
            signal.set_wakeup_fd(writer.fileno())
        except ValueError:
            pass
        else:
            raise AssertionError('blocking wakeup descriptor accepted')
        reader.setblocking(False)
        writer.setblocking(False)
        old = signal.signal(signal.SIGTERM, handler)
        old_fd = signal.set_wakeup_fd(writer.fileno(), warn_on_full_buffer=False)
        try:
            os.kill(os.getpid(), signal.SIGTERM)
            # Keep the wakeup-byte check independent of the existing native
            # IntEnum-to-bytes iterable conversion gap (also in the baseline).
            assert reader.recv(1) == bytes([int(signal.SIGTERM)])
            while True:
                try:
                    writer.send(b'x' * 4096)
                except BlockingIOError:
                    break
            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter('always')
                signal.raise_signal(signal.SIGTERM)
                assert not caught
                signal.set_wakeup_fd(writer.fileno(), warn_on_full_buffer=True)
                signal.raise_signal(signal.SIGTERM)
                assert len(caught) == 1 and caught[0].category is RuntimeWarning
        finally:
            signal.set_wakeup_fd(old_fd)
            signal.signal(signal.SIGTERM, old)
    finally:
        reader.close()
        writer.close()

    child = subprocess.run([sys.executable, '-B', '-c',
        'import signal; signal.signal(signal.SIGTERM, signal.SIG_DFL); signal.raise_signal(signal.SIGTERM); raise AssertionError("default did not terminate")'], timeout=10)
    assert child.returncode == -signal.SIGTERM, child.returncode
    old = signal.signal(signal.SIGTERM, signal.SIG_IGN)
    try:
        child = subprocess.run([sys.executable, '-B', '-c',
            'import signal; assert signal.getsignal(signal.SIGTERM) == signal.SIG_IGN; signal.raise_signal(signal.SIGTERM)'], timeout=10)
        assert child.returncode == 0, child.returncode
    finally:
        signal.signal(signal.SIGTERM, old)

print('POSIX kernel signal delivery: ok')
