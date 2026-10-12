# Native TimerHandle comparison cache trial — 2026-10-11

## Result

The class-owned guarded slot/double comparison cache reduced the median of the
same diagnostic from **0.315 s to 0.270 s** for 300,000 comparisons per sample
(9 samples each), a **14.2% local improvement** over the saved pre-cache
XLang3 Release control. CPython 3.14.7 measured **0.050 s**, leaving the
candidate **5.45× slower** on this focused probe. This is a real but narrow
win; it does not materially close the full pyperformance gap.

The benchmark is `benchmarks/diagnostics/native_callback_slot_compare_bench.py`.
It exercises `asyncio.TimerHandle.__lt__` in the VM and is diagnostic evidence,
not an official pyperformance score. The source fixture
`tests/fixtures/core/native_callback_slot_compare.py` covers normal ordering,
`NotImplemented`, subclass and descriptor fallback, `__code__` replacement,
method replacement, global rebinding, and shadowed `isinstance`. Its expected
output matches both the fixed XLang3 executable and CPython 3.14.7.

## Full-suite candidate attempt

The fixed Release candidate full-fast attempt was interrupted at case 16 of 97
so the callback-path work could be measured directly. It recorded 15 completed
definitions before `async_tree_io`; `async_tree_eager_io` and
`async_tree_eager_io_tg` each hit the configured 300-second timeout. Because
the runner was stopped before normal completion, no final pyperf JSON was
produced. The complete log is retained at
[`pyperformance-xlang3-builtin-global-cache-full-fast-20261011-r1.log`](data/pyperformance-xlang3-builtin-global-cache-full-fast-20261011-r1.log).
Do not treat that interrupted attempt as a full-suite result.

The async I/O timeout therefore remains unresolved. The comparator cache only
removes a narrow exact `__lt__` body after class, module, code, descriptor,
and builtin guards pass; it does not remove the much larger task creation,
resume, attribute, and ownership costs in the async workload. Continue profiling
those paths before attempting another full-suite candidate run.

## Reproduction and provenance

Raw sequential A/B samples and SHA-256 identities are in
[`native-callback-slot-double-compare-ab-20261011-r1.json`](data/native-callback-slot-double-compare-ab-20261011-r1.json).

- Candidate fixed executable: `500e5cabfebbaad6ac797cf7244e417cb85f9d687e7369c1af3c9be57d256ac4`.
- Candidate runtime DLL: `b4e16b0ee173972e2a97b14cb190e7daa3e47bcbbdca26d56e9c8575f668b43f`.
- Pre-cache XLang3 control executable: `be0e00d5875d5887b1c7b9345066b82e9dda450c75fd62aff8614823a1b7f97b`.
- Pre-cache XLang3 control runtime DLL: `e4c8ace7413dc3782ac62513733ee664d3394bd942e666a126e9201711758249`.
- CPython executable: `4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9` (Python 3.14.7).

CPython 3.14.7's specialized `LOAD_GLOBAL_BUILTIN` uses namespace-version
guards plus a cached dictionary index, avoiding repeated name lookup in the hot
opcode ([`bytecodes.c`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L1667)). XLang3's builtin cache currently still looks up the
`builtins` module by name on each hit. That is a separate candidate optimization
and has not yet been shown to improve the full suite.
