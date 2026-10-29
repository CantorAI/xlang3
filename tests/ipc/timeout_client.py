"""Keep the caller alive after repeated timeouts, then reuse the same slots."""
import os
import sys
import time

endpoint = 'lrpc:' + sys.argv[1]
import ipc_srv as srv thru endpoint
delayed = srv.delayed_echo
cancel = srv.wait_for_cancellation
payload = b'frame' * 40000
for i in range(40):
    os.environ['XLANG3_LRPC_TIMEOUT_MS'] = '150'
    try:
        delayed(str(os.getpid()) + ':' + str(i), payload)
        raise AssertionError('delayed call unexpectedly completed')
    except RuntimeError as error:
        assert 'timed out' in str(error), str(error)
os.environ['XLANG3_LRPC_TIMEOUT_MS'] = '3000'
time.sleep(0.8)
assert srv.echo('after timeouts') == 'echo:after timeouts'
assert srv.bytes_echo(payload) == payload, 'late response overwrote a newer call'
before = srv.cancellation_count()
os.environ['XLANG3_LRPC_TIMEOUT_MS'] = '150'
try:
    cancel()
    raise AssertionError('cancellable call unexpectedly completed')
except RuntimeError as error:
    assert 'timed out' in str(error), str(error)
os.environ['XLANG3_LRPC_TIMEOUT_MS'] = '3000'
time.sleep(0.1)
assert srv.cancellation_count() > before, 'running handler did not observe cancellation'
assert srv.add(20, 22) == 42
print('timeout-recovery-passed')
