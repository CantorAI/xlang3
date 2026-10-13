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

## Follow-up native compatibility validation — 2026-10-13

The branch now includes the native Pydantic Core 2.50.1 facade. On the
unchanged TestProj backend, xlang3 pip installed the pure-Python Pydantic
2.14.1 wheel into an isolated target, then the branch's native Core package
was selected ahead of the prior project-local facade. pytest 8.4.2 with its
default assertion rewriting passed all 9 API tests. This exposed that
`ast.alias` accepted pytest's source-location keywords only as deprecated
arbitrary attributes. Its `_attributes` now declares the four standard
location fields, and `ast_alias_source_locations` covers their constructor
values with deprecation warnings treated as errors. The only remaining pytest
warning is an unrelated Starlette/AnyIO deprecation.

The complete xlang3 CLI fixture suite passed after the AST change. The required
11-case Release performance comparison also passed against the preserved
pre-change executable; its 21-pair report is
`scratch/performance/pytest-alias-source-locations-20261013.json`.

The TestProj API suite was rerun on branch tip `b3ec9bf4` after syncing with
`origin/main` at `f0c425cf`: xlang3 3.14 ran pytest 8.4.2 with Pydantic 2.14.1
and the native Pydantic Core 2.50.1 facade. All **9 tests passed** in 3.58
seconds. The only warning is Starlette's upstream AnyIO `BlockingPortal` alias
deprecation. Exact sanitized invocation and result:
[`pytest-testproj-current-branch-20261013.json`](pytest-testproj-current-branch-20261013.json).
