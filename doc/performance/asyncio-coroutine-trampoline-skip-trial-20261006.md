# Asyncio coroutine delegation-probe skip trial (2026-10-06)

## Result

Rejected. `generator_send()` called the synchronous-generator delegation
trampoline on every resume, although `generator_can_trampoline()` always
rejects coroutine objects. I tested skipping that inapplicable probe when the
generator's existing `is_coroutine` flag is set. The change compiled, but one
official pyperformance 1.14.0 fast run measured **4.52 s ± 0.05 s** for
`async_tree_none`, versus **4.50 s ± 0.05 s** for the matched current-main
control. `pyperf compare_to` hid the pair as statistically insignificant.
No speedup was established, so the code was removed.

The saved CPython 3.14.7 full-suite result is **227.4 ms** for this case; the
control remains about **19.8× slower**. This confirms that removing one
inapplicable branch scan does not address the dominant per-resume cost.

## Reproduction and evidence

Both measurements used pyperformance **1.14.0**, the Python **3.14.7**
benchmark environment at `C:\Python\Python314`, and the same dependency site
and Windows compatibility shim. The control executable and runtime DLL SHA-256
were `E8EFEE922E95093437E0FEF3F6754ED2730A3CEC2BBA9A4C067F2C660C7B202C` and
`B29EE2944F3916F7D901EB2A178F58CD9EA1B36DC1D1DFB2033338012D61CC2D`. The
candidate executable was unchanged; its runtime DLL SHA-256 was
`CE53D149A9DEC71B8D41CA7DCCC0E41A56FDFFBB946A6083B7E95AC435AA2D80`.
The ordinary Release executable and DLL were restored to the control hashes.

- [Control pyperf JSON](data/pyperformance-coroutine-trampoline-control-fast-20261006.json)
- [Candidate pyperf JSON](data/pyperformance-coroutine-trampoline-candidate-fast-20261006.json)
- [pyperf comparison](data/pyperformance-coroutine-trampoline-compare-20261006.txt)

The result is diagnostic negative evidence, not a performance improvement.
The next async-tree work should measure a larger part of the native Task and
saved-frame resume path before proposing another local fast path.
