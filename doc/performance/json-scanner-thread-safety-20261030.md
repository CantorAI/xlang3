# Shared JSON scanner synchronization (2026-10-30)

The first Azure stability candidate still crashed during signed observability
calls. A separate process using only six Python threads and ordinary
`json.loads()` reproduced SIGSEGV without Cantor, IPC or serving modules.
Its GDB stack is `JsonBuiltinParser::parse_value` -> `json_scanner_call` ->
`ensure_key_indexes`, at ELF address `0x9aeb8b`. This matches the deployed
kernel file offset `0x9adb8b` plus its executable segment delta `0x1000`.
Unlike the earlier SDK stress reproduction, this identifies the shared
decoder memo and the concrete parser call path.

Ordinary native callbacks run outside the VM execution lock. The stdlib
default JSON decoder retains one native scanner whose key memo is reused
between calls. Concurrent scanner callbacks could look up, insert and clear
that same dictionary while another scanner rebuilt its lazy indexes.

The scanner callback now takes the existing execution guard for the complete
parse, including error-path memo clearing. Captured encoder callbacks also
guard their lazy fallback state and container traversal; the default encoder's
existing fast callback already executes with the lock held. No Python stdlib
module is replaced, no VM loop changes, and no IPC capacity is increased.
Hooks continue through the Python fallback and existing blocking native calls
retain their normal execution suspension behavior.

The new fixture shares the default decoder, an explicit decoder and a captured
encoder among six threads. It validates nested values, duplicate/escaped keys,
malformed-input errors and subsequent decoding, and checks the memo is empty
when all calls finish. The isolated original reproducer passed in 50 fresh
processes after the fix (300,000 loads). Sixteen native checks passed. The
449-case fixture differential has 432 passes, 17 baseline-identical failures
and no new failures; the packaged Linux fixture limitations described in
[the SDK evidence](native-sdk-collection-locks-20261030.md) still apply.

The preserved accepted Release remains the performance baseline. An additional
copy of the preceding candidate executable/runtime was saved before this edit.
The VM source hash, function size `0x2f37c9`, and preceding candidate entry
address `0xacac80` are unchanged. All 90 public C API exports remain present.
The complete default Release gate passed all eleven cases with 21 balanced
pairs, five warmups and the unchanged 10% limit, on CPU 2 without overlapping
builds or tests. Relative to the original preserved Release, arithmetic is
0.999x, property access 1.034x, JSON dumps 1.001x and the largest ratio is 1.034x.

- [Complete gate](data/json-scanner-locks-20261030-gate.json)
- [Full fixture differential](data/json-scanner-locks-20261030-fixtures.json)
- [ABI and unchanged VM layout](data/json-scanner-locks-20261030-abi.json)

Production deployment acceptance is separate; these checks alone do not
establish production longevity or inference token throughput.
