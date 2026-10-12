# inspect weakref-MRO LRU identity trial — 2026-10-12

## Result

The identity-index shortcut was rejected. It bypassed the native
`_functools._lru_cache_wrapper` hash and equality work when every argument was
an already-hashed exact builtin weakref and the argument objects matched a
cached tuple by identity. Distinct weakrefs, subclasses, keywords, and other
key shapes stayed on the existing generic path. This was intended to help
`inspect._shadowed_dict_from_weakref_mro_tuple`, which CPython 3.14.7 implements
as a cached native LRU wrapper and XLang3 invokes heavily from the unchanged
Python `inspect` and `typing` modules.

The official `typing_runtime_protocols` case did not move: the candidate
measured **2.86 ms ± 0.08 ms** and the parent measured **2.85 ms ± 0.07 ms**.
The direct cache diagnostic also failed to show a consistent gain. Across five
alternating 128,000-hit samples, the candidate was slightly slower for a
one-key working set and effectively unchanged for 8 and 32 keys; its apparent
6% gain at 128 keys was not reflected in the official benchmark. The runtime
shortcut and its temporary fixture change were removed.

The diagnostic does confirm a substantial lower-level gap: the actual
`inspect` weakref-MRO cache hit path took about **8.3× to 10.9×** longer on
XLang3 than CPython across working sets of 1, 8, 32, and 128 cached keys. The
relative gap increases with the cache working set, but this trial does not
show that the vector scan alone is responsible. The earlier positional
fastcall adapter was also rejected; optimizing this cache further needs
isolated evidence for the hash, native call, and cache lookup costs separately.

## Evidence

- Equal-hit diagnostic: [`profile_inspect_weakref_lru_cache.py`](../../benchmarks/diagnostics/profile_inspect_weakref_lru_cache.py)
- XLang3 vs CPython 3.14.7, five alternating samples:
  [`inspect-weakref-lru-cache-equal-hits-20261012.log`](data/inspect-weakref-lru-cache-equal-hits-20261012.log)
- Parent vs identity candidate, five alternating samples:
  [`inspect-weakref-lru-identity-candidate-ab-20261012.log`](data/inspect-weakref-lru-identity-candidate-ab-20261012.log)
- Official pyperformance 1.14.0 fast-mode samples:
  [parent](data/inspect-weakref-lru-identity-parent-typing-20261012.json),
  [candidate](data/inspect-weakref-lru-identity-candidate-typing-20261012.json)
- Both official runs used `C:\Python\Python314\python.exe` (CPython 3.14.7),
  the fixed `build-repro/main-verify-20261006/Release/xlang3.exe` path, and the
  same dependency site. The executable SHA-256 was
  `5731C010C761F1CD7D784EE6A9671E7AD923F3F2789CC9359F046176CB67D9CE`.
