# Command-benchmark hook failure in the shared-dependency run

**Source diagnosis; command-level confirmation and replacement measurements
are still pending.** The currently running full comparison keeps its original
compatibility directory unchanged.

The fresh CPython 3.14.7 full run attempted all 97 definitions, completed 92,
and produced 116 subtest timings. Five definitions failed: `2to3`,
`django_template`, `python_startup`, `python_startup_no_site`, and `sympy`.
The Django and SymPy tracebacks explicitly report missing `distutils` in the
shared dependency versions. The other three use pyperf's command timer and
report only `Command failed with exit code 1`.

The original `benchmarks/diagnostics/pyperf_compat/sitecustomize.py` eagerly
imports `pyperf._runner` and `pyperf._worker` to disable Windows priority and
host metadata hooks. It runs in descendant Python processes too.

The installed pyperf `_command.py` invokes `_process_time.py` in a child
interpreter. At entry, `_process_time.py` rejects execution when `pyperf` is
already in `sys.modules`. The eager sitecustomize import therefore contradicts
that explicit guard. `_command.py` captures combined child output but discards
it when reporting a nonzero exit, which explains why the full-suite log does
not expose the guard's error text.

This diagnosis is directly supported by the installed source. Reproduce the
guard failure with the original directory and the same `_process_time.py`
command after the full run finishes before labeling the diagnosis confirmed.

The separate candidate
`benchmarks/diagnostics/pyperf_compat_command_safe/sitecustomize.py` skips
pyperf imports for the command timer and minimal `-c`/stdin entry points.
Actual benchmark scripts still get the identical priority/metadata changes.
The timer runs unchanged. Its measured command runs unchanged. In particular,
the fix must not time eager pyperf imports as part of `python -c pass` startup.

After the live pair finishes:

1. Confirm the original helper exits at its pyperf-import guard.
2. Verify the candidate helper runs without a pyperf import and verify the
   ordinary worker priority/metadata hooks still apply.
3. Run the official three command benchmarks on CPython and XLang3 using the
   candidate directory, the same dependencies, mode and time cap on both sides.
4. Keep the original full-run failures and raw files. Publish repaired command
   results with separate identities and compatibility provenance; do not
   overwrite the original full-run evidence or invent missing timings.

The original [CPython raw JSON](data/pyperformance-cpython314-native-iocp-shared-deps-full-fast-20261001.json),
[full log](data/pyperformance-cpython314-native-iocp-shared-deps-full-fast-20261001.log)
and [paired run manifest](data/pyperformance-native-iocp-shared-deps-run-manifest-20261001.json)
remain the evidence for the initial comparison.
