# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# XLang3 No-GIL Runtime Policy

This is the product policy for XLang3 threading and object sharing. XLang3 does
not adopt a CPython-style global interpreter lock as the core safety mechanism.
The runtime keeps XLang's `X::Value`/refcount object model and uses explicit
object-sharing rules plus local synchronization.

## Runtime Rules

- Every native thread owns its own XlangVM execution state: frames, registers,
  temporary arenas, current-frame debug views, and current-thread trace hook.
- Object refcounts are atomic so object lifetime is safe when values cross
  threads.
- Thread-local memory caches are only used for objects whose allocation/free
  happens on the owning thread, or through allocator paths that can safely
  return foreign-thread frees to a global/owner-safe path.
- Immutable objects may be shared freely after construction. This includes
  strings, bytes, tuples containing share-safe values, code objects, functions,
  classes after publication, and module metadata after import publication.
- Mutable objects may be shared only through one of these routes:
  - Protected by a runtime/container lock.
  - Transferred so only one thread mutates it.
  - Copied before cross-thread mutation.
  - Wrapped by a future synchronized/shared object adapter.
- Built-in mutable containers must not rely on a global VM lock for correctness.
  Lists, dicts, sets, bytearrays, instances, modules, and mutable native objects
  need either internal synchronization for cross-thread mutation or documented
  thread-confined behavior until their synchronized path is implemented.
- Native modules must document whether returned objects are immutable,
  thread-confined, transferred, or synchronized.
- Host/device RPC, subprocess IPC, and shared-memory IPC move data through the
  same policy: immutable/share-safe values can be shared by reference when the
  transport supports it; mutable values are copied, transferred, or wrapped.

## CPython Compatibility Rule

Python source code must not need new syntax to run under this policy. Normal
Python code that uses `threading.Lock`, `queue.Queue`, immutable messages, or
copy-based handoff should behave naturally. Code that relies on accidental GIL
serialization of unsynchronized mutable shared state is not guaranteed to be
data-race-compatible; XLang3 may offer a compatibility mode later, but it must
not become the core runtime model.

## Implementation Checkpoints

- Atomic refcounting is already the object lifetime baseline.
- Per-thread XlangVM state and thread-local trace state are runtime concepts.
- The current runtime still has legacy execution-lock regions and mutable
  containers that need a focused synchronization audit.
- Before marking the threading implementation complete, audit every mutable
  built-in object and native module against the rules above.
