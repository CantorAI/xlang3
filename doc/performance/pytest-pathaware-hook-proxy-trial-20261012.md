# pytest path-aware hook proxy compatibility — 2026-10-12

The pytest 8.4.2 smoke run exposed a missing `__getattr__` fallback in the
VM's fused keyword-method-call path. Pytest's `PathAwareHookProxy` creates hook
callers lazily and invokes `config.hook.pytest_cmdline_main(config=config)`;
the hook proxy worked for ordinary attribute reads, but `CallMethodEx` did not
resolve the missing attribute through the instance's `__getattr__` method.
The keyword-call miss path now resolves that hook and forwards the original
keyword arguments without changing the successful lookup fast paths.

The `object_attribute_hooks` fixture verifies a multi-level lazy proxy called
with a keyword argument. That fixture passes on the candidate. pytest 8.4.2
collects and passes a smoke test with the default assertion rewriting enabled.
An earlier apparent `FunctionDef.lineno` failure came from a malformed local
smoke-test source file and was not an xlang3 AST compatibility gap.

The first isolated `pip install` attempt was run before native package targets
had been built into the isolated runtime, so its initial `mmap` import error
was a test-build setup issue rather than a missing implementation. Once the
runtime packages were built, a real pure-Python property descriptor bug surfaced
in `execute_inline_small_self_method`: methods with nested attribute access
could return an inherited property's descriptor without invoking its getter
when that getter fell outside the inliner's supported subset. The inliner now
falls back to ordinary execution for descriptors it cannot safely evaluate.
The new `inline_method_descriptor_fallback` fixture covers this case. With the
fix, xlang3's own pip installed pytest 8.4.2 from local wheels and pytest 9.1.1
from PyPI; the normal pytest 8.4.2 assertion-rewriting smoke test also passes.

The required full default Release performance gate passed against the
preserved pre-change executable and runtime DLL. Per-case results are recorded
in [`pytest-pathaware-hook-proxy-20261012.json`](data/pytest-pathaware-hook-proxy-20261012.json).
The keyword-call fix only adds fallback work on missing method lookups. The
descriptor fix keeps the existing inlined path for supported property getters
and falls back for descriptor shapes the inliner cannot implement correctly.
