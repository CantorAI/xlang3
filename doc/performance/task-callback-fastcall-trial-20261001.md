# Asyncio Task callback fast-call trial (2026-10-01)

**Status: rejected; no repeatable pyperformance gain.** The implementation was
removed after a paired official benchmark measured the candidate 3% slower and
the initial broad dispatch exposed a network regression.

## Hypothesis from CPython 3.14.7

CPython's native `_asyncio` implementation uses a specialized
`TaskStepMethWrapper` for Task step/wakeup callbacks rather than routing each
transition through a generic Python bound-method call. In XLang3,
`Context.run` invokes a Task callback from C++ through `runtime_call_callable`;
that generic helper prepends `self` in a heap-backed `std::vector<Value>` and
calls the native function's ordinary callback, even when it has a fast-call
adapter. The source reference is
[`_asynciomodule.c` at CPython 3.14.7](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c).

The trial registered fast adapters for `_asyncio.Task._step`,
`_eager_step`, and `_wakeup`, then dispatched these known internal symbols from
the C++ callable path using the existing argument array and a small stack index
array. A first, unrestricted version applied this to every native bound method;
the Release network integration test then stalled on the large-response case.
The preserved pre-change executable passed that test. Restricting dispatch to
the three Task symbols restored the network test and passed
`tests/native/asyncio_accelerator.py`.

## Official benchmark

The exact `async_tree_eager` case was run with pyperformance 1.14.0 in `--fast`
mode, CPython 3.14.7's shared dependency site, and the command-safe Windows
shim. `pyperf compare_to` found the candidate significantly slower:

| Variant | Mean |
|---|---:|
| Preserved control | 1.79 s ± 0.03 s |
| Task callback fast-call candidate | 1.83 s ± 0.05 s |
| Candidate/control | 1.03× slower |

Both runs warn about limited sample stability. The measured direction does not
support keeping the added dispatch branch or adapters, so both were removed.
The comparison does not establish that the Task callback path explains the
roughly 21× full-run gap; the larger costs remain in the general Python
execution path.

Raw evidence: [control JSON](data/async-tree-task-callback-fastcall-targeted-control-fast-20261001.json),
[candidate JSON](data/async-tree-task-callback-fastcall-targeted-fast-20261001.json),
[control log](data/async-tree-task-callback-fastcall-targeted-control-fast-20261001.log),
and [candidate log](data/async-tree-task-callback-fastcall-targeted-fast-20261001.log).

After removing the experiment, the Release tree passed **55/55 CTest tests**
and the full default **11-case fixed-baseline gate**. The post-rollback report
is [here](data/asyncio-task-fastcall-rollback-fixed-baseline-20261001.json).
