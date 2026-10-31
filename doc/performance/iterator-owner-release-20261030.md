# Native iterator owners surviving final release

The completed live interval documented in
[closure-cycle-reclamation-20261030.md](closure-cycle-reclamation-20261030.md)
failed its API memory criterion. Its report remains failed. A subsequent
15-minute observation of the separate Cantor resource-publication deployment
passed, but does not replace the failed 45-minute memory interval.

## Reproduction and cause

The old API's privately preserved heap contains 233,613 tracked tuples,
including 153,915 two-element and 76,095 five-element tuples with reference
count one. Only aggregate
object metadata was inspected; heap contents are not published.

A second offline shape audit identifies 75,998 five-field token records, all
with reference count one, and 151,996 distinct position tuples referenced by
those records. Together these account for 227,994 tracked tuples. The audit
also identifies 19 native tokenizer owners. A further offline walk of their
native token vectors accounts for exactly all 75,998 records. Seventeen of
the streams stopped before their final token; most stopped within their
first 200 tokens while retaining thousands. The native state's offsets are
checked against its source-defined layout (400 bytes, index at 56, tokens at
360). This directly identifies the retained record owners, but is not
evidence of one leaked tokenizer per HTTP request or proof that this alone
explains the interval's RSS growth. Neither source lines nor token text are
read or reported by these audits.
The token records and their position tuples alone use at least 24.35 MiB in
their 144-byte and 96-byte allocation classes, excluding strings and other
owners. This is retained allocation size, not a measurement of reclaimed RSS.

Ordinary tuple creation, tuple subclasses, named tuples, dictionary item
iteration, enumerate, zip, and argument packing did not reproduce this
retention. Iterating `_tokenize.TokenizerIter` did: 1,000 tokenizations of
`x = (1, 2)` leave 27,000 additional tuples and 1,000 tokenizer/source owners
after collection. An early loop break also retains the owners. Direct
`next(stream)` and direct bound-method invocation do not reproduce it.

A separate, synthetic standard-library reproduction calls
`inspect.getblock()` 20 times on a two-line function followed by 50 assignment
lines. The preserved Azure B06 runtime retains 20 tokenizers and 12,720
additional tuples after collection. CPython retains no tokenizers. This
exercises standard-library early exit without reading application source,
opening IPC/HTTP connections, or submitting model inference.

`release_last_reference` removes an object from GC tracking before dispatching
its destructor. Its switch omitted `CallableIterator`, `ChainIterator`, and
`ProtocolIterator`, even though the existing functional-iterator helper has
the corresponding destructors. A zero-reference wrapper therefore becomes
invisible to GC without releasing its opaque owning Values. The wrapped
tokenizer retains its token and position tuples. Increasing IPC capacity or
the collection frequency cannot repair this missing ownership retirement.

The correction routes those three kinds to the existing deleter. It changes
no tokenizer algorithm, native ABI, object layout, IPC protocol or capacity,
and does not move a Python standard-library implementation into C++.

The regression fixture checks weak owner release for protocol and getitem
iteration, callable/sentinel iteration, chain and chain.from_iterable, and
early exit from tokenization. It also retains yielded tokens and verifies
their text and position survive reader destruction. CPython passes this
fixture. The preserved pre-fix native runtime fails all six owner checks.

## Performance investigation

Both initial formulations pass the 25 selected native tests and the complete
451-case fixture comparison: 434 pass, 17 fail identically to the fixed
baseline, and there are no new failures. They nevertheless fail the complete
fixed performance gate. Their binaries and reports are retained; neither is
accepted, committed as an engine fix, or deployed.

The first formulation adds the three explicit switch cases. Its VM entry is
`0xacb490`; arithmetic and property access regress approximately 19% and 24%.
The second formulation keeps the old common-kind switch and calls the
kind-checked deleter from its fallback. Its VM entry is `0xacb030`; arithmetic
and property access regress approximately 33% and 25%. The VM implementation
source and its `0x2f37c9` function size are unchanged in both. Both also show
range iteration near 0.63x the baseline time, which is not an isolated overall
runtime improvement.

The fixed baseline and qualified pre-fix runtime have 64-byte-aligned VM
entries (`0xacac00` and `0xacb040`). These observations match the earlier
[SDK collection synchronization investigation](native-sdk-collection-locks-20261030.md).
That change anchored the ELF text section; growth within an earlier object
file can still move this entry relative to the section start. The third
candidate retains the direct destructor cases and gives `run_function` an
explicit GNU ELF 64-byte alignment. The dispatch implementation is unchanged;
the source comparison strips only the six alignment/comment lines and checks
the original source SHA-256. This isolates an alignment change without
identifying a particular CPU mechanism.

The completed third build keeps all 90 public C API exports. Its VM entry is
`0xacb4c0`, with the original `0x2f37c9` function size. The complete VM object
file `.text` section is byte-identical to the preserved object: 3,327,596 bytes,
SHA-256 `a0f0bdacd349c3fc201d10a34d701afda4d5a64ac1ddf1419cdfe72912f6a842`.
Its section alignment changes from 16 to 64. The unrelocated function bytes
also match exactly. Linked call relocations and addresses can still differ;
this is not a claim that the entire linked runtime is byte-identical.

The third build passes all 25 selected native tests, the 451-case fixture
comparison (434 passes, 17 baseline-identical failures, no new failures), and
the native asyncio collection contracts. The complete fixed performance gate
exits 0: all 11 unchanged default cases pass against the original `678cc67c...`
baseline, with 21 order-balanced pairs, five warmups and the original 10%
criterion. Arithmetic is 0.999x, function calls 0.999x, property access 1.026x,
and the largest observed ratio is 1.026x. No cases, loop bodies, optimization
level, thresholds or baseline were changed.

