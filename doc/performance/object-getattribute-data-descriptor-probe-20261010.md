# Native object attribute data-descriptor probe — 2026-10-10

## Question

The unchanged `inspect.getattr_static` implementation repeatedly calls
`object.__getattribute__(instance, "__dict__")`. Its native XLang3 counterpart
first checks whether an instance value shadows a class descriptor, even when
the descriptor is a data descriptor that must win. Skipping that instance
storage probe looked like a possible reduction in repeated runtime work.

## Result

The candidate moved the data-descriptor check before slot, instance-dictionary,
and fallback-attribute probes. Eight alternating process pairs measured 20,000
calls to the existing pyperformance `typing_runtime_protocols` helper on each
binary. Candidate/parent median ratios ranged from **0.97× to 1.06×** across
the 11 diagnostic rows. The target `object_getattribute_dict` row measured
**1.02× candidate/parent**; `inspect_getattr_static_instance` also measured
**1.02×**. This does not establish an improvement, so the source change was
removed before continuing.

These are runtime diagnostics, not pyperformance scores. They include Python
loop and call overhead. The Python 3.14.7 library implementations were
unchanged.

## Evidence

- [Order-balanced raw samples and executable hashes](data/object-getattribute-data-descriptor-balanced-r1-20261010.json)
- Candidate source was built in the established
  `build-repro/main-verify-20261006/Release` path; its pre-change executable and
  runtime pair is preserved under `build-repro/controls/object-data-descriptor-parent-20261010/Release`.
