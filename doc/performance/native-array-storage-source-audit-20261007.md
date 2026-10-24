# Native array storage: measured scaling and source audit

This audit records a generic native-module performance defect and its buffer
constraints. No array engine change is included in checkpoint `003b3882`,
whose all-97 pyperformance run remains on the unchanged Release binary.

## Existing evidence

The [scalar-read probe](../../benchmarks/diagnostics/native_array_scalar_scaling_probe.py)
constructs each array before timing, then performs 256 reads of index zero
inside a function. It keeps the operation count constant while changing the
array length. The raw [XLang3 control](data/native-array-scalar-control-20261007.json)
and [CPython 3.14.7 reference](data/native-array-scalar-cpython3147-20261007.json)
each retain five samples per length. Median durations:

| Array elements | XLang3, 256 scalar reads | CPython 3.14.7, 256 scalar reads |
| ---: | ---: | ---: |
| 256 | 0.1184 ms | 0.00690 ms |
| 4,096 | 0.2609 ms | 0.00720 ms |
| 65,536 | 2.2361 ms | 0.00740 ms |

These are short diagnostic timings, not official pyperformance scores or
proof of a candidate gain. The growth with array size identifies an algorithmic
problem in an operation that should only inspect one element. The
[package protocol probe](../../benchmarks/diagnostics/package_runtime_protocol_probe.py)
also records failure of `copied[:] = original[:]`, the native-array operation
used by the unchanged Python SciMark implementation. Slice access currently
enters the integer-index converter and raises TypeError.

## Source cause and buffer constraints

`src/runtime/modules/system/array_module.cpp` keeps a `std::string bytes` in
`ArrayState` while also publishing a separate bytearray payload. Each length
or scalar read calls `sync_array_bytes`, which copies the entire published
bytearray back to the string. Scalar assignment first copies that buffer,
encodes one item, modifies the string, then calls `publish_array_bytes`, which
allocates another whole bytearray and republishes typecode/itemsize metadata.
Repeated appends likewise republish growing buffers. This duplicates storage
and makes otherwise constant-size operations depend on total buffer length.

