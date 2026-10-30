# Native SDK collection synchronization (2026-10-30)

The Azure native host faulted while building a dictionary's lazy key index.
The kernel reports a file offset: the matching ELF's executable PT_LOAD has
VirtAddr minus Offset = 0x1000. Consequently 0x9adb0b/0x9adb9a map to ELF
addresses 0x9aeb0b/0x9aeb9a, both `ensure_key_indexes`, rather than the previously
reported `mapping_truthy`/`mapping_to_string` symbols. Kernel offsets alone do
not supply the caller stack or prove which application dictionary was shared.

`x3_get_item` already held the VM execution guard, but native dictionary writes,
entry enumeration and list appends did not. A host writer could resize entries
or indexes while a guarded reader rebuilt them. Against the exact deployed
Release library, an eight-thread standalone dictionary workload aborted with
unaligned tcache/double-free corruption; GDB included `mapping_set_item` and
`x3_dict_set_item`. A deterministic lock-boundary test also failed because
`dict_set_item` completed while another thread owned the VM lock.

The embedding boundary now guards dictionary mutation/enumeration, list append,
container rendering, and byte/stream graph serialization/deserialization.
The existing nested guard/suspension mechanism is reused. No lock is added to
an opcode or Python dictionary operation, no capacity is increased, and no
application workaround substitutes for the native correction. Returned borrowed
byte-buffer pointers still require their existing ownership/lifetime contract;
this patch does not promise atomicity across multiple separate SDK calls.

## Performance qualification

A separate complete Release baseline was preserved before edits, including its
runtime libraries. It exactly matches the deployed runtime SHA256
`678cc67c8f941685d3717de0cee3e950430e0e0cc9e06c58ad8874d001256588`.
This comparison prevents additional regression; it does not establish recovery
against an older historical performance baseline.

The first guard-only binary failed the complete default gate: scalar arithmetic
was 1.334x and property access 1.282x. CPU affinity independently confirmed the
failures. No thresholds, cases, loop bodies or baseline were changed.
The unchanged VM function retained size 0x2f37c9 but moved from 0xacac00 to
0xacac70 when the ELF text start shifted by 0x70. A GNU ELF alignment anchor on
the SDK dictionary entry point gives the output text section 64-byte alignment.
The original VM source and original matching Release object are preserved;
its entry is now 0xacac80 with identical size. No VM implementation, compiler
optimization level, instruction semantics or monitoring feature was changed.
A proposed direct VM-function alignment edit was withdrawn before acceptance;
the preserved object was reused only after restoring byte-identical archive
source and its original timestamp. Its preservation receipt is retained with
the build evidence.

The final complete default gate passed on the same XPS, with CPU 2 affinity,
21 order-balanced pairs and five warmups per case, the unchanged 10% limit,
and no concurrent builds or benchmarks. Arithmetic measured 0.994x, property
access 1.038x, and function calls 1.069x; passing does not mean zero overhead.

- [First complete gate, failed](data/native-sdk-collection-locks-20261030-prealignment.json)
- [Affinity confirmation, failed](data/native-sdk-collection-locks-20261030-prealignment-affinity.json)
- [Final complete gate, passed](data/native-sdk-collection-locks-20261030-gate.json)
- [Correctness and ABI summary](data/native-sdk-collection-locks-20261030-correctness.json)

## Correctness and scope

Sixteen SDK/runtime/IPC/Cantor/Galaxy/Principia native checks passed. The SDK test
checks eight entry points block while the VM lock is owned by another thread,
then checks shared dictionary/list integrity with four writers and four readers.
Fifty fresh-process dictionary stress runs passed (4000 keys, eight threads).
All 90 public C API exports remain present.

All 448 CLI fixture cases were compared with the preserved baseline: 431 passed,
17 failed identically, and none newly failed. Packaged `xlang3._pth` intentionally
excludes fixture-local imports, so identical CLI hard links without that
filename-specific path file were used for both correctness runs. Traceback
runtime-directory prefixes were normalized, not expected outputs. The original
full fixture command does not pass in this packaged Linux environment; the
shared failures are retained and are not fixed or waived as general correctness
claims. Performance used the original executables/path configuration.

The standalone reproduction establishes a native concurrency defect consistent
with the corrected production fault site. It is not a captured production
caller stack, an all-platform acceptance result, or a production longevity
claim. Deployed observer-failure injection and a sustained signed RPC check are
separate acceptance steps.
