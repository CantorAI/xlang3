# Subparser object-hash type-slot correction

## Finding

The `argparse_subparsers` workload produced 22,719 `BoundMethod` values in an
unprofiled counter run. 6,516 came from hashing `_StoreAction` objects. The
runtime found each object's `__hash__` through ordinary instance attribute
lookup, bound the inherited `object.__hash__`, and dispatched that temporary
method. CPython hashes through the type's hash slot; assigning an instance
attribute named `__hash__` does not replace that slot.

`runtime_value_hash_key` now checks the type-level `__hash__` first. It uses the
identity-hash implementation directly for the inherited native
`object.__hash__`, rejects a type-level `None`, and keeps the regular call path
for custom hash methods. The code comment records why this fast path is valid.
The `object_hash_identity_fastpath` fixture covers inherited hashing, an
instance-level shadow, custom hashing, and an unhashable class.

## Measurements

| Measurement | Control | Candidate | Result |
|---|---:|---:|---|
| Official pyperformance `argparse_subparsers`, fast | 293 ms ± 36 ms | 282 ms ± 32 ms | Not statistically significant (`pyperf compare_to` hid the case) |
| Official pyperformance `argparse_subparsers`, rigorous | 292 ms ± 38 ms | 293 ms ± 36 ms | Not statistically significant (`pyperf compare_to` hid the case) |

The official measurements used pyperformance 1.14.0 with the Python 3.13
compatibility standard library available on this host. These numbers do not
show a speedup from this change. Do not count this as a performance win.

The candidate passed the isolated 41-pair `subparsers` fixed-baseline gate
against `scratch/performance/baseline-0336992/xlang3.exe` (candidate / baseline
median ratio 0.874; 95% paired interval 0.869–0.884). That comparison includes
all accumulated changes since the older baseline and does not attribute a gain
to this hash correction. The compatible full gate was then run with the
repository's Python 3.13 `copy.py` overlay: ten cases passed with 41 pairs each,
and `list_append` passed a separate 101-pair rerun (ratio 0.997; interval
0.954–1.013). The first attempt omitted that overlay and its `deepcopy_memo`
failure was a test setup error, not a runtime blocker. Complete reports:
[`current-release-fixed-baseline-r41-20261002.json`](data/current-release-fixed-baseline-r41-20261002.json)
and
[`current-release-list-append-fixed-baseline-r101-20261002.json`](data/current-release-list-append-fixed-baseline-r101-20261002.json).

## Evidence

- `data/subparsers-bound-method-profile-20261002.txt` — unprofiled allocation
  attribution.
- `data/subparsers-object-hash-control-fast-20261002.json`
- `data/subparsers-object-hash-candidate-fast-20261002.json`
- `data/subparsers-object-hash-control-rigorous-20261002.json`
- `data/subparsers-object-hash-candidate-rigorous-20261002.json`
- `data/subparsers-object-hash-fixed-baseline-r41-20261002.json` — targeted
  gate result.
- `data/subparsers-object-hash-fixed-baseline-full-r41-20261002.json` — initial
  full-gate attempt without the required compatibility overlay; this failure
  is superseded by the successful overlay-enabled reports above.

The code change is retained as a CPython semantics correction with a regression
fixture. It is not retained as a claimed optimization; further work should
target a change with measurable end-to-end benefit.
