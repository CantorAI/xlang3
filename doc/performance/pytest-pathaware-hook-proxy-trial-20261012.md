# pytest path-aware hook proxy and TestProj compatibility — 2026-10-12

The pytest 8.4.2 smoke run exposed a missing `__getattr__` fallback in the
VM's fused keyword-method-call path. Pytest's `PathAwareHookProxy` creates hook
callers lazily and invokes `config.hook.pytest_cmdline_main(config=config)`;
the hook proxy worked for ordinary attribute reads, but `CallMethodEx` did not
resolve the missing attribute through the instance's `__getattr__` method.
The keyword-call miss path now resolves that hook and forwards the original
keyword arguments without changing successful lookup fast paths.

The `object_attribute_hooks` fixture verifies a multi-level lazy proxy called
with a keyword argument. That fixture passes. pytest 8.4.2 collects and passes
a smoke test with default assertion rewriting enabled. An earlier apparent
`FunctionDef.lineno` failure came from a malformed local smoke-test source file
and was not an xlang3 AST compatibility gap.

The first isolated pip test exposed a separate native buffer gap: pip's
CacheControl response wrapper calls `memoryview(mmap.mmap)`, but the xlang3
`mmap` package did not export mapped bytes through the runtime's memoryview
contract. The package now exports a zero-copy external-backed view, keeps the
mapping alive for the view lifetime, and rejects `close()` while any base or
derived view remains active. `memoryview.obj` reports the native exporter, and
releasing the final view releases the mapping export. The `mmap_native` fixture
covers these behaviors.

The runtime memoryview constructor now resolves property getters on native
exporters and preserves external buffer ownership. The native Pydantic Core
package now ships its matching 2.46.5 pure-Python facade and `core_schema.py`;
its `_pydantic_core` implementation remains xlang3 native. This lets the
pure-Python Pydantic wheel use the bundled implementation without installing a
CPython extension.

Validation on the isolated Release build: the full xlang3 CLI fixture suite
passes; xlang3 pip resolves TestProj's runtime and development requirements in
dry-run mode; installing only pure-Python wheels with dependency resolution
disabled, plus their pinned pure-Python dependencies, succeeds; and TestProj's
9 pytest API tests pass on xlang3. The native Pydantic Core package reports
2.46.5, so the TestProj smoke run used Pydantic 2.13.5, whose PyPI metadata pins
that exact core version. The current broad `<3.0` TestProj constraint also
permits Pydantic 2.14.1, which requires native core 2.50.1 and is not covered by
this compatibility result.

The required 11-case default Release performance gate passed against the
preserved pre-change executable and runtime DLL, with 21 paired samples per
case. Per-case results are recorded in
[`pytest-pathaware-hook-proxy-20261012.json`](data/pytest-pathaware-hook-proxy-20261012.json).
The keyword-call fix adds work only on missing method lookups; supported
descriptor inlining keeps its fast path, while unsupported descriptor shapes
fall back to normal execution.
