"""Observe the default Coverage backend; no benchmark workload or timing."""
import json
import os
import sys

sys.path.insert(0, 'D:/CantorAI/xlang3/venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages')
import coverage

assert sys.version_info[:3] == (3, 14, 7)
cov = coverage.Coverage()
cov.start()
backend = type(cov._collector.tracers[0])
cov.stop()
native_module = sys.modules.get('coverage.tracer')
print(json.dumps({'implementation': sys.implementation.name, 'version': list(sys.version_info[:3]),
    'coverage_version': coverage.__version__, 'coverage_file': coverage.__file__,
    'backend_module': backend.__module__, 'backend_name': backend.__name__,
    'native_module_file': getattr(native_module, '__file__', None),
    'core_override': os.environ.get('COVERAGE_CORE'), 'timid': cov.config.timid,
    'trace_disabled_after_stop': sys.gettrace() is None}, sort_keys=True))
