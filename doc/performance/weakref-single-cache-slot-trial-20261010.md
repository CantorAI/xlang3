# Direct single-entry weakref reuse probe — 2026-10-10

`inspect._shadowed_dict` in CPython 3.14 repeatedly calls `weakref.ref` for
the same MRO classes. A current steady-state primitive run measured the
XLang3 repeated `weakref.ref` path at about **24.6x CPython 3.14.7 time** and
`inspect._shadowed_dict` at **18.3x**. That workload evidence justified a
fresh paired check of the existing rejected idea to bypass the temporary
owning candidate vector when the target has one callback-free built-in weak
reference.

The candidate checked the one-entry target index while holding the registry
mutex, retained the reference, and read its fixed callback slot directly.
Every subclass, callback reference, multi-reference target, or mismatched
native state fell back to the existing enumeration and attribute lookup.
Eight alternating parent/candidate process samples measured 20,000 warmed
operations each. Candidate/parent ratios were **1.00x** for repeated weakref
creation, **1.00x** for dereference, and **1.00x** for the full
`inspect._shadowed_dict` helper. This more controlled run found no gain, so the
candidate source change was removed before further testing.

The full-suite root cause remains in the generic Python/runtime work performed
by `inspect` and `typing`; the single-entry weakref cache lookup was not a
material part of this measured gap.

## Evidence

- [Current repeated weakref baseline samples](data/weakref-reuse-current-r2-20261010/)
- [Alternating candidate trial samples](data/weakref-reuse-candidate-r1-20261010/)
- Baseline executable/runtime SHA-256: `E39AF2EE6E4A531D7758525A93E63A40DD678DB9BB6BE1C3AB4FF1B0ADCC7584` /
  `F06476F7BA02C36879FA52DB6248375B10F14D0A188E750F05F57EC321C76707`
- Trial executable/runtime SHA-256: `E39AF2EE6E4A531D7758525A93E63A40DD678DB9BB6BE1C3AB4FF1B0ADCC7584` /
  `9D83E6A824464971514E8F9A3E567C7AE44F911044D184ED90B851EC538079EC`
