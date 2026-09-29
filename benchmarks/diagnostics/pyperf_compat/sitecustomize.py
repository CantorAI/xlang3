"""Skip unsupported Windows priority/metadata hooks in cross-runtime pyperf.

Both XLang3 and CPython 3.14 must run with the same hooks disabled so the
timed benchmark body and pyperf worker protocol remain the comparison target.
"""
try:
    import pyperf._runner as _runner
    import pyperf._worker as _worker
except ImportError:
    pass
else:
    _runner.Runner._process_priority = lambda self: None
    _worker.WorkerTask.collect_metadata = lambda self: {}
    _worker.WorkerProcessTask.collect_metadata = lambda self: {}
