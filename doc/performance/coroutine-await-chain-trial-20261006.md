# Await-chain trampoline trial (2026-10-06)

## Result

Rejected. I extended the synchronous generator-delegation trampoline to
coroutine and `__await__`-iterator chains when the chain had no tracing,
monitoring, debug, or suspended exception-handler observers. Seven focused
async fixtures passed. A full fixture sweep stopped before the async cases at
`ssl_timed_socket` because this Python 3.14.7 installation has no `_ssl`
module; those seven async fixtures were then run directly and passed.

Paired pyperformance 1.14.0 fast runs show no significant change:

| Benchmark | Fixed Release control | Candidate | Candidate / control | Saved CPython 3.14.7 |
|---|---:|---:|---:|---:|
| `async_tree_none` | 3.66 s ± 0.05 s | 3.65 s ± 0.04 s | 1.00× | 227 ms |
| `async_tree_eager` | 1.32 s ± 0.05 s | 1.30 s ± 0.04 s | 1.02×, not significant | 86.6 ms |
| `coroutines` | 134 ms ± 1 ms | 139 ms ± 17 ms | 0.96×, not significant | 17.9 ms |

```text
Elapsed time, bars grow left to right; shorter is faster
async_tree_none   control ████████████████████████████████████ 3.66 s
                  trial   ████████████████████████████████████ 3.65 s
async_tree_eager  control ████████████████                     1.32 s
                  trial   ████████████████                     1.30 s (ns)
coroutines        control █████████████████                    134 ms
                  trial   ██████████████████                   139 ms (ns)
```

`pyperf compare_to` hid all three cases as insignificant. The CPython column
is the saved Python 3.14.7 full-run reference, not a newly paired run. The
remaining gaps are about 16.1× for `async_tree_none`, 15.0× for
`async_tree_eager`, and 7.8× for `coroutines` by elapsed time.

## Why this did not help

The proposed route had little overlap with the hot benchmark path. XLang3
already handles a fresh exact coroutine at `await` by pushing its bound frame
onto the active VM stack, which is the key `SEND`-style optimization. That
guarded path is documented beside `await_op` in
[`xlang_vm_ops_async.h`](../../src/executor/xlang_vm/ops/xlang_vm_ops_async.h).
The trampoline trial mostly affected coroutine objects that had already
suspended and retained an `awaiting` chain; the official workloads did not
benefit from it. The coroutine changes were reverted. Do not retry this
trampoline; any new async work should instrument or optimize the existing
inline-frame resume path instead of adding a parallel delegation mechanism.

## Reproduction and artifacts

Both runs used `C:\Python\Python314\python.exe` (Python 3.14.7), the same
pyperformance 1.14.0 environment and compatibility shim, and
`--case-timeout-override async_tree=600`. The fixed Release executable SHA-256
was `A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF`;
its runtime DLL was `BC1B9C0A8086F7E6FB0C037516DC9C1EEA20427FA887E3AA623714BC5EF5DA8D`.
The candidate used the same executable and a trial runtime DLL with SHA-256
`0AB01BC4CB97E82CD75711DCBE93A83F10807F2E90E08FCD2B2CC4A57C5A9D61`.

- [Fixed Release control pyperf data](data/pyperformance-xlang3-coroutine-chain-control-fast-20261006.json)
- [Candidate pyperf data](data/pyperformance-xlang3-coroutine-chain-candidate-fast-20261006.json)
