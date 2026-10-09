"""One small Coverage operation, with no timer or benchmark loop."""
import sys

sys.path.insert(0, 'D:/CantorAI/xlang3/venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')
import coverage

assert sys.version_info[:3] == (3, 14, 7)
assert sys.gettrace() is None and sys.getprofile() is None

def diagnostic_excepthook(kind, value, traceback):
    sys.settrace(None)
    print('UNCAUGHT_KIND', getattr(kind, '__name__', '<unknown>'))
    print('UNCAUGHT_VALUE_TYPE', type(value).__name__)
    print('UNCAUGHT_IS_EXCEPTION', isinstance(value, BaseException))
    print('UNCAUGHT_VALUE_REPR', repr(value))
    print('UNCAUGHT_TRACEBACK_TYPE', type(traceback).__name__)

sys.excepthook = diagnostic_excepthook

def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

cov = coverage.Coverage()
stage = 'start'
try:
    cov.start()
    stage = 'fibonacci'
    value = fibonacci(3)
    stage = 'stop'
    cov.stop()
    print('PASS', value, 'TRACE_DISABLED', sys.gettrace() is None)
    assert value == 2 and sys.gettrace() is None
except BaseException as error:
    sys.settrace(None)
    print('CAUGHT_STAGE', stage)
    print('CAUGHT_TYPE', type(error).__name__)
    print('CAUGHT_REPR', repr(error))
    print('CAUGHT_IS_EXCEPTION', isinstance(error, BaseException))
    raise
