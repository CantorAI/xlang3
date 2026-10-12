# Weakref callback-slot duplicate-write trial — 2026-10-10

`weakref.ref(obj)` created a new built-in reference by calling
`make_weakref_ref()`, which initialized its callback slot to `None`, and then
called generic attribute assignment to write `None` into the same slot again.
The native `_weakref` entry point now skips the second assignment when the
callback is omitted or explicitly `None`; a non-None callback still follows
the existing attribute path. The nearby comment explains why the common path
can safely avoid that dispatch. This is a runtime/native-module change, not an
IR change, and it does not translate any pure-Python library logic.

## Measurement

The 20,000-reference diagnostic measured creation at a median of **28.30 ms**
on the candidate, **29.15 ms** on the saved parent, and **1.70 ms** on CPython
3.14.7. The candidate is therefore still about **16.6× slower** than CPython
for this isolated creation loop. The candidate's 2.9% lower creation time
against that parent is directional only: the saved parent predates the inline
weakref target-pointer change, and an executable from the exact source state
before this one-line optimization was not preserved. The comparison cannot
isolate the duplicate-write change or support a stable speedup claim.

Dereference measured 6.92 ms on the candidate and 7.72 ms on the older parent;
that difference is expected to include the earlier inline target-pointer
optimization and says nothing about this callback-slot change. CPython measured
0.572 ms. Raw process samples and these limitations are recorded in the
[measurement data](data/weakref-duplicate-callback-write-microbench-20261010.json).

The full Python 3.14.7 fixture runner completed against the fixed candidate
path after the build. The build used the established Visual Studio script and
did not change the fixed run path. This focused trial has no new official
pyperformance result and no full 11-case baseline gate; it is not a commit
checkpoint. The larger current gap remains the 75/97 completed full run, with
22 failed cases and a 0.12306× geometric speed ratio on its scored subset.

The optimization removes a redundant operation, but the native creation path
still allocates a generic instance and updates weakref registry structures.
The evidence here is too weak to attribute the measured directional change.
Further work should profile those costs and establish same-source paired
controls before claiming a creation-speed gain.
