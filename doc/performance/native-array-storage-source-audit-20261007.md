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
