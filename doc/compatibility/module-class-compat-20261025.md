# Module class reassignment compatibility

Pip25.2 vendored urllib3 assigns a ModuleType subclass to an imported module. Compiled stores bypassed the module setter, which also treated __class__ as a namespace value. Validate the module subclass/layout, retain its runtime and class metadata, invalidate module attribute caches, and route ordinary positional/keyword/fused VM calls through class-special-method lookup. Namespace __call__ entries do not make a module callable.

The regression covers imported module identity/namespace preservation, dynamic descriptors and getattr, repeated class changes, positional/keyword and fused calls, invalid classes/layout and restoration. Six SDK contracts, four ctypes regressions, normal-cache offline pip25.2 with pinned Python3.14.0 source and pip26.2.1 pass without CPython or vendor patches. The fixed cumulative baseline default11 gate passes at21 repeats/5 warmups/10% threshold. This is a compatibility fix; no optimization or xMind release acceptance is claimed. Failed candidates remain in xMind validation artifacts.

See the adjacent qualification receipt and the preserved performance report.
