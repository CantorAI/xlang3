# Scaled async-tree native profile (2026-10-05)

## Finding

A user-mode native IP sample of 100 four-level, three-branch async trees
produced 334 samples (231 in `xlang3_runtime.dll`). The symbolized sample counts
put `xlang3::release(Value)` at 20, `Value::operator=` at 12,
`Interpreter::run_function` at 11, the MSVC string hash at 10,
`object_get_attr` at 8, `XlangVMSmallBuffer<Value, 64>` construction at 5, and
`XlangVMFrame::clear_for_pop` at 5. This is a useful direction toward Value
ownership traffic and frame setup/cleanup; it does not establish precise time
shares because the sample is small and suspends the running thread.

This profile also confirmed that XLang3 already has an inline-coroutine frame
path in `xlang_vm_ops_async.h`. It pushes a fresh, exact, unaliased child
coroutine onto the current VM frame stack when tracing and monitoring are
inactive, and falls back to `generator_send` for other cases. Therefore, a
generic Call/Await fusion or a second copy of the existing frame-push shortcut
is not the next candidate. The next useful measurement is the hit rate of
this guarded path and then the cost of Value ownership operations on the same
workload.

CPython 3.14.7's native Task step calls `PyIter_Send` for its coroutine, whose
exact generator path enters the evaluator with the saved frame. See the
[CPython 3.14.7 Task implementation](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c#L2880-L2934)
and [generator send implementation](https://github.com/python/cpython/blob/v3.14.7/Objects/genobject.c#L174-L265).
For the official full benchmark, XLang3 measured 4.44 s on
`async_tree_none`, against CPython 3.14.7 at 226 ms; see the
[full pyperformance comparison](pyperformance-xlang3-sparse-instr-cache-vs-cpython314-fast-20261005.md).

## Reproduction and limits

The diagnostic command ran `async_tree_scaled.py --levels 4 --branches 3
--iterations 100` under the Release-with-debug-info executable, while
`sample_native_windows.py` sampled at 2 ms intervals. The script-reported
elapsed time was 1.499 s while sampling, so it is not a performance score and
must not be compared to an unprofiled CPython timing.

Python used for the command was **3.14.7**. The XLang3 executable SHA-256 was
`C12CC2915D49E9753523C25100699698517BA65BE2FF927B013C9F2810959FC6`; the
runtime DLL SHA-256 was
`A0CD91786EFCFB1DFCFF693695C5ACBA96DF740D79271F2625C04B48460E7F81`.

- [Raw native sample JSON](data/async-tree-scaled-native-samples-20261005.json)
- [Sampler output](data/async-tree-scaled-native-samples-20261005.log)
- [Symbolized sample output](data/async-tree-scaled-native-symbols-20261005.txt)

No runtime change was retained from this profile. Any ownership optimization
must preserve thread safety and object finalization, then pass the fixture
suite, interpreter tests, the complete fixed Release gate, and official
pyperformance A/B measurements.
