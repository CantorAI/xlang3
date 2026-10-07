# Preserve partial pyperformance evidence from failed definitions

Pyperformance 1.14.0's `_benchmark._run_perf_script` creates a temporary JSON
output file, invokes the benchmark script, and raises on a nonzero exit before
loading that file into the suite. The temporary-file context then removes it.
A multi-subtest definition can therefore print several measured means and
still leave no corresponding raw samples in the final suite JSON when a later
worker times out.

The current subscription-dispatch all-97 run started with the original runner
from source checkpoint `23d1d443`. Its `base64` definition printed timings for
several subtests before reaching the 300-second limit. Those console means are
retained in the [runner log](data/pyperformance-xlang3-subscription-dispatch-full-fast-20261007.log).
They are rounded output, and must not be reconstructed as raw pyperf samples.
The definition remains failed. This preservation change is for subsequent
runs; it does not alter the already-loaded live runner or recover deleted
temporary files from this run.

The [runner](../../benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py)
now calls [the preservation helper](../../benchmarks/diagnostics/preserve_pyperformance_partial.py)
after a worker failure and cleanup, before upstream removes its temporary
output. Successful invocations perform no preservation work. Failed output
is copied byte for byte into `<suite-output-stem>-partial/`, with a separate
provenance file recording the failed definition, exit code, SHA-256, byte
count, JSON parse status, and any available measured-value counts. Calibration
and warmups do not contribute to those counts.

Truncated JSON and malformed schemas are retained as diagnostic evidence.
Syntactically valid JSON is not, by itself, proof of a usable pyperf result.
Partial files are never silently merged into the successful suite output,
and their existence never changes a failed definition into a completed one.
A future comparison that uses valid partial subtests must label them explicitly
and keep their failed-definition status and timing population visible.

The [offline validation record](data/partial-output-preservation-validation-20261007.json)
uses synthetic temporary files, not benchmark measurements. It checks byte
identity after upstream-style deletion, preserved failure status, measured
values counted separately from warmups, separation from the successful suite,
safe filenames, truncated/schema-invalid input, and missing-output handling.
No XLang3 process or benchmark worker was started for this validation.
