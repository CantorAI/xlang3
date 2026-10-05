# Native `_pickle.loads` date-opcode fast path (2026-10-02)

The native `_pickle.loads` reader used to reject date-bearing protocol-4/5
pickle streams and hand them to Python `pickle.py`. The stream structure uses
`TUPLE1` for the `datetime.date` reducer arguments and may use `SETITEM` while
building dictionaries. The native reader now recognizes both structural
opcodes. GLOBAL and REDUCE remain restricted to the canonical `datetime.date`
class, so the fast path does not execute arbitrary reducers. Pure-Python
`pickle.py` remains Python; this extends XLang3's native `_pickle` counterpart.

The change was checked against the official `pyperformance 1.14.0`
[`bm_pickle/run_benchmark.py`](https://github.com/python/pyperformance/blob/1.14.0/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py)
`bench_unpickle` shape: each loop loads 20 times each of a dict with a date, a
tuple, and a group of three such dicts. The compact-data diagnostic measured
a **0.1597×** median paired ratio, but it did not use the upstream objects. A
second direct run now uses the exact upstream definitions and load loop; six
order-balanced Release pairs measured a **0.2491×** median paired
candidate/control ratio (**4.01× faster**). The exact-shape candidate median
was 4.93 ms per loop, versus 20.02 ms for the previous Release control. This
is not an official pyperf score. CPython 3.13 measured 222.9 µs per loop in the
same direct harness, leaving XLang3 about **22.1× slower** in this diagnostic.
The compact and exact-shape raw pairs and binary hashes are in
[`data/pickle-date-opcode-shape-trial-20261002.json`](data/pickle-date-opcode-shape-trial-20261002.json)
and [`data/pickle-date-opcode-exact-shape-20261002.json`](data/pickle-date-opcode-exact-shape-20261002.json).

A bounded vector-reserve trial was also discarded: six paired exact-shape runs\nmeasured a 1.0148× candidate/control ratio, 1.48% slower. The raw samples and\nruntime hashes are in\n[`data/pickle-date-opcode-reserve-trial-20261002.json`](data/pickle-date-opcode-reserve-trial-20261002.json).\n\nAn attempted per-load cache of the canonical date class showed no measurable
extra gain (median paired ratio **0.9939×**); it was discarded. The retained
optimization is the missing opcode coverage that lets the already-guarded
native decoder handle the payload.

The existing pickle fixture now checks date lists under protocols 4 and 5.
The focused fixture passes, and the Python 3.13 interoperability check passes.
The full fixture runner still stops at an unrelated existing
`runtime_protocol_fastcheck` output mismatch.
An opt-in `XLANG3_PICKLE_TRACE=1` run confirms that the date payload completes
native decoding; a mixed payload with a custom reducer is still rejected by
the native whitelist and falls back.

The complete fixed-baseline Release gate remains unverified. Its run stopped at
`deepcopy_memo` because the preserved baseline cannot load the available Python
3.13 `copy.py` (`bytearray.copy` is missing). A second gate against the older
pre-opcode control DLL stopped for the same reason. Both JSON reports are
preserved under `data/release-regression-pickle-opcodes-*.json`. The current
pyperformance environment is absent, so a focused official `unpickle` rerun and
the full official suite remain outstanding. The most recent saved full-suite
CPython comparison predates this change and measured XLang3 `unpickle` at
75.8 µs versus 10.6 µs on CPython 3.14.7. The direct diagnostic does not close
that gap.



