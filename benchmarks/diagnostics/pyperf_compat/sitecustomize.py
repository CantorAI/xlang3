"""Skip unsupported Windows priority/metadata hooks in cross-runtime pyperf.

Both XLang3 and CPython 3.14 must run with the same hooks disabled so the
timed benchmark body and pyperf worker protocol remain the comparison target.
"""
import sys

# pyperf's _process_time helper intentionally requires a clean interpreter and
# exits if its own package is imported at startup. Do not apply these hooks in
# that nested timer process; its child command does not need pyperf metadata.
_is_process_timer = bool(sys.argv) and sys.argv[0].replace("\\", "/").endswith(
    "/pyperf/_process_time.py")
# pyperformance measures startup by launching ``xlang3 -c pass``. That child
# never creates pyperf workers, so importing pyperf here would charge its full
# import cost to the interpreter-startup score instead of measuring XLang3.
_is_startup_probe = sys.argv == ["-c"]
if not _is_process_timer and not _is_startup_probe:
    try:
        import pyperf._runner as _runner
        import pyperf._worker as _worker
    except ImportError:
        pass
    else:
        _runner.Runner._process_priority = lambda self: None
        _worker.WorkerTask.collect_metadata = lambda self: {}
        _worker.WorkerProcessTask.collect_metadata = lambda self: {}