[CPython 3.14.7 native array code](https://github.com/python/cpython/blob/v3.14.7/Modules/arraymodule.c#L2370-L2629)
uses array storage directly for scalar access. Its buffer export exposes the
same storage with element format/size and tracks active exports; resizing
while exported raises BufferError. Its slice operations copy selected items
into independent array storage and enforce replacement type/length rules.

XLang3's instance-backed memoryviews presently retain the array instance and
resolve its published payload. Bytearray export counting in `Value::memoryview`
only increments when the direct storage owner is a bytearray. The array path
in `src/executor/xlang_vm/xlang_vm_inline_support.h` leaves that owner as an
instance, so native-array resizes do not currently get the bytearray export
guard. It also changes the view format from `B` to the typecode without
adjusting the default byte-count shape/stride to element units. These are
source findings awaiting focused runtime probes; they are not new passing
compatibility results.

A storage correction must have one authoritative byte buffer shared by native
array operations and views. It must retain that buffer for every operation,
keep existing view writes visible to array reads, expose array writes through
existing views, preserve `memoryview(a).obj is a`, and reject length changes
until all relevant exports have released. Do not simply remove synchronization
while retaining two independently mutable buffers. Do not replace a buffer on
every scalar write or append, and do not refresh type metadata in those hot
paths. Any new owning native references need matching GC traversal/cleanup.

## Next verification

After the frozen full-suite run ends, preserve its executable/DLL as a control.
Probe scalar reads/writes and lengths, integer-index coercion, positive and
negative strides, equal-length and resizing slice assignment, overlapping
self-slices, independent slice storage, and `tolist` behavior. Check view
shape/strides/format/obj, two-way writes, derived views, release order, and
BufferError without partial mutation. Keep exception and subclass dispatch
semantics while considering native stack argument adapters.

Then run the complete fixture/C++/SDK/serialization validation and the complete
unchanged fixed Release gate. Rerun the fixed-operation scaling diagnostic
and official SciMark definitions against CPython 3.14.7; report remaining
failures and CPU algorithm costs separately. A flatter diagnostic curve is
useful evidence, but an official score is needed for a suite gain claim.

CPython implements `array` natively, so this work belongs in XLang3's own
native `array` counterpart. SciMark and all CPython pure-Python library
algorithms remain Python. The comparison and run paths remain unchanged.


## Focused buffer protocol evidence after the Unicode checkpoint

The new [eight-case probe](../../benchmarks/diagnostics/native_array_buffer_protocol_probe.py)
was run only after official timing finished, on CPython 3.14.7 and the
validated Unicode checkpoint `f2e76b14`. The native array source has not changed.
CPython passes all eight cases; XLang3 passes scalar integer-index coercion
and fails the other seven:

| Check | CPython 3.14.7 | XLang3 |
|---|---|---|
| Scalar `__index__` reads/writes | Pass | Pass |
| Independent slice storage | Pass | TypeError |
| Overlapping and resizing slice assignment | Pass | TypeError |
| Memoryview shape, strides, itemsize, and object | Pass | AssertionError |
| Two-way writes through array and memoryview | Pass | AssertionError |
| Resizing with exports, without partial mutation | Pass | AssertionError |
| Derived views and release order | Pass | AssertionError |
| `tolist` | Pass | AttributeError |

These are correctness records, not speed scores. Each case catches its own
exception so later checks still run. Both processes return 0 because the
records were produced; that must not be described as a passing suite. The
[CPython records](data/native-array-buffer-protocol-cpython3147-20261007.json),
[XLang3 records](data/native-array-buffer-protocol-control-20261007.json), and
[source/runtime provenance](data/native-array-buffer-protocol-control-provenance-20261007.json)
preserve the baseline. The next implementation must use one shared buffer,
correct element metadata and exporter identity, and export lifetime guards;
slice and `tolist` support must be added without replacing Python SciMark.


## Shared-storage candidate, still uncommitted

The first shared-storage candidate keeps one bytearray in native ArrayState,
uses it directly for scalar reads/writes and lengths, publishes its identity
once, and registers native-owned tracing references. Native slices and
`tolist` are implemented. Memoryviews own the shared bytearray for export
counting and retain the logical array separately for `.obj`. Readonly metadata
uses class descriptors, while private buffer metadata bypasses Python
subclass overrides. The broader fixture also found and fixed an existing
final-index increment overflow in generic list/tuple/binary slice loops.

All complete fixtures and eight selected C++/SDK/serialization tests pass.
The original eight-case buffer probe now passes all eight. In the fixed-count
scalar diagnostic, medians are 0.1024, 0.1027, and 0.0996 ms for 256 reads on
256-, 4,096-, and 65,536-element arrays respectively. The old large-array
median was 2.2361 ms, about 22.45x slower. These are diagnostic timings, not an
official SciMark score or CPython win.

The [strided-view probe](../../benchmarks/diagnostics/native_array_strided_view_probe.py)
passes on CPython 3.14.7 but fails positive and negative steps on both the
[preserved control](data/native-array-strided-view-preserved-control-20261007.json)
and [shared-storage candidate](data/native-array-strided-view-shared-candidate-20261007.json).
Generic memoryview slicing with step != 1 copies selected bytes into a readonly
snapshot, losing the exporter and export lifetime instead of sharing storage.
That path must be corrected before claiming shared-buffer semantics for all
views. The fixed default regression gate and official SciMark run have not yet
been run for this candidate; it must not be committed until required validation
is complete. See [pending validation](data/native-array-shared-storage-validation-pending-20261007.json).

## Subsequent strided-view repair and official results

The preceding section describes the first intermediate candidate, not the
current implementation. Shared positive/negative strided views now pass the
focused ownership probe and the broader fixture, including nested slices,
overlap, readonly equality/hash, export lifetimes and logical byte conversion.
All complete fixtures, eight C++/SDK/serialization tests and the complete
fixed default gate passed for the first strided build.

Its official SciMark run completed all five subtests with 20 values each at
the unchanged 300-second definition cap. XLang3 remains about 10–24x slower
than CPython 3.14.7. The [phase-specific report and horizontal chart](native-array-shared-strided-scimark-20261007.md)
record every mean, speed factor and binary identity. The final scalar-read
diagnostic for that phase is 0.08860 ms on 65,536 elements versus the original
2.2361 ms XLang3 control, about 25.24x faster locally. This is not a CPython
win or an official before/after SciMark speedup: the old official run failed
before producing successful means.

A later consumer audit found graph/IPC serialization and byte formatting
needed explicit logical-order packing for shared strided views. Contiguous
native consumers must reject such views instead of treating a null span as
empty input. Serialization, formatting, integer conversion and native buffer
guards now have added regression coverage. The focused fixture passes on
CPython 3.14.7 and the newly built XLang3 candidate. Fresh full correctness,
the complete fixed gate and official SciMark verification are running in
sequence; see [current validation state](data/native-array-final-consumer-validation-20261007.json).
These later engine edits remain uncommitted until all required checks pass.

The final consumer-audit validation has now completed: full fixtures, eight
C++/SDK checks, all 11 default fixed-gate cases and all five official SciMark
subtests return 0. The added direct IPC test also passes in a test-only rerun;
engine hashes are unchanged. The final chart/CSV and measured native dispatch
costs are recorded in the [checkpoint report](native-array-shared-strided-scimark-20261007.md).
XLang3 still takes about 9.9–25.4x as long as CPython on these five subtests.
The frozen all-97 aggregate remains separate and unchanged.
