# Instance/class attribute dispatch trial (2026-10-10)

## Change

`attribute_get` now sends `InstanceObject` and `ClassObject` values directly to
the same `object_get_attr` implementation before probing unrelated builtin
kinds. The lookup, descriptor, and exception implementation is unchanged. The
comment in `src/runtime/attribute.cpp` records why this dispatch order is on a
hot path. No Python standard-library implementation was moved into C++.

## Evidence

The fixed Release build completed and the complete fixture suite passed. The
diagnostic used 20,000 warmed calls over classes from pyperformance's official
`typing_runtime_protocols` definitions. It is a runtime profile, not an
official score. `inspect.getattr_static(instance, "a")` changed from 441.8 ms
to 420.4 ms (0.952x candidate/parent), and the missing-member case changed
from 437.9 ms to 423.3 ms (0.967x). `inspect._check_instance` changed from
43.29 ms to 38.99 ms (0.901x). In CPython 3.14.7 those same rows took 23.16,
23.39, and 2.50 ms, respectively.

The official pyperformance 1.14 `typing_runtime_protocols --fast` run gave
2.76 ± 0.07 ms for the candidate, 2.80 ± 0.08 ms for its immediate parent,
and 125 ± 5 µs for CPython 3.14.7. `pyperf compare_to` classified the
candidate/parent result as **not significant**. XLang3 remains about 22x slower
than CPython on this case. Do not claim a pyperformance win from this trial.

The required full 11-case Release gate passes against the immediate parent.
Against the accepted fixed baseline, ten cases pass, but `gc_traversal` is a
confirmed 1.142x slowdown. The same case is 0.995x against the immediate
parent, so this trial did not introduce that existing cumulative regression.
The failed fixed-baseline gate means this candidate is not ready to commit.

## August and October protocol comparison

The October 1 full-run table recorded `typing_runtime_protocols` at 0.2503 ms
while the runtime protocol-specific native fast check was present. Commit
`f9bc1773` removed that implementation on October 7; its code comment says
Python-defined hooks, including `typing.Protocol`, must execute their Python
body. That is required by the project rule keeping CPython pure-Python library
behavior in Python. The later October 9 all-97 run measured 2.948 ms; this
trial measures 2.76 ms. The timing change closely follows the removed
special-case implementation and is the main identified cause of this case's
roughly 12x jump. Restoring that C++ member loop would violate the rule. The
remaining work is to make generic VM/runtime execution of the existing Python
hook cheaper.

August-era local-loop and function-call results are separate microbenchmarks,
not the full pyperformance comparison. The preserved August-vs-current
throughput report records the October current runtime 15.53x faster than the
August build on its `function_calls` workload. That does not erase broad
stdlib-heavy pyperformance gaps or the benchmark-coverage limitations of the
August build.

## Artifacts

- [Diagnostic candidate](data/instance-class-attribute-dispatch-profile-candidate-20261010.json),
  [parent](data/instance-class-attribute-dispatch-profile-parent-20261010.json),
  and [CPython 3.14.7](data/instance-class-attribute-dispatch-profile-cpython3147-20261010.json).
- Official pyperformance 1.14 results: [candidate](data/typing-runtime-protocols-attribute-dispatch-candidate-20261010.json),
  [parent](data/typing-runtime-protocols-attribute-dispatch-parent-20261010.json),
  and [CPython 3.14.7](data/typing-runtime-protocols-attribute-dispatch-cpython3147-20261010.json).
- Full [immediate-parent gate](data/instance-class-attribute-dispatch-parent-gate-20261010.json)
  and [fixed-baseline gate](data/instance-class-attribute-dispatch-fixed-gate-20261010.json).
- The broader 97-case comparison remains the October 9
  [full report](pyperformance-xlang3-python-new-r4-vs-cpython3147-full-fast-20261009.md).

The full performance goal remains active. Next, target generic Python function
entry and nested calls from runtime builtins; do not recreate
`typing._ProtocolMeta.__instancecheck__` in native code.