On the exact third-build binary, 1,000 native/stdlib tokenization and early-exit
controls retain no additional tuples or tokenizer owners. The preserved B06
control retains 27,000 tuples and 1,000 tokenizers. The synthetic
`inspect.getblock` control changes from 12,720 tuples/20 owners to zero/zero.
All six weak-owner checks and retained yielded values pass. The unchanged API
helper's default 30-second collection, normal shutdown and exceptional-body
cancellation/join checks also pass.

The 900-second concurrency qualification retains the original four
ASGI/JSON threads, 0.1-second sleeps, two-second collection cadence and memory/
throughput checks. It additionally executes synthetic `inspect.getblock()`
early exit in every request, continuously exercising the repaired ownership
boundary. Its timings are therefore not directly comparable to the earlier
JSON-only V5 soak. The subsequent live interval uses the unchanged RPC/HTTP
checker and its original criteria.

It completes 900.10 seconds and 34,605 requests with zero errors, all four
threads joined and 969,816 objects collected. Final RSS growth after the first
sample at or after 120 seconds is 3,840 KiB, within the unchanged 20 MiB
criterion. First/last 6,000-request median is 1.914/1.869 ms, with p95
7.402/7.384 ms. It exits 0. This is an isolated ASGI/inspect/GC workload, not
HTTP socket or native GPU decode qualification.

## Complete live interval on the iterator runtime

The deployed `6b8a6a36...` runtime completes 2,700.28 seconds with 10,676
signed RPCs and 2,665 HTTP health requests. The original checker exits 0:
zero errors, all client threads joined, one unchanged cached proxy and no
core/API restart. Core/API PIDs remain 923645/923939. The strict collector
also verifies unchanged native images, all 55 backend Python source hashes,
configuration hashes, stored role/graph sources and external state links.

The unchanged less-than-20-MiB-per-process criterion passes. Core RSS changes
from 138,580 KiB after 300 seconds to 158,300 KiB, growth 19,720 KiB; API changes
from 207,780 to 223,640 KiB, growth 15,860 KiB. These are also the sampled final
peaks. No server collection is forced and no sampling phase or threshold is
adjusted. The API helper retains its original 30-second interval.

First/last five-minute RPC median is 5.291/5.395 ms, p95 11.661/7.823 ms;
HTTP median is 12.286/11.916 ms, p95 19.467/15.632 ms. The full RPC maximum is
1,000.97 ms and the HTTP maximum is 98.93 ms. Passing the recorded interval
does not establish a bounded latency tail or indefinite memory convergence.

Four real streaming START requests complete before this interval, with four
matched END requests completing after its verified completion. All eight use
the same prompt, 64-token limit, serial concurrency, zero warmups/retries and
actual final usage. All return HTTP 200 and finish normally. Client first
visible content times are:

| Model | START, seconds | END, seconds |
| --- | --- | --- |
| 2B | 0.658, 0.292 | 0.639, 0.326 |
| 27B | 2.153, 1.980 | 1.664, 1.732 |

Same-request worker receipts are present for all eight calls. Their reported
decode rates are 48.94/48.35 -> 53.76/54.09 tokens/s for 2B and 35.77/35.08 ->
36.44/34.89 tokens/s for 27B. These worker/model timers include their documented
execution boundary, not pure GPU kernel time. They use 43/52 generated tokens;
client final completion usage is 44/53 including the sampled stop token.
This small sample does not certify sustained model throughput or exclude a
future long TTFT tail. There is no model
inference emitted by the checker. This arrangement differs from the older
failed interval's recorded overlapping background work, so it is not an
identical-total-load before/after comparison or a sustained GPU decode test.
Client timings and native worker timings must remain separate.

Read-only counter inspection during this interval also leads to a distinct,
reproducible [transient-string ingress finding](transient-string-ingress-20261030.md).
That follow-up candidate is not deployed during this iterator qualification.
Its future qualification cannot reuse this interval. The older complete
45-minute API-memory failure remains failed.

No protected shared Windows Release files or service are modified.

## Evidence

- Rejected formulations: [first full gate](data/iterator-performance-gate.json),
  [first ABI/layout](data/iterator-abi-layout.json),
  [fallback full gate](data/iterator-r2-performance-gate.json),
  [fallback ABI/layout](data/iterator-r2-abi-layout.json).
- Qualified third build: [complete fixed gate](data/iterator-r3-performance-gate.json),
  [ABI, source and identical object text](data/iterator-r3-abi-layout.json),
  [idle preflight](data/iterator-r3-idle-preflight.json).
- Correctness: [25 native tests](data/iterator-r3-native-correctness.log),
  [all fixture outcomes](data/iterator-r3-fixture-differential.json),
  [asyncio contracts](data/iterator-r3-asyncio-accelerator.log).
- Ownership: [before/after controls](data/iterator-r3-owner-controls.json),
  [preserved Azure inspect control](data/inspect-block-pre-fix-azure.json),
  [failed API structural metadata](data/failed-api-896837-token-owner-metadata.json),
  [direct tokenizer-vector ownership](data/failed-api-896837-token-state-ownership.json).
- [Default collection and shutdown](data/iterator-r3-native-lifespan.log),
  [900-second concurrent ASGI/inspect/GC record](data/iterator-r3-threaded-soak.json).
- Live interval: [initial exact identity](data/iterator-r3-live-initial-identity-20261031.json),
  [strict acceptance and full time series](data/iterator-r3-live-acceptance-20261031.json),
  [all START/END client, API and worker observations](data/iterator-r3-live-stream-start-end-20261031.json).
