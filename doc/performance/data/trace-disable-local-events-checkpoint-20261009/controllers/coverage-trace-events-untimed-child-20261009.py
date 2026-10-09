"""Instrument Python tracer events for one tiny, untimed Coverage operation."""
import sys

sys.path.insert(0, 'D:/CantorAI/xlang3/venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')
import coverage
from coverage.pytracer import PyTracer, THIS_FILE

assert sys.version_info[:3] == (3, 14, 7)
assert sys.gettrace() is None and sys.getprofile() is None
original_trace = PyTracer._trace
records = []

def diagnostic_trace(self, frame, event, arg, lineno=None):
    filename = frame.f_code.co_filename
    if THIS_FILE not in filename:
        records.append(('before', event, frame.f_code.co_name, frame.f_lineno, len(self.data_stack), filename))
    try:
        answer = original_trace(self, frame, event, arg, lineno)
    except BaseException as error:
        sys.settrace(None)
        records.append(('error', event, frame.f_code.co_name, frame.f_lineno, len(self.data_stack), type(error).__name__, repr(error)))
        for row in records:
            print('EVENT', repr(row))
        raise
    if THIS_FILE not in filename:
        records.append(('after', event, frame.f_code.co_name, frame.f_lineno, len(self.data_stack), answer is None))
    return answer

# Diagnostic-only Python instrumentation. Installed package bytes stay intact.
# timid=True selects the Python tracer on both runtimes, unlike the official
# default benchmark. No timing or official result is produced here.
PyTracer._trace = diagnostic_trace

def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

cov = coverage.Coverage(timid=True)
try:
    cov.start()
    value = fibonacci(3)
    cov.stop()
    assert value == 2
    print('PASS', value, 'TRACE_DISABLED', sys.gettrace() is None)
finally:
    sys.settrace(None)
    print('RECORD_COUNT', len(records))
    for row in records:
        print('EVENT', repr(row))
