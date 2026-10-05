"""Apply comparison-only pyperf hooks without breaking command benchmarks.

The command timer deliberately refuses to run if pyperf is already imported.
Importing pyperf from sitecustomize also adds its import cost to the child
startup workload. Leave both the timer and minimal -c/-S startup children alone.
Benchmark scripts still disable the same Windows priority/metadata hooks.

This candidate compatibility directory is separate from the directory used by
the live paired full-suite run. Validate and use it for both runtimes together;
do not change that live run's hook implementation in place.
"""
import os
import sys

_entry = sys.argv[0] if sys.argv else ""
_minimal_command = _entry in ("", "-c", "-")
_command_timer = os.path.basename(_entry).lower() == "_process_time.py"
if not _minimal_command and not _command_timer:
    try:
        import pyperf._runner as _runner
        import pyperf._worker as _worker
    except ImportError:
        pass
    else:
        _runner.Runner._process_priority = lambda self: None
        _worker.WorkerTask.collect_metadata = lambda self: {}
        _worker.WorkerProcessTask.collect_metadata = lambda self: {}
