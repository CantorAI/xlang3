# Native `_asyncio.FutureIter.send` trial (2026-10-06)

## Result

Rejected. I added an exact-class route from `await_op` to XLang3's native
`_asyncio.FutureIter.send`, aiming to avoid bound-method construction and
generic callable dispatch on Future wakeups. A class-version guard and an
instance `send` shadow check kept mutable forms on normal dispatch. This
respected the implementation boundary: `_asyncio` is native in both runtimes;
no pure-Python standard-library module changed.

The candidate passed seven focused asyncio fixtures. In a three-case fast
pyperformance run, `async_tree_eager` first appeared **1.02× faster** and
`pyperf compare_to` showed that one case. The other two cases were
insignificant. I repeated `async_tree_eager` back-to-back on fixed Release and
candidate binaries; the second pair was also insignificant, so the initial
movement was not repeatable and the code was reverted.

| Benchmark | Fixed Release control | Candidate first run | Candidate / control | Saved CPython 3.14.7 |
|---|---:|---:|---:|---:|
| `async_tree_none` | 3.66 s ± 0.05 s | 3.67 s ± 0.04 s | 1.00× | 227 ms |
| `async_tree_eager` | 1.32 s ± 0.05 s | 1.29 s ± 0.04 s | 1.02× on first pair; repeat insignificant | 86.6 ms |
| `coroutines` | 134 ms ± 1 ms | 137 ms ± 12 ms | 0.98×, insignificant | 17.9 ms |

The back-to-back eager repeat was **1.31 s ± 0.04 s** on control and
**1.30 s ± 0.04 s** on candidate; pyperf hid it as not significant.

```text
Elapsed time, bars grow left to right; shorter is faster
async_tree_none   control ████████████████████████████████████ 3.66 s
                  trial   ████████████████████████████████████ 3.67 s
async_tree_eager  control ████████████████                     1.32 s
                  first   ████████████████                     1.29 s (1.02×)
                  repeat  ████████████████                     1.30 s (ns)
coroutines        control █████████████████                    134 ms
                  trial   █████████████████                    137 ms (ns)
```

The saved CPython values are the existing full-run reference, not newly paired
measurements. The native boundary is still worth keeping in mind when the
await implementation is next redesigned, but this particular per-send shortcut
has no repeatable pyperformance gain. Do not re-add it without a new profile
that shows bound-method resolution or generic native-call dispatch dominates.

## Validation and provenance

The candidate passed `asyncio_runtime_edges`, `async_taskgroup_current_task`,
`asyncio_native_future_constructor`, `asyncio_native_task_constructor`,
`asyncio_future_new_module_rebind`, `asyncio_scheduled_tasks_add_patch`, and
`asyncio_task_init_override`. The full fixture runner in this environment
stops earlier at `ssl_timed_socket`, because Python 3.14.7 lacks `_ssl`.

Both runs used Python **3.14.7**, pyperformance 1.14.0, the repository's
Windows compatibility shim, and `--mode fast`. The repeated pair ran
`async_tree_eager` immediately control then candidate.

- [First three-case control](data/pyperformance-xlang3-coroutine-chain-control-fast-20261006.json)
- [First three-case candidate](data/pyperformance-xlang3-futureiter-send-candidate-fast-20261006.json)
- [Repeated eager control](data/pyperformance-xlang3-futureiter-send-control-r2-fast-20261006.json)
- [Repeated eager candidate](data/pyperformance-xlang3-futureiter-send-candidate-r2-fast-20261006.json)

The fixed Release executable/runtime SHA-256 values were
`A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF` and
`BC1B9C0A8086F7E6FB0C037516DC9C1EEA20427FA887E3AA623714BC5EF5DA8D`.
The trial runtime DLL was
`06A507491D1DF1CCF57E878C0E922337D687818A396FD127507215330B1EE13D`.
