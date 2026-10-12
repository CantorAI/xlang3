# `object.__getattribute__` call-site cache trial — 2026-10-11

## Result

The exact-method `CallMethod` cache was removed. On the `typing_runtime_protocols`
pyperformance case, one fast-mode sample measured 2.75 ms immediately before
the cache, 2.79 ms with it, and 2.76 ms after removing it. That is no measurable
gain; the with-cache run was 1.02x slower than the pre-cache sample and pyperf
flagged the sample as unstable. The cache is not retained.

The full CTest suite passed (55/55) with the cache present and the module
exception propagation fix in place. After removing the cache, the fixed Release
build completed; no semantic code was changed by that removal.

Against the saved CPython 3.14.7 sample, the after-removal XLang3 result is
2.76 ms versus 125 us, or 22.03x slower. The cache experiment did not close
that gap.

## Why this cache was tried

`inspect.getattr_static` repeatedly asks the canonical `object` class for its
`__getattribute__` native method. The trial cached only that exact unbound
native method, guarded by receiver-class and metaclass versions, with ordinary
lookup retained for observer-enabled or mutated cases. The narrow call shape
was not a meaningful share of the benchmark cost, so the extra cache was not
justified.

## Evidence

- [Before-cache sample](data/typing-runtime-protocols-attribute-error-constructor-fast-20261011.json)
- [With-cache sample](data/typing-runtime-protocols-object-getattribute-cache-20261011.json)
- [After-removal sample](data/typing-runtime-protocols-object-getattribute-cache-removed-20261011.json)
- [CPython 3.14.7 reference](data/typing-runtime-protocols-attribute-dispatch-cpython3147-20261010.json)
