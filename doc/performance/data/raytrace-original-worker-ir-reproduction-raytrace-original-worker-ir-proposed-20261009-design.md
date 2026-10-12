One original raytrace worker IR observation (proposed, unexecuted)

Root must finish and authenticate accepted runtime restoration and choose a quiet
window. This adapter launches the fixed Release executable directly on byte-exact
C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_raytrace/run_benchmark.py.
The original source is not copied, rewritten, wrapped, or imported under another
name. Original pyperf worker arguments select exactly one body:
bench_raytrace(1, 100, 100, None), --worker-task 0, --values 1, --warmups 0.

Public route: src/xlang3.cpp:484-500 dumps ir::dump_module; run_source:755-805
lowers the original entry, dumps before Interpreter.run, then executes the same
module. -m ignores dump_ir at884-888, so importing from a separate Python child
would not supply the original module's VM IR. This proposal uses no native helper.

The worker and one original render exercise/warm the normal original dot call
sites. The dump is immutable compile IR emitted BEFORE warmup. It exposes neither
adaptive cache kinds nor optimized frame counts. Source-based eligibility is
separate: current ops_call.h requires empty explicit arguments for the small-self
route; inline_support rejects functions with params.size()!=1. Vector.dot has
self and other. No claim that every dot call creates a frame is established by
this observation alone. Trace/profile/debug/profiling options are absent, a fresh
worker is used, inherited Python/XLang instrumentation environment is cleared,
and the only site hook is the existing pinned priority/metadata compatibility
hook. Actual worker gettrace/getprofile state is not directly observed.

The controller validates one output value, zero warmups, loops1 and100x100,
extracts the uniquely qualified Vector.dot/line51/two-parameter function block,
and records stdout/stderr/whole IR/pyperf JSON hashes, selected source and pyperf
package inputs, and full178-file Release maps before/after. Worker stderr normally
contains the CLI 'debug: wrote IR' message. Pyperf elapsed output is retained raw
but is unscored: no CP comparison, CPU share or gain is calculated. This is not a
full dependency/source provenance audit or substitute for optimization eligibility.

Root invocation (replace placeholders with freshly authenticated restored hashes):
C:/Python/Python314/python.exe -I scratch/performance/observe-raytrace-original-worker-ir-proposed-20261009.py --exe-sha256 EXE_SHA --dll-sha256 DLL_SHA

Outputs: scratch/performance/raytrace-original-worker-ir-20261009/receipt.json,
run_benchmark.ir.txt, Vector.dot.ir.txt, worker.pyperf.json, stdout.log, stderr.log.
No AST, Python/runtime, build, benchmark or compiler helper execution by author.
