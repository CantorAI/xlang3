# Transient data strings (2026-10-30)

Runtime JSON values, embedding/IPC strings and several text-producing builtins
used the identifier-oriented `Value::string` / `Value::string_view` factories.
Those factories automatically intern alphanumeric strings. The global table
owns its entries; identifier-shaped values can also become immortal. Changing
request identifiers and positive numeric text therefore remain reachable after
their request ends. Explicit GC cannot remove that owning table reference.

This is separate from the earlier [missing iterator deleter](iterator-owner-release-20261030.md).
Read-only production map-counter observations motivate isolated changing-input
tests; they do not identify application string contents or prove that this
mechanism accounts for all observed RSS growth. The fixed-input HMAC/JSON
control does not reproduce the issue because it reuses the same strings.

## Reproduction and retained intermediate results

On the preserved `6b8a6a36...` runtime, decoding and discarding 20,000 different
JSON strings adds 20,005 intern-table entries after explicit collection.
Identifier-shaped values add approximately 20,000 immortal strings; numeric
values are mortal but still table-owned. The corrected path adds only a few
harness/static entries. A CPython 3.12 control adds zero entries. Its optional
immortal-count interface is unavailable, so the report retains null for that
field rather than inferring a value.

The first ingress-only candidate, `922e098a...`, passes native, fixture and fixed
performance checks but **fails** its complete 900-second concurrency test. It
finishes 34,659 requests without call errors, with all threads joined and only
3,920 KiB RSS growth; however, its intern-table growth after 120 seconds is 256,
exceeding the predeclared less-than-128 criterion. It is not committed separately
or deployed. Its original failed report, source archive and binary are retained.

Further isolated tests show `repr(int)`, `int.__repr__` and indented JSON each
retain 5,001 entries from 5,000 different positive numbers. JSON's Python
encoder fallback uses the numeric representation descriptor. This also matters
to application JSON with custom separators/sorted keys, including canonical
signed-operation bodies; it is not merely a diagnostic-report issue. Generic
numeric and object rendering must return ordinary owned data strings.

The next intermediate, `7cfe5eac...`, fixes numeric rendering and passes the full
fixed gate, but a real production-style `uuid.uuid4().hex` test still retains
5,002 entries from 5,000 UUIDs. Python's UUID implementation reaches the native
string formatter. Split/strip/case conversion/formatting/decoding tests expose
the same data-construction policy. Its older-scope ASGI run is intentionally
stopped to extend coverage: the last sample records 420.38 seconds and 16,206
requests. SIGINT terminates that owned CLI without a Python final sample; no
complete interval or joined-worker result is claimed. The interruption receipt,
partial series and binary are preserved. No service or GPU process is signalled.

## Correction

The final candidate uses the existing owned-data constructor for JSON data,
embedding string creation, incoming IPC strings, representations, integer/base
formatting, native string transformations and bytes/codec decoding. JSON keys
retain their existing per-decode memoization. Unicode, explicit lengths and
embedded NULs retain their contents and ownership.

The owned-data helper preserves the fixed empty/ASCII-singleton cache, which
has a bounded domain. Variable data does not enter the identifier table.
Compiler/name interning and explicit `sys.intern` remain unchanged, as do
ordinary `Value::string` semantics elsewhere. No global interning policy,
object layout, public ABI, wire protocol, IPC capacity, collection cadence,
Python standard-library implementation or VM dispatch implementation is replaced.
This is not a general weak-intern-table or automatic-GC implementation.

## Final candidate qualification

The accepted preceding executable and full Release libraries, plus both
intermediate binaries, are preserved. Controls compare those three versions,
the final candidate and CPython: 65 independent cases cover changing JSON
values, numeric representations, indented JSON, real UUIDs, string operations
and decoding. Native coverage includes 2,000 values through both C ABI string
constructors and 1,000 actual IPC round trips. Explicit interning, fixed
singletons, custom formatter dispatch, Unicode and embedded NULs remain valid.

All 25 selected native checks pass. The complete 453-case fixture comparison
has 436 passes, 17 failures identical to the fixed baseline, and no new failures.
Native asyncio collection contracts pass. The existing packaged fixture failures
remain recorded; this is not an all-fixtures-pass claim.

The complete eleven-case default performance gate exits 0 against the original
`678cc67c...` accepted Release, with 21 order-balanced pairs, five warmups and
the original 10% criterion. The largest observed ratio is 1.071x for function
calls; JSON dumps is 0.980x. The VM object `.text` remains byte-identical to
the preserved object and the entry remains 64-byte aligned. All 90 public
C API exports remain. No baseline, cases, loops, optimization level or
thresholds are changed.

The default 30-second API collection and normal/exceptional shutdown contracts
pass. A separate 900-second four-thread ASGI/inspect test now creates a fresh
real `uuid.uuid4().hex`, then parses it as JSON, on every request. It retains
0.1-second sleeps, two-second test collection, the 20 MiB RSS limit and the
original less-than-128 intern-growth limit after 120 seconds. The complete
run completes 900.10 seconds and 34,339 requests with zero errors, all four
threads joined, 962,368 objects collected, 3,824 KiB final RSS growth and
**zero** intern-table growth after the warmup. First/last 6,000-request median
is 2.551/2.281 ms, p95 8.947/8.683 ms. It exits 0. Its load differs from the earlier ASGI tests; their
latencies are not treated as identical-workload performance comparisons.

Final-source production deployment and sustained qualification remain pending.
The preceding iterator-runtime production interval cannot qualify this candidate.
The protected shared Windows Release and test service are unchanged.

## Evidence

- Final [fixed performance gate](data/string-ingress-r3-performance-gate.json),
  [unchanged VM text and ABI/source identity](data/string-ingress-r3-abi-layout.json),
  [idle preflight](data/string-ingress-r3-idle-preflight.json).
- [25 native checks](data/string-ingress-r3-native-correctness.log),
  [453 complete fixture outcomes](data/string-ingress-r3-fixture-differential.json),
  [asyncio contracts](data/string-ingress-r3-asyncio-accelerator.log).
- [65 controls including all intermediate versions and CPython](data/string-ingress-r3-controls.json),
  [default collection/shutdown](data/string-ingress-r3-native-lifespan.log),
  [complete real-UUID ASGI/inspect/GC series](data/string-ingress-r3-threaded-soak.json).
- First candidate [passing performance gate](data/string-ingress-r1-performance-gate.json),
  [source/ABI](data/string-ingress-r1-abi-layout.json),
  [partial-scope controls](data/string-ingress-r1-controls.json),
  [failed complete 900-second result](data/string-ingress-r1-threaded-soak-failed.json).
- Superseded second candidate [passing gate](data/string-ingress-r2-performance-gate.json),
  [source/ABI](data/string-ingress-r2-abi-layout.json),
  [numeric controls](data/string-ingress-r2-controls.json),
  [explicit interruption and unqualified partial series](data/string-ingress-r2-superseded-interruption.json).
