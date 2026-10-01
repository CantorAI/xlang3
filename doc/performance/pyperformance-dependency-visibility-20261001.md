# Benchmark dependency visibility (2026-10-01)

The current all-97 XLang3 run is an attempt to measure every definition, but
some import failures come from a different Python package search path than the
saved CPython reference. They cannot all be counted as runtime incompatibility.
This finding does not change any timing or failure in the original run.

The saved reference is
[`pyperformance-cpython314-full-fast-gc-root-20260929.json`](data/pyperformance-cpython314-full-fast-gc-root-20260929.json).
Its nonempty `python_executable` run metadata identifies the common environment
`venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Scripts/python.exe`.
That environment still exists. Its site-packages contains `websockets` 11.0.3,
Chameleon 4.6.0, Django 3.2.4, and the other optional suite dependencies.
The running XLang3 harness uses host site-packages, with the compatibility shim
on `PYTHONPATH`; it does not include this reference environment's site-packages.
For example, `asyncio_websockets` fails with `ModuleNotFoundError` for
`websockets` in the
[unchanged full-run log](data/pyperformance-xlang3-set-regex-candidate-full-fast-20261001.log).

The harness now accepts repeatable `--dependency-site` arguments. These append
an explicitly selected existing site-packages directory after the compatibility
hooks and inherited `PYTHONPATH`. The directory must exist and is printed in
the run log. No package installation is needed to use the saved environment.

This uses Python package sources. XLang3's native package search on Windows
does not include `.pyd` files, and `_imp.create_dynamic`/`exec_dynamic` raise
`ImportError`. Native package imports use XLang3's package interface. A package
that needs an unavailable native component can still fail; sharing dependency
sources is not evidence that its native component is supported.
The relevant implementation is in
[`native_package_loader.cpp`](../../src/import/native_package_loader.cpp) and
[`imp_module.cpp`](../../src/runtime/modules/system/imp_module.cpp).

After the unchanged all-97 attempt completes, verify the recovered imports
under XLang3 and run the affected official definitions with this directory:

```text
--dependency-site venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages
```

Keep both attempts and distinguish dependency import failures, runtime errors,
and timeouts. The per-definition timeout covers calibration and all worker
processes; it is not a measured steady-state duration. Use sufficient time
for calibration on slow definitions before concluding that they cannot run.
The WebSocket definition subsequently recovered a measurement after sharing
these sources and fixing a generic lazy keyword-call lookup: 531 +/- 50 ms
for XLang3 versus 186 +/- 10 ms for CPython. The
[IOCP report](windows-iocp-native-queue-20261001.md) preserves the initial import
failures, the module-call failures, and the successful rerun separately.

The comparison generator also now maps the official subtest names
`many_optionals` and `subparsers` to `argparse` and `argparse_subparsers`.
Regenerating the previous complete run verified both status rows and preserved
its 40 matched subtests, 2 faster/38 slower counts, and 0.13614x geometric ratio.
An XLang3 subtest without a CPython timing is retained without inventing a ratio.

Harness argument validation was checked with the CPython 3.14 host used for
the suite: `--help` includes the new option, and a nonexistent dependency
directory exits with argparse status 2 before starting a benchmark. The shell's
default `python` uses a different environment without pyperformance; use
`C:/Python/Python314/python.exe` for this harness on the recorded host.

## Full-suite follow-up caps

The harness also accepts repeatable `--case-timeout-override PATTERN=SECONDS`
arguments. Patterns match the current official benchmark definition; the last
match wins. This changes only the parent process's full-case deadline. It does
not change benchmark source, workload size, loops, warmups, or pyperf samples.
Each applied timeout is printed before the runner records the failed definition.

The next full CPython/XLang3 pair uses the shared dependency site, fast mode,
and a 600-second general cap. `--case-timeout-override 'async_tree*=120'`
retains a shorter cap for the tree definitions because a previous XLang3 worker
used about 3.3 GB after 90 seconds without returning a timing. Both runtimes
receive the same caps. These capped definitions remain in the complete status
list, with their failures and no invented speed ratios.

The override was exercised with a one-second `fann*` smoke cap: the log names
`fannkuch`, records the one-second deadline, and reports the expected timeout.
That is a harness smoke test, not a performance result:
[output](data/pyperformance-timeout-override-smoke-20261001.log).
