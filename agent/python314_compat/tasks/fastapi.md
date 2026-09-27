# FastAPI on XLang3

For continuation under another Codex account, start with
[`FASTAPI_ACCOUNT_HANDOFF.md`](FASTAPI_ACCOUNT_HANDOFF.md). This file remains
the detailed implementation and verification ledger.

## Required execution boundary

Continue the rules established in the "Complete XLang3" task and recorded in
`system_prompt.md`, `rules.md`, `context/module_policy.md`, and
`tasks/native_module_parity.md`. The user explicitly reaffirmed these rules
for this goal on 2026-09-19.

- Run FastAPI and its Python dependencies on XLang3's own VM and object model.
- Do not depend on the CPython interpreter, Python DLL, CPython C ABI, or the
  optional CPython bridge. CPython is only an external differential test oracle.
- Keep all pure Python standard-library and third-party code as Python source.
  Do not replace FastAPI, Starlette, AnyIO, Pydantic's Python layer, or `ssl.py`
  with C++ implementations.
- Reuse existing native libraries. Implement only the missing XLang3 bindings
  and runtime semantics at native dependency boundaries.
- Use the Python 3.14 standard library `.py` files, executed by XLang3.
  The Python source library is allowed; CPython's execution engine and binary
  extension ABI are not.
- Keep existing native-module packaging. Do not relocate working modules solely
  to imitate CPython packaging or reduce the core DLL.
- Keep runtime built-ins small and broadly useful. Put dependency-backed
  extension facilities under `modules` when that is their natural owner; this
  is an ownership boundary rather than an absolute size rule.
- No stubs, fake outputs, test-specific shortcuts, or broad locks/retries to
  conceal failures. Every compatibility fix needs a regression fixture.
- Run the deterministic build and fixture tools for each implementation batch;
  run the complete Release CTest suite before declaring the goal complete.
- Commit and push only when the user asks. The user requested a checkpoint
  merge and push to `main` on 2026-09-23.

## Seven-project upstream matrix (latest verified results, 2026-09-25)

All runs use the pinned, unmodified upstream checkout on XLang3 Release with
normal pytest assertion rewriting and a fresh bytecode cache. A green row
records the process exit, not merely passing assertions before teardown.

| Project | Current result | Remaining issue |
| --- | --- | --- |
| FastAPI 0.141.1 | 3,324 passed, 17 skipped, 4 xfailed; exit 0 | Stability/load scope remains open |
| Starlette 1.6.0 | 1,056 passed, 4 skipped, 3 documented Windows deselections, 2 xfailed; exit 0 | None in selected suite |
| Pydantic 2.13.5 | 5,804 passed, 261 skipped, 26 xfailed; exit 0 | Review skip reasons against pinned upstream requirements |
| pydantic-core 2.46.5 | 5,794 passed, 130 skipped, 10 xfailed; exit 0 | None in selected suite |
| AnyIO 4.15.1 | Collection blocked | Unchanged `trustme` now stops at missing native `_rust.x509.RevokedCertificate` |
| Uvicorn 0.53.0 | 1,000 passed, 352 upstream skips; exit 0 | Optional HTTP/2 and TLS variants needing test dependencies remain unverified |
| HTTPX 0.28.1 | Collection blocked | Unchanged `trustme` now stops at missing native `_rust.x509.RevokedCertificate` |

The matrix is **5/7 exit-zero**. The open projects are neither passes nor
platform skips. The latest untouched full Pydantic run exited zero. The current
Release build passes the 206-case local FastAPI gate on its rerun and 53/53 CTest. The live
demo reports XLang3 3.14.7 and FastAPI ready.

2026-09-24 AST traceback continuation: the Pydantic setup failure's missing
`rpds.rpds` import revealed a general traceback formatter crash. CPython 3.14
gives child `Name` nodes proper zero-based columns for a multiline call;
XLang3's `ast.parse()` gave those children line/column zero and one-based
columns for the call. Python 3.14's source `traceback.py` then computed a
negative caret anchor and raised `IndexError` while formatting the original
`ModuleNotFoundError`. The parser now captures child expression spans when
building a public AST, and the AST bridge converts lexer one-based columns
to Python AST zero-based columns. Normal execution parsing keeps its prior
trace-event behavior. `ast_traceback_multiline` is a CPython-oracle fixture,
and the public FastAPI `traceback_multiline_contract` route formats an
exception from a multiline call. Direct CPython/XLang checks match; the
complete Release build and CTest **53/53** pass. The FastAPI local and full
upstream reruns are pending. Native `rpds` remains open; the improved
traceback now exposes its missing import normally.

## Current evidence (2026-09-23)

After the `7b20d5b` main checkpoint, native `_cffi_backend.FFI` now decodes
the generated CFFI type, typename, and struct/union tables and implements
`ffi.sizeof()` for the primitive, pointer, typedef, array, and standard-layout
struct types used in Trio's Windows definitions. The dedicated
`tests/fastapi/run_cffi_type_layout.ps1` regression compares **all 31 Trio
typedefs plus pointer, array, and named-struct cases** against CFFI under
CPython 3.14 and passes.
This is type-layout support only: native library symbol binding, typed foreign
calls, owned C data, and buffers remain open, so Trio tests still cannot pass.

Current continuation: fixed parser recovery when an invalid
parenthesized `async with` reaches a closing delimiter; `ast.parse()` now
raises `SyntaxError` for parser-rejected source instead of returning an empty
placeholder. Coroutine VM suspension preserves the active exception and
handler stack across `await`, while nested interpreter calls restore their
caller's active exception. The CPython 3.14 `async_exception_context.py`
oracle, existing traceback fixture, public FastAPI async re-raise route, and
unchanged Starlette lifespan failure case pass. General `os.stat()` now follows
symlink targets by default and honors `follow_symlinks=False`; Windows
`os.symlink()` recognizes existing directory targets. The CPython oracle,
unchanged Starlette file/directory symlink tests, and a public FastAPI static
file request through a symlink pass.

The unchanged Starlette routing/schema/static/status/template/TestClient/
WebSocket asyncio selection passes **168 tests, 1 upstream Unix-only skip**;
132 cases were deselected (Trio plus one Windows permission assertion). The
excluded `test_staticfiles_with_invalid_dir_permissions_returns_401[asyncio]`
fails on CPython 3.14 too because Windows `chmod` does not enforce its Unix
permission expectation. A separate static-file fixture initially failed under
both runtimes because Git `core.autocrlf=true` converted an upstream LF file to
CRLF; it was restored byte-for-byte from the upstream Git blob without editing
the test source. Release CTest is **53/53** and all local FastAPI integration
cases pass after these fixes. The untouched full seven-project matrix and
Trio/CFFI boundary remain open; full compatibility is unproven.

Latest checkpoint: the CPython 3.14-oracle exception-group, custom-exception
constructor, and sized file-read fixes are validated by unchanged Starlette
cases and public FastAPI integration. Added an independent `_zstd` native
package backed by vendored Zstandard 1.5.7 C sources, while retaining Python
3.14's unmodified pure-Python `compression.zstd`. The local oracle verifies
one-shot, incremental, and 192 KB streaming round trips. Unchanged Starlette
`test_request_headers[asyncio]` now passes because HTTPX2 naturally
advertises zstd, and a FastAPI TestClient zstd response is decoded correctly.
Release CTest is **53/53** and all local FastAPI integration cases pass.
Complete `_zstd` API coverage, the Trio/CFFI boundary, and the full untouched
upstream matrix remain open. This checkpoint is not full compatibility.

Continuation after `76d9231`: the complete unchanged Starlette
form-parser/request/response asyncio selection now passes **167 tests with one
upstream Python-version skip**; 161 Trio cases were deselected. The next
unchanged routing/schema/static/status/template/TestClient/WebSocket selection
stops at `test_lifespan_state_unsupported[asyncio]`: XLang3 times out while
Starlette formats the lifespan exception traceback; CPython 3.14 passes the
isolated case. This is an open runtime failure, not a skip. A standalone
reproduction also stalled during import, so the specific root cause is not
yet established. Native `_zstd` now also implements frame-header metadata and
compression/decompression parameter bounds using the Zstandard C API. The
public Python 3.14 `compression.zstd` oracle matches CPython for known and
unknown frame content size and parameter bounds. Release CTest passed **53/53**
on rerun; the first run had one intermittent native-network large-response
failure, and that test passed in isolation. The complete local FastAPI
integration runner passed after this change.

The checkpoint through `58b939d` was merged and pushed to `origin/main` at the
user's request. This continuation on `fastapi-compatibility` implements
`ast.Expression` compilation, parser-backed eval expressions,
`ast.Interactive` compilation, dynamic `eval()` locals mapping lookup,
exception-class constructor semantics for `raise`, keyword forwarding in
`_contextvars.Context.run`, Python-sign modulo in the VM constant path, and
`bytes.rjust`/`bytearray.rjust`. CPython 3.14 oracle fixtures cover each.
The real Starlette session TestClient probe now persists a signed cookie;
untouched Starlette asyncio session tests pass **10/10**, WSGI tests **6/6**,
and body-limit tests **18/18**. The final Release build passed CTest **53/53**
and all production FastAPI integration runner cases, including Uvicorn
end-to-end. The untouched serial FastAPI and Starlette suites
still expose a shared Trio Windows CFFI boundary:
`CLibrary.CreateIoCompletionPort` is missing. Full pinned upstream
compatibility remains unverified.

Continuation after `a742cec` (uncommitted on `fastapi-compatibility`):
`BaseExceptionGroup(message, ordinary_exceptions)` now constructs an
`ExceptionGroup`, matching CPython 3.14; mixed/base-only inputs retain
`BaseExceptionGroup`, and user subclasses retain their type. A CPython-oracle
fixture covers these cases. This fixes unchanged Starlette
`test_collapsing_task_group_two_exc[asyncio]` (1/1); the complete
`test__utils.py -k 'not trio'` selection passes **13/13**. A public FastAPI
ASGI route now exercises two failures in an AnyIO task group and catches the
resulting `ExceptionGroup`; both XLang3 and CPython pass. Release CTest passes
**53/53**, and the complete production FastAPI integration runner passes,
including Uvicorn end-to-end. The next unchanged Starlette selection
(`test_applications.py`, `test_authentication.py`, `test_background.py`,
`test_concurrency.py`, excluding Trio cases) passes **42/42**. The following
selection (`test_config.py`, `test_convertors.py`, `test_datastructures.py`,
`test_endpoints.py`, `test_exceptions.py`) passes **76**, fails **1**, and
deselects **34** Trio cases. Its only failure,
`test_missing_env_file_raises`, fails identically on CPython 3.14 under
Windows because the upstream test interpolates a `C:\Users` path into an
unescaped regular expression and `re` rejects `\U`. Do not alter XLang3 or
the upstream source to make this case pass.

The subsequent unchanged Starlette form/request/response batch selected 168
asyncio cases: **164 passed, 3 failed, 1 upstream skip, 161 Trio cases
deselected**. CPython 3.14 passed the same three failing cases. Two are now
fixed in uncommitted general runtime changes: exception subclasses retain
constructor arguments even if their custom `__init__` skips `super()` (the
upstream URL-encoded form-limit case now passes), and buffered sized file reads
continue to the requested count or EOF (the upstream 14 KB FileResponse case
now passes). Both have CPython-oracle fixtures. A public FastAPI TestClient
download of a 14 KB file passes on XLang3 and CPython. The remaining request
headers test expects HTTPX2 to advertise `zstd`; its decoder imports Python
3.14's unmodified `compression.zstd` on CPython, which in turn requires the
native `_zstd` extension. XLang3 cannot use CPython's `_zstd.pyd`; implement
that true native boundary under `modules/`, while retaining the pure-Python
standard-library package. No HTTPX or Starlette source was altered.
After the file-read fix, the final Release CTest rerun passes **53/53** and
the complete FastAPI integration runner passes, including the new file
download, task-group route, and Uvicorn end-to-end case. One preceding CTest
run had an intermittent native-network large-response failure; the isolated
network test and the subsequent complete rerun both passed.

Branch: `fastapi-compatibility`, created from synchronized main `c09174a`.
Reference interpreter: CPython 3.14.7, used only for tests.

Installed under ignored `scratch/fastapi-deps` for investigation:
FastAPI 0.141.1, Starlette 1.6.0, Pydantic 2.13.5, pydantic-core 2.46.5,
AnyIO 4.15.1, Uvicorn 0.53.0, HTTPX 0.28.1.

The complete untouched pydantic-core serializer directory now collects 804
tests and passes **797 with 6 upstream skips and 1 expected failure**. The
continuing untouched validator sweep has closed function, generator, integer,
is-instance, is-subclass, JSON, list, literal, model-fields, and model-init
files. The untouched `test_model_fields.py` file is now **308 passed with 1
upstream skip and 0 failures** (**309 collected**), and untouched
`test_model_init.py` is **14/14 passed**. The implementation now covers the
real three-part model-fields result contract, aliases and attribute extraction,
extras and fields sets, custom initialization, instance revalidation, and
  custom-init union selection. Untouched `test_model_root.py` is now **9/9
  passed**. Untouched `test_model.py` is now **41 passed, 3 failed, and 1
  upstream skip** (**45 collected**), improved from 14 passed and 30 failed.
  All implementation cases in that file now pass. The three remaining
  validator-error comparisons require pytest assertion rewriting: with the
  required `--assert=plain` runner, XLang3 and CPython both produce the same
  empty `AssertionError` detail instead of pytest's rewritten source
  expression. Enabling rewriting currently makes XLang3 collect zero tests, so
  pytest rewrite support remains a separate runtime frontier.
  The adjacent untouched none, nullable, pickling, and set validator batch now
  passes **85/85**. This closes JSON `None` diagnostics, unhashable set-item
  aggregation, smart union list/set exactness, native `TzInfo` pickle state,
  and callback cycles crossing a native `SchemaValidator`. Native payloads now
  publish their retained Python values to the runtime cycle collector, and VM
  loop liveness no longer treats call-result temporaries as values carried
  between iterations. `function_attribute_gc.py` is the CPython 3.14 oracle
  for the general function-attribute cycle behavior.

The latest untouched validator batch adds bool, bytes, float, string, tuple,
complex, custom-error, chain, lax-or-strict, json-or-python, with-default,
callable, and recursive-definition coverage: **840 passed with 2 expected
failures**. Within that result, tuple is **89/89**, with-default is **130 passed
with 2 expected failures**, and recursive definitions are **36/36**. Separate
untouched gates pass arguments **201 with 50 upstream skips**, partial
validation **14/14**, and call/definitions/dict/enum/frozenset **168 with 1
upstream skip**. Recursive schemas now validate unresolved references at
construction, preserve definition-aware reprs and titles, detect cycles in
mappings and attribute objects, propagate nested validator errors, and support
assignment through definitions schemas. The general runtime comparison path
also takes the CPython identity fast path for a container compared with itself,
so cyclic dict/list/tuple equality cannot recurse indefinitely. The rebuilt
Release suite remains **53/53 passed** (33.55 seconds).

- [x] Nested comprehension assignment targets used by HTTPX headers.
  Reuse the recursive for-target parser; preserve the distinction between
  grouped names and singleton tuple targets. Coverage:
  `tests/fixtures/core/comprehension_nested_targets.py`.
  Release build and all 137 checks in `agent/scripts/run_fixtures.py` passed.
- [x] `_ssl` binding over the already bundled OpenSSL backend.
  The separate `xlang__ssl.x3pkg` exposes the Python 3.14 `_SSLContext`,
  `_SSLSocket`, `MemoryBIO`, `SSLSession`, and `Certificate` surfaces used by
  source-backed `ssl.py`; it does not load CPython or `_ssl.pyd`. Real OpenSSL
  behavior covers TLS over memory BIOs and native sockets, certificate and
  hostname verification, trust-store inspection, decoded/DER/PEM certificates,
  verified and unverified chains, ALPN, cipher and channel-binding inspection,
  session state and TLS 1.2 resumption, writable-buffer reads, SNI context
  switching, message callbacks, key logging, post-handshake client
  verification, DH/ECDH configuration, and TLS 1.2 PSK callbacks. Callback
  values and context getters retain their native ABI references, preventing
  callback closures from being reclaimed while OpenSSL still owns them.
  `tests/native/ssl_module.py` is a CPython-oracle fixture covering the public
  Python 3.14 wrapper, certificate-authenticated TLS 1.2/TLS 1.3, certificate
  chains, session reuse, SNI/message/keylog callbacks, caller buffers, PSK TLS,
  and socket-backed encrypted I/O. The FastAPI suite separately proves a real
  Uvicorn HTTPS request through Python 3.14 asyncio's MemoryBIO TLS path.
- [x] Pydantic native core required by the pinned production FastAPI stack.
  `modules/pydantic_core` now provides XLang3-native `SchemaValidator` and
  `SchemaSerializer` classes plus the error and schema helpers needed by the
  exercised Pydantic 2 paths. FastAPI request and response models validate and
  serialize on XLang3 without loading CPython. Every public
  `CoreSchemaType` category in pydantic-core 2.46.5 has an explicit native
  validation path; unsupported malformed schemas and unknown line-error types
  fail explicitly rather than falling through or accepting input.
  Validation and mode-correct serialization now cover bytes, sets, fixed
  tuples, constrained string lengths, dates, datetimes, UUIDs, and decimals.
  Date, datetime, UUID, and Decimal construction calls their real Python 3.14
  library classes rather than reimplementing those libraries in C++. The
  public FastAPI TestClient test `tests/fastapi/pydantic_common_types.py` sends
  these values through a real HTTP request and response, verifies coercion and
  JSON output, and verifies a structured `string_too_short` validation error.
  `function-before` and `function-after` schemas now execute real Pydantic
  field and model validators with or without `ValidationInfo`, preserve nested
  validation failures, and translate validator `ValueError`s into structured
  errors. ValidationInfo exposes the current model configuration, caller
  context, previously validated field data, canonical field name, and Python
  input mode. The public
  TestClient coverage in `tests/fastapi/pydantic_validators.py` verifies
  normalization, post-validation transformation, model validation, response
  serialization, context-sensitive validation, cross-field data access, and an
  HTTP 422 validator failure.
  `function-plain` schemas also execute no-info and with-info PlainValidator
  callbacks, including context and prior-field access. Call-level and
  schema-level strict mode now reaches primitive and collection validators;
  strict integer validation is covered by `pydantic_features.py` and rejects
  string coercion with a structured `int_type` error.
  `function-wrap` schemas now receive a real callable native handler that
  re-enters the inner schema with the same definitions, location, strictness,
  context, and field data. Coverage proves transformation, deliberate inner
  validation bypass, and propagation of an inner `int_parsing` error through a
  FastAPI HTTP 422 response.
  TypedDict schemas and discriminator-based tagged unions now validate and
  serialize through FastAPI. `tests/fastapi/pydantic_structures.py` covers
  nested coercion, real model branch construction, rejected discriminator
  tags, and correctly located required-field errors. Model and TypedDict field
  traversal now distinguishes defaults, optional TypedDict keys, and required
  keys before entering the field's primitive schema.
  String and integer enum schemas validate to their real Python Enum members,
  preserve members in Python serialization mode, and emit their underlying
  values in JSON mode. `tests/fastapi/pydantic_enums.py` covers successful and
  rejected HTTP bodies and integer-enum coercion. This also corrected the
  general native ABI call path so class calls honor custom metaclass `__call__`
  behavior; native callers now match normal VM calls for EnumType and other
  metaclasses.
  Variable-length typed tuples use Pydantic's `variadic_item_index` layout in
  validation and serialization. Frozen sets, `datetime.time`, integer and
  floating-point bounds and multiples, strict and string-to-float validation,
  collection length constraints, and integer list/tuple/set locations are
  covered through public FastAPI and Pydantic APIs. Nested list and dictionary
  failures now retain the actual item index or dictionary key in structured
  error locations. Coverage lives in `pydantic_common_types.py` and
  `pydantic_constraints.py`.
  Native `Url` and `MultiHostUrl` values now implement normalized components,
  default ports, query decoding, comparisons, hashes, public builders,
  multi-host DSNs, and IDNA through Python 3.14's codec. URL and DSN schemas
  validate and serialize through real FastAPI request/response handling in
  `pydantic_urls.py`. `TzInfo` is a real `datetime.tzinfo` subclass with fixed
  offsets rather than an import placeholder. Timedelta and complex schemas,
  instance/subclass/callable schemas, chain, lax-or-strict, json-or-python,
  embedded JSON, and `SchemaValidator.validate_json` are covered by
  `pydantic_common_types.py` and `pydantic_core_schemas.py`.
  Pydantic dataclass, dataclass-arguments, and dataclass-field schemas now
  validate dictionaries into real dataclass instances, apply defaults and
  post-init hooks, and serialize fields in Python and JSON modes. The FastAPI
  TestClient coverage in `pydantic_dataclasses.py` exercises a nested, slotted
  dataclass through request validation, response serialization, defaults,
  coercion, post-init behavior, and an HTTP 422 failure.
  Custom-error schemas replace nested validation failures with their configured
  error type, formatted message, and complete context mapping; validation and
  serialization coverage is included in `pydantic_core_schemas.py`. The same
  coverage verifies identity-based validation of Pydantic's public `MISSING`
  sentinel through the `missing-sentinel` schema.
  Arguments, arguments-v3, and call schemas bind positional, keyword-only,
  default, variadic positional, and variadic keyword values, retain precise
  argument error locations, invoke the configured callable, and validate its
  return schema. Public `ArgsKwargs` access and Pydantic's `@validate_call`
  decorator are covered in `pydantic_core_schemas.py`.
  Generator schemas now return a real lazy `ValidatorIterator`, validate each
  yielded item with its index location, enforce minimum and maximum lengths at
  consumption time, and preserve laziness. Serialization returns a lazy
  `SerializationIterator` in Python mode and emits JSON arrays in JSON mode;
  both iterator types expose their live index. Coverage is included in
  `pydantic_core_schemas.py`.
- [x] Python 3.14 subinterpreter primitives required by AnyIO imports.
  `_interpreters` owns isolated XLang3 runtimes with persistent `__main__`
  namespaces, lifecycle/ref tracking, source execution, serialized calls, and
  process-global IDs. `_interpqueues` provides bounded process-global queues
  whose values cross runtime boundaries through XLang3's value-graph codec.
  Python 3.14's `concurrent.interpreters` source imports and exercises create,
  prepare/exec/call/close and queue transfer. This also fixed fused method-call
  descriptor dispatch so it matches normal attribute loading.
- [x] Source-backed imports for the exercised dependency graph.
  Python 3.14's unchanged `hashlib.py` and `hmac.py` run over native `_hashlib`
  and `_blake2` packages. FastAPI, Starlette, Pydantic, AnyIO, HTTPX, Click,
  H11, and Uvicorn import under XLang3.
- [x] Routing, path/query/body validation, models/serialization, dependency
  injection, exceptions, middleware, and OpenAPI generation. Direct ASGI tests
  verify generated component schemas, `$ref`/`schema` serialization aliases,
  request constraints, validation errors, and response models.
- [x] Async requests, lifespan, streaming, background tasks, WebSockets, and
  Uvicorn HTTP serving with real source dependencies. Direct ASGI tests cover
  lifecycle and protocol behavior; a real Uvicorn process accepts an external
  HTTP/1.1 POST, validates its Pydantic body, and returns JSON, including the
  `Connection: close` path.
- [x] Public Starlette/FastAPI `TestClient` coverage for HTTP, lifespan, and
  WebSockets. Cancellation thrown through a delegated coroutine now resumes a
  parent with the child return value when the child suppresses cancellation;
  `tests/fixtures/core/async_suppressed_cancellation.py` protects this general
  generator/coroutine rule.
- [x] Untouched upstream FastAPI exception, authentication, streaming,
  WebSocket, dependency-yield, middleware, and multipart/form batches. The
  current verified batches are 5/5 exception-handler tests, 16/16 HTTP and API
  key security tests, 33/33 streaming/WebSocket tests, 33/33 dependency scope
  and post-yield lifecycle tests, and 84/84 middleware/form/file tests. These
  are executed from the pinned upstream checkout rather than copied or adapted
  test cases. `inline-snapshot` reporting is disabled because that plugin does
  not identify XLang3 as a supported interpreter; the tests still execute and
  their ordinary assertions remain active.
- [x] The broad untouched upstream security matrix passes 277/277. It covers
  API-key schemes, HTTP basic/bearer/digest, OAuth2 forms and flows, OpenAPI
  security models, JWT-backed tutorial applications, password login, disabled
  users, malformed tokens, and the complete tutorial004/tutorial005 variants.
  The run uses unmodified FastAPI, Pydantic, Starlette, HTTPX, AnyIO, PyJWT,
  pwdlib, and argon2-cffi sources; only inline-snapshot reporting is disabled
  for the interpreter-identification limitation described above.
- [x] Delegated async-generator exception and cancellation completion follows
  CPython. Exceptions thrown through an async-generator awaitable unwind the
  awaited coroutine before reaching the caller, and completion after a
  suppressed cancellation becomes `StopAsyncIteration`. This prevents leaked
  AnyIO capacity-limiter tokens and cancellation scopes. Coverage:
  `async_generator_await_exception_unwind.py` and
  `async_generator_suppressed_cancellation.py`.
- [x] Exceptional `with` and `async with` exits receive the exception's real
  traceback object, and reraising the active exception cannot create a
  self-referential `__context__` chain. Coverage:
  `context_manager_exception_traceback.py` and `exception_self_context.py`.
- [x] Assignment expressions followed by a call-argument separator no longer
  consume the separator as a singleton-tuple RHS. Module-level walrus targets
  are included in the static module slot table, and trivial-function inlining
  rejects `*args`, keyword-only, and `**kwargs` signatures that require normal
  argument binding. `named_expression_call_trailing_comma.py` is a CPython
  oracle for all three general runtime rules. This fixes unmodified
  `pytest.raises(match=...)` in FastAPI's upstream tests.
- [x] Native pydantic-core model-field validation implements configured extra
  behavior for dictionary input, including calls with `from_attributes=True`.
  `extra="allow"` retains values in `__pydantic_extra__`, exposes them through
  Pydantic's normal model API, records them in the field set, and serializes
  them; `extra="forbid"` produces structured `extra_forbidden` errors. The
  CPython-oracle coverage in `pydantic_core_schemas.py` and untouched FastAPI
  form-model tests verify both paths. Optional extra serialization uses
  `getattr(..., default)` so plain core-schema model classes do not leave a
  pending `AttributeError`.
- [x] Smart-union validation evaluates successful branches and selects the
  branch with the greatest valid-field count while preserving explicit
  `left_to_right` behavior and first-match ties. This restores Pydantic's
  model-union selection used by FastAPI security schemas. The current native
  implementation excludes `extra="allow"` values from the valid declared-field
  score, so a permissive HTTP security model cannot outrank the correct API-key
  or OAuth2 branch. CPython-oracle coverage in `pydantic_core_schemas.py` and
  untouched OpenAPI security tests verify this rule.
- [x] Native pydantic-core string schemas enforce `pattern` through Python
  3.14's regular-expression implementation and produce CPython-compatible
  `string_pattern_mismatch` details, message text, context, and input. The
  complete upstream OAuth2 test file passes 10/10.
- [x] Native pydantic-core function validators preserve caught `ValueError` and
  `AssertionError` instances in validation-error context and construct messages
  from the exception value rather than the runtime traceback text. Other
  exception types propagate normally. Local production coverage and the
  untouched response-model validator-cloning tests verify this boundary; the
  latter passes 3/3.
- [x] The `_argon2_cffi_bindings._ffi` native dependency boundary is backed by
  the upstream Argon2 C reference implementation. The upstream pure-Python
  argon2-cffi `PasswordHasher`, exceptions, parameter parsing, and pwdlib code
  remain unchanged; no CPython extension or ABI is loaded. Deterministic hash,
  verification, mismatch, and error-message coverage is in
  `tests/native/argon2/argon2_module.py`.
- [x] `bytes.rsplit` and `bytearray.rsplit` implement separator, whitespace,
  `maxsplit`, keyword, empty-separator, and result-type behavior. This restores
  unmodified PyJWT token parsing and extends the existing bytes split gate.
- [x] Assignment-expression targets participate in function scope and closure
  analysis, including nested handlers defined under conditional walrus
  assignments. This restores Pydantic JSON-schema metadata closures without a
  Pydantic-specific path. Captures referenced by generator expressions inside
  `assert` statements are also prepared before preceding local stores. XLang IR
  cache format version 40 invalidates artifacts compiled before these scope and
  await-precedence changes while retaining Python 3.14's `.pyc` header magic and
  source-library compatibility.
- [x] `await` binds to the primary/call expression before comparison operators,
  matching Python 3.14 for expressions such as `await stream.read() == data`.
  The CPython-oracle fixture `async_await_expression_precedence.py` protects the
  parser rule and the untouched upstream asynchronous UploadFile test passes.
- [x] Dictionary keys honor user-defined `__hash__` and `__eq__` during literal
  construction, lookup, assignment, membership, deletion, and dict methods.
  Runtime hashing recurses through tuple and frozenset keys, which restores
  Python's `functools.lru_cache` behavior for FastAPI's callable-identity tuple
  keys. CPython-oracle coverage is in `dict_custom_hash_equality.py`; the
  untouched upstream dependency-model file passes 10/10, including its 3,000
  callable cache test.
- [x] A further untouched dependency batch passes 101/101, covering dependency
  overrides, parameterless/partial/wrapped dependencies, PEP 695 annotations,
  security overrides, HTTP and WebSocket yield scopes, and hashable `Depends`.
- [x] `types.GenericAlias` exposes the CPython reduction protocol, including
  reconstruction through `(types.GenericAlias, (origin, args))`. Python 3.14's
  unchanged `copy.deepcopy()` can therefore clone annotations such as
  `list[Model]`; the CPython oracle in `generic_union_substitution.py` matches
  and the untouched FastAPI unique-operation-ID file passes 8/8.
- [x] `os.symlink` is implemented as a native OS/VFS boundary, with real host
  symbolic-link creation and `target_is_directory` handling. FastAPI's
  untouched frontend containment test creates a link outside the served tree
  and confirms it is rejected with HTTP 404.
- [x] Chained comparisons retain their middle operand across every comparison.
  This fixed Python 3.14 asyncio write-buffer limit checks used by Uvicorn and
  is covered by the function-local cases in
  `tests/fixtures/core/chained_comparisons.py`.
- [x] Fused local/local and local/constant conditional comparisons preserve
  Python rich-comparison dispatch and exception behavior. This allows
  asyncio's `heapq` of `TimerHandle` objects to schedule TLS handshake timers
  and is protected by class-defined `__lt__` cases in
  `tests/fixtures/core/chained_comparisons.py`.
- [x] Compiler-inferred instance layouts cross the internal class-construction
  boundary without adding private keys to user-visible metaclass namespaces.
  Explicit slots, custom metaclasses, decorated classes, and
  `dataclass(slots=True)` use dynamic layout handling when static inference is
  unsafe.
- [x] Writable instance `__dict__` replacement follows Python object-model
  semantics. Assigning a dictionary replaces the visible attribute mapping,
  retains dictionary identity, hides attributes from the previous mapping,
  and rejects invalid or slot-only assignments with the correct exception
  categories. Compiler-inferred layouts remain dynamic when a method writes
  `self.__dict__` or `self.__weakref__`. This supports Pydantic's general
  constraint metadata objects, including FastAPI `Query(pattern=...)`, without
  a framework-specific path. Coverage lives in
  `tests/fixtures/core/instance_dict_assignment.py` and
  `tests/fixtures/core/str_join_generator.py`.
- [x] The interactive `tests/fastapi/demo_website.py` runs as a real Uvicorn
  application on XLang3. It exercises a rendered HTML/CSS/JavaScript client,
  runtime telemetry, typed query parameters, Pydantic request and response
  models, create/filter/toggle/delete state transitions, OpenAPI generation,
  and structured HTTP 422 validation errors.
- [x] FastAPI's untouched `jsonable_encoder` suite passes 27/27. Native
  pydantic-core serialization now propagates `exclude_defaults`, applies model
  include/exclude selectors, and supplies plain serializers that request an
  info argument with the current mode and exclusion settings. This covers
  nested list/dict encoding, explicit and implicit defaults, aliases, custom
  field serializers, and `PurePath` serialization without changing FastAPI or
  Pydantic's Python sources.
- [x] Python 3.14 class annotations created under conditional class-body paths
  are exposed only when that path executes. The deferred `__annotate__`
  function retains the selected annotation occurrence and supports local-class
  annotation closures; false `TYPE_CHECKING` blocks no longer leak unresolved
  type-only names into runtime annotations. The existing STRING/FORWARDREF
  oracle and `deferred_annotation_closure.py` cover the general behavior.
- [x] Reflected set union accepts dictionary key/item views when the mapping is
  on the left, matching Python 3.14's set-like view protocol. Constructor slot
  inlining now rejects signatures with variadic, keyword-only, or defaulted
  parameters and leaves their binding to the normal call binder. The latter
  restores Pydantic v1's multi-name `IfConfig` constructor and is protected by
  `type_call_keywords.py`. The complete upstream router-default and inherited
  custom-class rerun passes 44/44.
- [x] Native pydantic-core set and frozenset length constraints run after item
  validation and deduplication. Their `too_short`/`too_long` messages and
  context include the Python 3.14 field type, configured bound, and native-core
  `actual_length` convention. The local constraint gate and FastAPI's untouched
  nested `Annotated` sequence file pass, bringing that upstream batch to 39/39.
- [x] Native pydantic-core validation errors retain every failed untagged-union
  branch, expose the configured validator title, and format singular/plural
  error counts. Primitive list, dictionary, boolean, none, missing-field, and
  invalid-key failures now produce structured line errors instead of generic
  runtime exceptions. Python-compatible boolean coercion accepts the standard
  textual values and reports `bool_parsing` for invalid strings. The untouched
  OpenAPI schema-type and path suites pass, and the public constraint gate
  protects the native behavior.
- [x] `PydanticUndefined` has its public pydantic-core representation, nested
  serialization exclusion keeps the selected parent and passes the child rule,
  and tagged unions support callable discriminators. The untouched parameter
  representation, response include/exclude, and annotated discriminator suites
  pass 26/26 in total.
- [x] Compiled regular-expression `sub()` and `subn()` accept the public
  `repl`, `string`, and `count` keywords with normal duplicate/unknown/missing
  argument handling. FastAPI's untouched release-tool suite passes 16/16.
- [x] F-string parsing distinguishes the `!=` comparison operator from the
  `!s`/`!r`/`!a` conversion marker. The CPython oracle
  `fstring_not_equal.py` and FastAPI's validation-context formatting test cover
  conditional expressions such as `{'s' if count != 1 else ''}`.
- [x] Integer subclasses, including `IntEnum`, inherit value-based integer
  hashing. This preserves the Python equality/hash contract for set and mapping
  membership and restores `signal.valid_signals()` behavior used by Uvicorn.
  The CPython-oracle set fixture covers equal integer subclasses.
- [x] Explicit base-dictionary operations bypass subclass `__getitem__`
  overrides, matching calls such as `OrderedDict.__getitem__(self, key)`.
  This removes recursive redispatch in debugpy's `MessageDict` without any
  debugpy-specific path. The unmodified Visual Studio debugpy adapter and a
  complete launch, breakpoint, locals, hover, async pause, continue, and
  termination session both pass.
- [x] Deferred class annotations follow executed class-body control flow,
  including duplicate annotations selected by conditional paths. The
  `future_class_conditional_annotations.py` CPython oracle protects this
  Python 3.14 behavior.
- [x] Generic parameter discovery and specialization recurse through nested
  unions and typing aliases. Type variables inside constructs such as
  `Callable[..., Awaitable[T]] | Callable[..., T]` are exposed and replaced by
  subscription. The untouched FastAPI WSGI tutorial tests pass 2/2, and the
  general behavior is covered by `generic_union_substitution.py`.
- [x] Python 3.14's `_ast` surface exports `match_case` with its public base,
  fields, attributes, and match arguments. This allows unmodified Coverage
  tooling to import its AST parser; `ast_python314_classes.py` is the oracle.
- [x] `itertools.islice` accepts `None` as the explicit start and applies the
  integer index protocol to bounds. Rich's rendering path now runs unchanged,
  and the standard-module fixture covers both forms.
- [x] Closures created while evaluating a generator expression's eager first
  iterable are discovered before preceding local stores are lowered. Recursive
  generator/lambda combinations retain the correct per-frame cell, restoring
  Pygments' unchanged recursive regex optimizer. The reduced oracle is
  `recursive_generator_closure.py`; XIR cache version 42 invalidates earlier
  lowering artifacts.
- [x] PEP 604 unions normalize `None` to `NoneType` when either operand is a
  generic alias, including `list[Path] | None`. Typer now processes FastAPI
  CLI's real annotations unchanged; the nested case is protected by
  `optional_union_none_type.py`.
- [x] `sys.prefix`, `base_prefix`, and `sysconfig` identify the installation
  that owns the active Python 3.14 source library rather than the XLang3
  executable directory. Coverage consequently classifies the source-backed
  standard library correctly and its `sys.monitoring` backend starts without
  re-entering its monitor lock. `sys_startup_config.py` verifies the contract.
- [x] The native SQLite boundary implements DB-API `executescript`,
  `executemany`, and cursor iteration. Coverage creates, populates, reads, and
  persists its real SQLite data file on XLang3. The untouched FastAPI CLI suite
  passes 2/2 through Coverage, FastAPI CLI, Typer, Rich, and Pygments, and the
  native SQLite regression covers the new public methods.
- [x] The latest untouched upstream batches pass 23/23 (one platform skip),
  172/172 (one platform skip), 103/103, 69/69, and 31/31 after their discovered
  general runtime and native pydantic-core gaps were fixed. These batches cover
  OpenAPI, parameters and paths, release tooling, queries, response models,
  routing, schemas, serialization, strict content types, stringified
  annotations, discriminated unions, response validation, and webhooks.
- [x] Reproducible production dependency installation and local integration
  runner for the verified FastAPI stack. `tests/fastapi/requirements.txt` pins
  the production graph, `install_dependencies.ps1` installs it with Python
  3.14 into an isolated target, and `run_fastapi_tests.ps1` now performs an
  XLang3 interpreter preflight before running any gate
  (`sys.implementation.name == "xlang3"`).
- [ ] Validate the separately pinned upstream-test dependency environment from
  a clean target. `tests/fastapi/requirements-test.txt` records the exact
  Python test-tool versions, including Trio's complete pure-Python dependency
  graph, and `install_test_dependencies.ps1` installs it independently from
  the production dependency target. The clean target now also pins and imports
  unmodified PyYAML 6.0.3 through its pure-Python implementation; the untouched
  FastAPI YAML tutorial passes 4/4. Trio's `_cffi_backend` boundary and the
  complete clean-environment preflight remain open.
- [ ] Execute the complete seven-project upstream matrix. The checked-in
  `tests/fastapi/upstream-matrix.json` records the exact FastAPI, Starlette,
  Pydantic/pydantic-core, AnyIO, Uvicorn, and HTTPX versions and source refs.
  `run_upstream_matrix.ps1` rejects changed Git checkouts, proves it is running
  XLang3, executes the upstream test directories without copied tests, and
  writes a log per project plus `matrix-results.json`. Its PowerShell syntax is
  validated; a passing full execution is still required.
- [x] Close the untouched FastAPI full-suite baseline. The authoritative
  fixture-safe run collected all 3,345 tests and completed with 3,315 passed,
  36 upstream skips, 4 expected failures, and one teardown-only error after
  every behavioral assertion had passed. The error is an upstream parallel
  test race: `test_custom_docs_ui/test_tutorial002.py` and
  `test_static_files/test_tutorial001.py` both create and remove the same
  repository-root `static` directory, while their `workdir_lock` decorators do
  not cover module-fixture setup or teardown. CPython 3.14.7 reproduces the
  same error with those two untouched files under two file-scoped workers (7
  passed, 1 teardown error). XLang3 passes both files serially, 7/7. Together
  these runs execute every collected upstream case without an XLang3 behavior
  failure. The 3,000-callable dependency-cache stress test passed in the full
  run. Logs: `scratch/fastapi-full-loadfile-8.log`,
  `scratch/fastapi-static-race-cpython.log`, and
  `scratch/fastapi-static-serial-xlang.log`.
  Earlier baseline fixes include
  `bytes.isascii`/`bytearray.isascii` (EmailStr/idna), reflected PEP 604 unions
  with `typing.Literal`, Pydantic 2.13 validation error URLs and filtering,
  list/tuple `__contains__`, response include/exclude serialization, and safe
  lowering of assigned binary operations with Python special methods. Dynamic
  builtin lookup now preserves monkeypatching while exporting the implemented
  `slice`, `aiter`, `anext`, `ascii`, and `input` names. Property descriptors
  returned through an overridden `__getattribute__` execute correctly,
  pydantic-core accepts bytes in lax integer validation, and ordinary Python
  classes treat Python 3.14 `__static_attributes__` as metadata instead of
  physical slots. The latter fixes multiple-inheritance instance corruption;
  XIR cache version 45 invalidates unsafe layouts. The EmailStr file passes
  4/4, Python-types passes 16/16, PyYAML passes 4/4, settings passes 12/12,
  response-model tutorial006 passes 3/3, and extra-data-types passes 4/4.
  SQLAlchemy/SQLModel table construction was repaired by distinguishing a
  normal instance `__dict__` from the payload of an actual `dict` subclass
  during membership dispatch. SQL query compilation was then repaired by
  matching CPython's rule that `dict.get()` bypasses a subclass `__missing__`
  method; `%` mapping still invokes `__missing__` through `dict.__getitem__`.
  An unmodified SQLModel request now returns `200 []` from `GET /heroes/`.
- [x] Resolve the remaining saved-baseline roots. Exception instance
  dictionaries no longer expose internal BaseException storage, restoring
  FastAPI traceback serialization. Callable `re.sub()` replacements may return
  `None`, SHA-384/SHA-512 are available through the native hashing boundary,
  and startup import roots retain the requested source ordering. The regex
  engine now handles escaped ASCII punctuation, exact-end `\z`/`\Z`, invalid
  host patterns, and selective ignore-case translation without a
  FastAPI-specific path. The CPython-oracle fixtures are
  `exception_instance_dict.py`, `re_sub_callable_none.py`,
  `sha2_complete.py`, `sys_startup_config.py`, and
  `re_escaped_punctuation.py`. The original 60 problem nodes pass 60/60, and
  the complete core fixture runner passes.
- [x] Stabilize the unmodified Visual Studio debugpy launch gate without a
  debugger-specific runtime path. Dictionary-subclass deletion now dispatches
  `__delitem__`, which keeps `OrderedDict` link state synchronized, and the
  built-in `next()` retains the VM execution lock like CPython instead of being
  classified as a blocking native call. CPython-oracle coverage lives in
  `subclass_delitem_dispatch.py` and `native_next_gil_atomicity.py`. The
  production Release binary completed 20/20 consecutive no-log launch sessions
  and the full lifecycle CTest. Temporary frame/socket/debugpy diagnostics were
  removed before the final rerun.
- [x] Restore the Release CTest gate after the current full rebuild. The latest
  rebuilt Release runtime passes **53/53** in 33.25 seconds. SQLite's public
  DB-API classes now expose `sqlite3` as their module, cursor representation
  checks use that public identity, coroutine fixture output matches CPython,
  and the negative fixture validates CPython's exact AttributeError wording.
- [ ] Complete the untouched pydantic-core scalar validator matrix. The
  dataclass validator file passes 238 tests with 11 upstream skips, and its
  accumulated regression batch passes 548 tests with 61 skips. The untouched
  date, datetime, time, and Decimal validator files now pass a combined
  696/696. The untouched timedelta validator and serializer files add 165
  passes with 2 upstream skips. This covers strict Python/JSON modes, bytes and numeric input,
  seconds/milliseconds inference, cross-platform large timestamps, schema
  bounds, UTC-aware past/future checks, timezone constraints, Decimal
  precision and scale, and non-finite values. The isolated test target
  now pins pure-Python/data `tzdata==2026.4`, allowing unchanged `zoneinfo`
  tests to use the IANA database on Windows. `math.isnan`, `math.isinf`, and
  `math.isfinite` now honor the general `__float__` protocol used by Decimal;
  `math_float_protocol.py` is the CPython oracle for that runtime rule.
  Timedelta coverage includes ISO and clock parsing, Decimal rounding,
  signed zero, bounds, large values, duration introspection, ISO/seconds/
  milliseconds serialization, warnings, unions, and typed dictionary keys.
  Container `repr()` now dispatches each element's Python `__repr__`, with
  recursion handling, as verified by the CPython-oracle fixture
  `container_dynamic_repr.py`.
  The untouched UUID validator file passes 97/97, covering strict Python/JSON
  behavior, string subclasses, raw and textual bytes, detailed parsing
  diagnostics, versions 1/3/4/5/6/7/8, non-RFC variants, copy/deepcopy, and
  wrapped validation. The complete untouched URL validator file passes **378
  tests with 6 upstream expected failures**. Its native boundary now covers
  WHATWG preprocessing and strict diagnostics, special/file/opaque schemes,
  malformed authority transitions, IPv4/IPv6 canonicalization, IDNA and
  Unicode host presentation, component percent encoding, lax bytes and strict
  inputs, empty-path configuration, defaults, allowed schemes, identity and
  cross-type conversion, `Url.build`/`MultiHostUrl.build`, implicit ports,
  schema invariants, and upstream vulnerability cases. The authoritative
  Release run completed in 7.72 seconds. The complete untouched URL serializer
  file also passes **10/10**, covering wrong-type warnings, normal and JSON
  modes, typed and inferred dictionary keys, custom serializers, subclasses,
  and pickle round trips. `any_schema()` now recursively infers native URL
  values in JSON mode, and native URL reduction reconstructs through the public
  constructor. Remaining untouched serializer files are the current
  pydantic-core frontier.
  The adjacent untouched UUID, Decimal, datetime/date/time, and timedelta
  serializer files also pass together with URL: **138 passed, 1 upstream
  skip**. General serialization now emits scalar type warnings, infers JSON
  dictionary keys without an explicit key schema, renders UTC as `Z`, supports
  configured temporal seconds/milliseconds output, preserves timestamp
  precision, and retains timedelta's distinct fixed-decimal key behavior.
  The untouched bytes serializer file passes **16/16**. General runtime
  support now exposes bytes methods on bytes subclasses and bytes Enum members,
  preserves exceptions raised while `str.join()` consumes an iterator, and
  gives native packages byte views for bytes-backed instances. Pydantic-core
  distinguishes ordinary CPython decoder diagnostics from its own invalid
  UTF-8 serialization errors, supports UTF-8/base64/hex values and keys, honors
  model configuration, and recursively serializes models through the public
  `to_json()` helper. `bytes_subclass_protocol.py` is the local CPython oracle.
  The untouched complex serializer file passes **13/13**. The adjacent none,
  nullable, literal, simple scalar, and string serializer files pass together:
  **98 passed, 1 upstream skip**. General native-core serialization now rejects
  empty literal schemas, selects tagged-union serializers through mapping or
  attribute discriminators, unwraps enum literals, supports arbitrary-precision
  integers and numeric/string subclasses in JSON mode, recursively serializes
  warning fallbacks, implements all `ser_json_inf_nan` modes, emits correct
  `ensure_ascii` escapes and surrogate pairs, truncates large warning values,
  and honors warn/none/error policy with `PydanticSerializationError`.
  The core runtime now applies inherited `int.__float__` to integer subclasses
  and preserves passthrough identity for its unboxed NaN representation;
  `numeric_subclass_conversion.py` matches CPython for both rules. The
  untouched enum and format serializer files pass **20/20**. The complete
  untouched any serializer file passes **99 tests with 2 upstream Windows
  skips**, covering recursive structures, include/exclude, public fallback,
  slotted dataclasses, generators/iterators, regex, pathlib/IP address types,
  non-finite float policies, arbitrary-precision subclasses, and public helper
  modes. The untouched dataclass serializer file passes **15 tests with 1
  expected failure**, including schema exclusion, computed fields, init-only
  fields, and alias precedence. The untouched definitions serializer file
  passes **7/7**, with duplicate-ref rejection and later-sibling reference
  resolution. The untouched recursive-definitions file passes **5/5**. The
  untouched dictionary serializer file passes **28/28**, and the untouched
  function serializer file passes **30 tests with 2 upstream platform skips**.
  This adds pretty JSON indentation, recursive untyped mappings, schema and
  runtime selectors, native `SerializationInfo`/`SerializationCallable`,
  return-schema warnings, model field predicates, exception chaining, and
  unexpected-value fallback. The full 804-test serializer probe most recently
  passes completely with **797 passed, 6 upstream skips, and 1 expected
  failure** across all **804 collected tests**. Generator **6/6**, infer
  **3/3**, JSON **4/4**, JSON-or-Python **2/2**, list/tuple **63/63**, and
  literal **6/6** pass untouched. The complete untouched model serializer file
  passes **67/67**, including computed fields, field serializers, extras,
  aliases, nested selectors, missing-field warnings, exception wrapping,
  round-trip mode, and computed-field exclusion. Model-root passes **8/8**,
  other passes **7/7**, and `serialize_as_any` passes **12/12**, including
  subclasses, unrelated/nested models, typed dictionaries, root models,
  recursive models, and field serializers. Root-model validation now stores
  arbitrary validated root payloads correctly, and unrelated model schemas do
  not read another model's Pydantic extras. Set/frozenset fallback passes
  **7/7** without false cycle detection. Simple scalar serialization passes
  **39 tests with 1 upstream skip**; the exact Any/typed-float non-finite
  regression passes **13/13**. Rerun the serializer directory and continue
  with the remaining untouched validator matrix. The untouched TypedDict serializer file passes
  **37/37**, including input order, aliases, allowed extras, custom extra
  schemas, selectors, and nested field serializers receiving the correct
  owner even when the field value is `None`.
  The untouched union serializer file passes **106/106**, covering literals,
  structural TypedDict selection, exact/fallback branches, serializer
  rejection, nested model unions, tagged unions, discriminator alias paths,
  JSON keys, and combined branch diagnostics.

Do not mark FastAPI compatible based on imports or a narrow smoke test alone.

After `1849241`, the native CFFI boundary gained zero-copy `ffi.from_buffer`
using XLang3's general buffer export API. CPython 3.14 borrowed-buffer oracle
cases pass. The unchanged Trio probe now reaches `poll_info.Handles` and
failed on missing general CFFI struct-field access. Generated CFFI struct
fields, nested arrays/structs, primitive and pointer writes, and CData hashing
now match the new CPython 3.14 oracle. An unchanged direct Trio `trio.run`
probe finishes, the targeted Starlette Trio cases pass 2/2, and the full
untouched Starlette `test__utils.py` passes 15/15. Wider untouched suites remain
open.
The untouched AnyIO matrix currently stops during collection because its
`trustme` test dependency imports unavailable `cryptography`; no AnyIO test
result is claimed from that attempt.
HTTPX collection has the same `trustme`/`cryptography` dependency gap. Uvicorn
collects 1337 tests with its lockfile-pinned pure-Python `websockets` wheel,
but one test module still cannot collect without a true native `httptools`
boundary. The full Starlette matrix reached 324 passes and 2 expected failures
before an unchanged Windows test failed identically on CPython 3.14; the exact
case is recorded as an explicit platform deselection for the next run.

2026-09-23 checkpoint: `_cffi_backend` now compiles bundled Windows x64 libffi
source into its native package and supports the generated Trio Windows API
declarations, real scalar/pointer foreign calls, owned pointer/array memory,
`getwinerror`, and pointer comparison/indexing without a CPython runtime.
`tests/fastapi/run_cffi_type_layout.ps1` matches CPython 3.14. CLI fixtures
pass, including `signal.set_wakeup_fd`, socket constants, and Windows
`OSError` constructor mapping. The direct Trio probe still fails at
`RuntimeError: must be called from async context` in its generated Windows I/O
module. Full Trio and untouched Starlette Trio coverage remains open.

2026-09-23 merge checkpoint: the upstream pydantic-core validator dependency
`pytest-run-parallel==0.10.0` is installed as a pure Python wheel in the
isolated test target and pinned in `requirements-test.txt`. The unchanged
validator directory collects 4470 tests; an initial broad run reached 1226
passes, 115 upstream skips, and one failure before stopping. That failure was
caused by a stale XLang bytecode cache for an unchanged archived source file,
so the runtime's bytecode magic was bumped and the upstream matrix runner now
uses an isolated cache prefix. With fresh bytecode, the untouched float and
decimal validator files pass 691/691. The two new core fixtures verify stale
bytecode invalidation and `_ast.Assert` round-trip behavior. Targeted
untouched tagged-union validation now passes its first 46 cases, including
path discriminators, callable discriminators, numeric choice locations, and
`from_attributes` input. It next fails at the upstream custom-error contract;
this and the broader validator matrix remain open. Three unchanged model
validator tests also fail under `--assert=plain` on both XLang and CPython
because they rely on pytest assertion rewriting; XLang's general `_ast`
coverage for rewriting remains incomplete. The Release build, core fixtures,
FastAPI local gate (including Uvicorn HTTP/HTTPS), and 53/53 CTest tests pass.
These results are a checkpoint, not a full FastAPI compatibility claim.

2026-09-23 validator continuation (uncommitted on `fastapi-compatibility`):
the untouched pydantic-core tagged-union file passes **48/48** after matching
custom error types and messages. The untouched TypedDict file passes **254
tests with 1 upstream skip** after correcting schema-level config precedence,
non-string extra-key errors, explicit required/default conflicts, `on_error`
constraints, and alias locations. The untouched time/timedelta/tuple files
pass **257 with 1 upstream skip**. The untouched URL/UUID/default files pass
**605 with 8 upstream expected failures**. The union file currently reports
**67 passed, 1 upstream expected failure, 9 failed assertions, and 7 fixture
errors**. The same 7 class-fixture errors reproduce on CPython 3.14 with the
current `--assert=plain` test setup; the 10 assertions remain genuine union
compatibility work. A CPython 3.14 oracle confirms the native fixes for union
custom errors, int/float exactness, explicit case labels, single-choice
collapse, and JSON-mode UUID, date, and time ranking. Public FastAPI TestClient routes cover tagged unions,
TypedDict-like allowed extras, and numeric union requests in the local gate.
The full Release build, expanded FastAPI gate, core fixtures, and 53/53 CTest
tests pass. The full seven-project upstream matrix, smart union field-count
ranking, and broader production load/soak gates
remain open; none of these partial passes constitutes full compatibility.

2026-09-23 smart-union continuation (uncommitted): general native union
ranking now tracks successful structured branches by validated input fields,
nested field counts, and exactness while preserving branch-order ties. The
unchanged upstream union file passes **83 tests with 1 upstream expected
failure**, matching CPython 3.14 exactly when both runs filter a pytest 9
`PytestRemovedIn10Warning` for upstream class-scoped instance fixtures. This
filter changes no test code or selection. CPython's full untouched
pydantic-core suite collected **5934** tests and under `--assert=plain`
reported **5791 passed, 130 skipped, 10 expected failures, 3 failed**; all
three failures are assertion-detail checks that pass under CPython's pytest
assertion rewriting. XLang's full project matrix run is in progress. The
PowerShell matrix runner's default path initialization, JSON array flattening,
empty selection guard, and native stderr handling were corrected so the
selected project actually executes and its exit code is reported. The broad
XLang project run was interrupted for a requested commit and merge checkpoint.
An independent top-level pydantic-core run found the next native API gap during
collection: `pydantic_core._pydantic_core.list_all_errors` is not exported.
The pinned upstream API exposes 104 error definitions; this catalog and
`PydanticKnownError` behavior require native implementation before the full
suite can collect. Full FastAPI compatibility remains unverified.

2026-09-23 error-catalog continuation (uncommitted on
`fastapi-compatibility`): the native `list_all_errors` export now returns the
complete 104-entry catalog for pinned pydantic-core 2.46.5. A canonical digest
of every entry, field, and order matches CPython 3.14; the two unchanged
upstream `test_all_errors*` cases pass on XLang3. The previously blocked
`tests/test_errors.py` now collects, revealing a separate genuine gap:
`PydanticCustomError` and `PydanticKnownError` are currently generic exception
classes and lack their public `message()`, template, type, context, and
rendering semantics. The first full-file attempt stopped at five such failures
under `--maxfail=5`. This native exception behavior is the next boundary to
implement; a passing catalog does not imply a passing error suite or full
FastAPI compatibility.
The focused FastAPI gate including the catalog oracle, Uvicorn HTTP/HTTPS,
and all 53 CTest tests pass after this change.

2026-09-23 error-contract checkpoint: native `PydanticCustomError` and
`PydanticKnownError` now expose their public constructor, properties,
`message()`, string/repr, context requirements, and validation-error
conversion contracts. `ValidationError.from_exception_data` supports known
and custom errors, JSON/Python message templates, input and URL options,
and robust JSON serialization of otherwise unserializable inputs. The
catalog and public FastAPI error-contract oracle fixtures match CPython
3.14 with pinned pydantic-core 2.46.5. The untouched upstream
`tests/test_errors.py` reports **191 passed, 1 skipped, 3 failed** under
XLang3; the three open failures all require `validation_error_cause` and
ExceptionGroup/traceback behavior. This checkpoint does not establish full
FastAPI compatibility or completion of the seven-project matrix. The
expanded local FastAPI gate, including Uvicorn HTTP/HTTPS, and all 53 CTest
tests pass on the checkpoint build.

2026-09-23 error-cause continuation (uncommitted on
`fastapi-compatibility`): configured `validation_error_cause=True` now
retains original user `ValueError` and `AssertionError` objects, adds
location notes, and attaches an `ExceptionGroup` cause to the native
`ValidationError`. This preserves explicit user exception chains and
tracebacks. All **194** tests in untouched pydantic-core
`tests/test_errors.py` pass, with **1** upstream skip. A new public
FastAPI/Pydantic model-validator fixture matches CPython 3.14 for the
cause chain, note, traceback, accepted request, and HTTP 422 response.
The expanded FastAPI local gate including Uvicorn HTTP/HTTPS and all
**53/53** CTest tests pass. The
full seven-project upstream matrix and broader production load/soak gates
remain open.

2026-09-23 upstream expansion (uncommitted): untouched
`tests/test_custom_errors.py` passes **4/4** after native
`PydanticCustomError.__new__` preserves subclass-transformed templates and
`ValidationError.from_exception_data` recomputes messages from type/context
instead of accepting a caller-supplied `msg`. The expanded public FastAPI
error-contract fixture matches CPython 3.14 for both subclass cases and an
HTTP 422 response. The general XLang3 `bytes.count`/`bytearray.count`
methods now accept integers and `__index__` objects with CPython range and
type errors; inherited `int.__index__` works on integer subclasses such as
`IntEnum`. A CPython 3.14 bytes fixture covers these boundaries. The
untouched pydantic-core Hypothesis file progressed past the former
`bytes.count` and `int.__index__` failures. A general set-union hash index
reduced Hypothesis's loaded-source scan from about 40 to 3.3 seconds, and
its first generated datetime test passes. Native `zlib._ZlibDecompressor`
now accepts Python 3.14's `wbits=` keyword; the Hypothesis gzip Unicode
cache loads and parses. A later untouched `test_urls_text` still ends the
XLang3 process with exit code 3 during Hypothesis text generation, before
Pydantic URL validation. This is an open general runtime gap. Set union also
still has a pre-existing equality gap for distinct custom objects with
equal user-defined `__hash__` and `__eq__`. The full upstream matrix remains
open; these changes are a compatibility checkpoint, not a completion claim.
The Release build, all **53/53** CTest checks, the expanded local FastAPI
gate including live Uvicorn HTTP/HTTPS, and the untouched upstream
`test_errors.py` plus `test_custom_errors.py` (**198 passed, 1 skipped**)
pass on this checkpoint.

2026-09-23 Unicode/Hypothesis continuation (uncommitted on
`fastapi-compatibility`): the generic built-in `sum` now accepts Python
3.14's `start=` keyword, with a CPython oracle fixture. A missing gzip
cache exposed a runtime finalizer failure when a bare `except` released an
exception while the cross-thread exception-registry mutex was held. The
registry now retires values after unlocking; a public gzip missing-file
fixture and cold Hypothesis Unicode-cache generation pass. General
`_IOBase.close()` is now idempotent for already closed streams, eliminating
an unraisable finalizer error from gzip's decompressor. Its CPython oracle
fixture passes. The **unchanged** pydantic-core `tests/test_hypothesis.py`
now reports **10 passed, 1 skipped** on XLang3 under `--assert=plain` and
the same upstream pytest deprecation-warning filter used on CPython. This
does not establish the untouched seven-project matrix or full FastAPI
production compatibility.
The unchanged upstream `tests/validators/test_url.py` also passes
**378 tests with 6 upstream expected failures**. A full pydantic-core
matrix attempt was interrupted after more than five minutes of active,
CPU-bound collection with no test report yet; this is not a test failure or
a complete matrix result. The runner starts with a fresh bytecode cache, and
the focused URL file itself needed roughly 40 seconds before its first
collection output. The complete entry must be rerun to a terminal result.
The follow-up collection trace found that all **5,934** pydantic-core cases
collect in about 50 seconds; pytest then spent minutes inside its fixture
reordering hook. XLang3 dictionary operations on 3,000 distinct object keys
took 2.6 seconds to build and 6.2 seconds to look up, versus effectively
instant CPython 3.14 timings. A runtime hash-bucket index now preserves
collision equality and insertion order while reducing the same XLang3 probe
to about **0.006 seconds to build and 0.011 seconds to look up**. Native
`dict.fromkeys()` and `dict.update()` use runtime-aware key hashing, and
user `__hash__` exceptions retain their original Python type and message.
A CPython 3.14 oracle fixture covers identity keys, collisions, deletion,
clear/reinsert, and hash errors. The complete, untouched pydantic-core
matrix entry now collects all **5,934** cases and reaches a terminal result:
**861 passed, 6 skipped, 1 xfailed, 5 failed** before the configured
`--maxfail=5` stop. The five failures are the nested definition-model
recursion benchmark, date validation from a datetime string, and three
serializer warning contracts (enum/any, variadic tuple, and union fallback).
The same five cases pass on CPython 3.14 (**5/5**). They remain open native
Pydantic-core compatibility gaps; this checkpoint does not establish full
FastAPI compatibility or completion of the seven-project matrix.

2026-09-23 native date/tuple continuation (uncommitted on
`fastapi-compatibility`): lax `date` validation now accepts ISO datetime
strings only when their time is exactly midnight, preserving the
`date_from_datetime_inexact` error for non-midnight values. The unchanged
upstream `TestBenchmarkDateX.test_date_from_datetime_str` passes, and a
CPython 3.14 oracle covers Python/JSON inputs plus HTTP 200/422 FastAPI
requests. Variadic tuple serialization no longer warns for inputs shorter
than the fixed schema prefix; non-variadic tuple length warnings remain.
The unchanged upstream `test_function_positional_tuple` passes, and another
CPython 3.14 oracle covers tuple lengths zero through four and a FastAPI
response. The complete pydantic-core matrix entry now reaches **1,182
passed, 9 skipped, 1 xfailed, 5 failed** before `--maxfail=5`. Its five
failures are recursive definition-model validation, enum/any serialization
warning, union-of-functions warning, typed-dict/literal union output, and
validator-iterator garbage collection. The last two pass unchanged on
CPython 3.14. A probe showed the recursive schema currently hits an
artificial 12-active-definition cap; increasing it toward the advertised
99 without restructuring native stack use made deep inputs terminate the
process, so that unsafe experiment was reverted. The recursion gap remains
open. Full seven-project matrix, production load/soak, and final demo
verification remain open.

2026-09-23 serializer/validator continuation (uncommitted on
`fastapi-compatibility`): `simple_ser_schema('any')` now replaces its parent
serializer, including an enum parent, rather than falling through to the
enum serializer. The unchanged upstream `test_any.py` reports **99 passed,
2 skipped**. A format serializer left inactive in Python mode now returns
the value without a spurious type warning; union serialization also ranks
branches with matching literal fields when no exact branch matches. The
unchanged upstream `test_union.py` reports **106 passed**. Python and JSON
root validators both convert uncaught `PydanticOmit`/`PydanticUseDefault`
to `SchemaError`; `isinstance_python` suppresses only `ValidationError`
and propagates internal errors. The unchanged upstream `test_isinstance.py`
reports **6 passed**. Public CPython 3.14 oracle fixtures cover each of
these behaviors and FastAPI requests. The complete pydantic-core matrix
reached **1,235 passed, 10 skipped, 1 xfailed, 5 failed** before
`--maxfail=5`; the failures were deep recursive definition validation,
validator-iterator garbage collection, Hypothesis multi-host URL text
generation (`unsupported operands for -` in pure Python code), invalid
JSON input type, and malformed JSON error text. The last two JSON cases
were fixed after that run: `validate_json` now rejects non-text input with
`json_type`, and its parser-error bridge reports CPython-matched EOF and
trailing-comma details. Both unchanged focused upstream tests pass, and
a FastAPI request oracle matches CPython. A separate full `test_json.py`
run now reports **36 passed, 14 failed**; those failures concern broader
JSON serialization, fallbacks, cycles, partial parsing, and encoded bytes.
The full Release build, **53/53** CTest checks, and expanded FastAPI gate
including the new CPython-oracle requests and live Uvicorn HTTP/HTTPS passed
at this stage.

2026-09-23 IntEnum/URL continuation: general runtime subtraction now accepts
numeric subclasses such as `IntEnum`, matching existing addition and
comparison behavior. A CPython 3.14 oracle covers reverse subtraction,
numeric subclasses, and an overridden `__sub__`; a public FastAPI request
exercises the same behavior. Native URL parsing now trims leading/trailing
C0/space characters, percent-encodes control characters in path/query/fragment,
and rejects them in the host with the CPython-matched error. A differential
probe and public FastAPI request oracle cover URL and multi-host URL behavior.
The unchanged upstream Hypothesis and URL test files report **388 passed,
1 skipped, 6 xfailed**. The complete untouched pydantic-core matrix collects
**5,934** cases and now reaches **1,238 passed, 10 skipped, 1 xfailed,
5 failed** before `--maxfail=5`. The five failures are deep recursive
definition-model validation, validator-iterator garbage collection, and
three general JSON serialization/fallback cases. Full seven-project matrix,
production load/soak, final demo verification, and full FastAPI compatibility
remain open.
The full Release build, **53/53** CTest checks (including the fixture gate),
and expanded FastAPI gate (including all new CPython-oracle requests and live
Uvicorn HTTP/HTTPS) pass on this checkpoint.

2026-09-23 JSON continuation (uncommitted on `fastapi-compatibility`):
`to_jsonable_python` now uses the native schema serializer instead of
delegating to Python `json.dumps`, so sets, bytes, exclusions, unknown-object
fallbacks, and inferred Pydantic serializers use the same general path as
`to_json`. Top-level JSON functions now honor `ensure_ascii`,
`serialize_unknown`, and the public serialization selectors; dictionary keys
also pass through the serializer before becoming JSON keys. Native bytes
validation handles unpadded base64 and hex input with matching invalid-symbol
and invalid-character errors. A CPython 3.14 oracle covers these behaviors
and a real FastAPI request. The complete untouched upstream `test_json.py`
file improved from **36 passed, 14 failed** to **50 passed, 0 failed**.
Correcting `repr(SomeClass)` to dispatch through a custom metaclass is a
general runtime fix; the native JSON functions now also handle failing
representations and partial JSON parsing. The full pydantic-core matrix,
run before those last two fixes, reached **1,268 passed, 10 skipped,
1 xfailed, 5 failed** before `--maxfail=5`: deep definition recursion,
validator-iterator GC, the two then-open JSON cases, and prebuilt validator
use. Nested prebuilt validator and serializer selection is now shared across
execution and debug output, with an active-engine guard to avoid recursive
self-selection. The untouched upstream `test_prebuilt.py` file reports
**9 passed**, and a CPython 3.14 oracle plus FastAPI request covers the
nested-model boundary. The full Release build, **53/53** CTest checks, and
expanded FastAPI gate including both new request oracles and live Uvicorn
HTTP/HTTPS pass. The complete pydantic-core matrix has not yet been rerun
after all these changes. The full seven-project matrix, full FastAPI
compatibility, production load/soak, and final demo verification remain open.

2026-09-23 schema continuation (uncommitted on `fastapi-compatibility`):
The complete pydantic-core matrix now reaches **1,399 passed, 10 skipped,
1 xfailed, 5 failed** before `--maxfail=5`; the five are deep definition
recursion, validator-iterator GC, `is-subclass` on non-class input, unknown
schema type acceptance, and `PydanticKnownError` string formatting. The last
three were fixed after that run. Non-class input now produces a
`is_subclass_of` validation error, while actual classes still use runtime
`issubclass`; untouched `test_schema_functions.py` reports **80 passed**.
Schema construction checks the pinned core schema and field-type vocabulary
and raises `SchemaError` for unknown types. General `KeyError.__str__`
uses its argument's representation as on CPython. Untouched `test_typing.py`
reports **10 passed**. CPython 3.14 oracles and public FastAPI requests cover
both fixes. The full matrix and local gates have not yet been rerun after
these last changes; the full seven-project matrix, production load/soak,
demo verification, and full FastAPI compatibility remain open.

2026-09-23 generator GC continuation (uncommitted on
`fastapi-compatibility`): the next complete untouched pydantic-core matrix
reached **1,408 passed, 10 skipped, 1 xfailed, 5 failed** before
`--maxfail=5`. The remaining failures at that point were deep definition
recursion, validator-iterator GC, and three timezone/context-manager cases.
The native validator iterator now registers all owned values as GC edges,
including its live field-data dictionary. General runtime cycle collection
now follows those native edges through dictionaries and clears only isolated
components. VM liveness analysis also releases container literals created
inside a loop after their last use; otherwise the final input dictionary
remained rooted after `del` of the local value. The unchanged upstream
`test_gc_validator_iterator` function passes when invoked directly, and a
new CPython 3.14 oracle verifies iterator identity and zero retained inputs
after 1,000 model validations. The pytest wrapper prints the test as passed
but currently exits with a separate traceback-rendering failure during its
own teardown. A Release CTest fixture rerun passes after adjusting built-in
`iter` to recognize both native iterators and Python `__next__` methods.
The full Release build, **53/53** CTest checks, and expanded FastAPI gate
including the generator GC oracle and live Uvicorn HTTP/HTTPS pass. The
fresh non-benchmark, untouched pydantic-core matrix reports **1,252 passed,
10 skipped, 1 xfailed, 5 failed** before `--maxfail=5`; the five are four
timezone cases and `validate_strings` string-to-bool handling. A separate
matrix attempt including benchmarks stopped on the known deep-definition
recursion failure and four pytest 10 fixture-deprecation setup errors.
The complete seven-project matrix, production load/soak, final demo
verification, and full FastAPI compatibility remain open.

2026-09-23 string and timezone continuation: strict
`SchemaValidator.validate_strings` now
parses bool, int, float, and temporal text in its string-input mode while
keeping strict Python-object validation distinct. Strict string-mode
dataclass validation accepts field dictionaries. The unchanged upstream
`test_validate_strings.py` reports **21 passed**; a CPython 3.14 oracle and
public FastAPI requests cover valid, malformed, and dataclass inputs.
Native `TzInfo` now implements offset-based ordering, `NotImplemented`
for unrelated objects, and `fromutc` conversion. General `%` execution
dispatches Python `__mod__`/`__rmod__` before native numeric fallback,
allowing Python 3.14's unmodified `timedelta` modulo method to run. The
unchanged upstream `test_tzinfo.py` reports **14 passed, 26 subtests
passed**; a CPython 3.14 oracle and public FastAPI request cover modulo,
ordering, and `fromutc`. The full Release build, **53/53** CTest checks,
and expanded FastAPI gate including live Uvicorn HTTP/HTTPS pass. The
broader non-benchmark pydantic-core matrix is running; the seven-project
matrix, production load/soak, and final demo gates remain open.

2026-09-23 upstream continuation: after the string and timezone fixes, the untouched
non-benchmark pydantic-core suite reached **4,957 passed, 130 skipped,
1 xfailed, 4 failed, 1 setup error** before `--maxfail=5`. One assertion
was a real dataclass union-location mismatch: `cls_name` must label the
branch, while the dataclass-args schema name belongs in the message and
context. The native schema-label fix passes the unchanged upstream case;
a CPython 3.14 oracle and public FastAPI request match both names. The
three model root-validator assertions fail identically on CPython 3.14
when pytest is invoked with `--assert=plain`; they require pytest assertion
rewriting, which XLang3 does not yet support for the full generated AST.
The setup error is a `PytestRemovedIn10Warning` caused by the pinned
upstream test's class-scoped instance fixture; CPython 3.14 produces the
same error with this pytest 9.1.1 test environment. The checked-in matrix
runner already filters that exact deprecation warning. A larger diagnostic
matrix with that filter stopped on a timeout in Python 3.14's `weakref.py`
during a later test, without establishing a new compatibility result. Full AST
round-trip and the other project suites remain open.

2026-09-23 AST and lazy-module continuation on `fastapi-compatibility`
(uncommitted): the unchanged FastAPI 0.141.1 suite collected **3,335 tests
with 10 collection skips** and passed through its first 4% without a failure.
That serial diagnostic run was interrupted to rebuild the runtime; it is not
a full-suite pass. Parser-backed `_ast` now preserves `Import`, `ImportFrom`,
`Dict`, and `Set`, and `compile(ast.Module(...))` lowers them to general
XLang3 statements/expressions. The parser-backed AST conversion rejects
unsupported call arguments instead of emitting invalid child nodes.
CPython 3.14 oracle fixtures cover import aliases, relative import levels,
dictionary unpacking, set literals, and execution from an AST. Pytest's
unchanged assertion rewriter now gets past imports and dictionaries; its next
unsupported node is `Raise`, so rewrite support is still incomplete.

The fuller AST exposed AnyIO's ordinary lazy-import path. XLang3's fused
module-method call had bypassed module `__getattr__` when the attribute was
not stored yet, causing unchanged Starlette `TestClient.__enter__` to fail on
`anyio.create_memory_object_stream`. The fused path now resolves the
module's `__getattr__` and invokes the returned callable. A CPython-oracle
module fixture and a public FastAPI TestClient route both pass. The Release
build, all registered fixtures, **53/53** CTest checks on final rerun, and
the full local FastAPI gate including live Uvicorn HTTP/HTTPS pass. An
earlier CTest run had the previously observed intermittent large-response
network failure; it passed in isolation and in the full rerun. The seven-
project untouched matrix, complete pytest assertion rewriting, full Trio
CFFI calls, load/soak, and final demo verification remain open.

2026-09-23 further upstream AST audit (uncommitted): parser-backed `_ast`
and `compile(ast.Module(...))` now also handle `Raise` with and without a
cause. A CPython 3.14 oracle fixture passes. A minimal untouched pytest
assertion-rewrite probe now produces a rewritten assertion failure rather
than an AST conversion error; this does **not** establish full rewrite
compatibility for upstream pydantic-core tests. The untouched FastAPI
WebSocket and streaming/cancellation selection passes **25/25** with the
matrix runner's existing AnyIO deprecation-warning filter. The full
3,335-test FastAPI suite remains unfinished. HTTPX suite collection stops
because its test dependency `trustme` imports `cryptography`, whose CPython
binary extension cannot be loaded by XLang3. Uvicorn collects 1,324 tests
but stops on one collection error: `test_server.py` imports the unavailable
native `httptools` HTTP parser. Both require genuine native dependency
support or a justified upstream test configuration, not a FastAPI-specific
shim. The registered CPython-oracle fixture runner passes after the `Raise`
change. The final CTest rerun passes **53/53**, and the full local FastAPI
gate passes, including live Uvicorn HTTP/HTTPS. The full upstream suites,
native HTTPX/Uvicorn test dependencies, load/soak, and final production
demo validation remain open.

2026-09-23 Uvicorn continuation (uncommitted): a diagnostic run of the
untouched Uvicorn suite excluding only `test_server.py` exposed a real
circular-import diagnostic mismatch. Source-backed modules now set and
clear `__spec__._initializing` around execution, and failed `from` imports
from an initializing module report the CPython 3.14 circular-import message.
The unchanged Uvicorn `test_circular_import_error`, a CPython-oracle
fixture, and a public FastAPI TestClient route pass. The next diagnostic
failure came from `truststore`'s `str | bytes | typing.Callable[...]`
annotation. Native union operands now retain arbitrary members as Python
3.14 does; the unchanged Uvicorn logging test, a CPython-oracle fixture,
and a public FastAPI route importing `truststore` pass.

The next Uvicorn failure was h11's `int(chunk_size, base=16)` while parsing
a chunked response. XLang3's inline `int` constructor now accepts the
`base` keyword and checks explicit-base operands before its numeric fast
paths. The unchanged Uvicorn `test_unknown_status_code`, a CPython-oracle
fixture, and a public FastAPI route pass. The broader diagnostic advanced
past these failures to roughly 21% before a 60-second timeout in an
`a2wsgi` WSGI request-body read; that boundary remains under audit. The
Release build and all registered fixtures pass. A concurrent CTest run
timed out only in the Visual Studio debugpy smoke test; an isolated rerun
and a sequential full rerun both pass **53/53**. The final FastAPI gate
rerun after the `int` fix passes, including live Uvicorn HTTP/HTTPS.

WSGI follow-up: the unchanged Uvicorn
`test_wsgi_put_more_body[WSGIMiddleware]` times out at both 20 and 180
seconds on XLang3, while the same test passes on CPython 3.14 in **0.29
seconds**. The test streams 1 MiB in 1,024 chunks through `a2wsgi`'s
worker-thread `asyncio.run_coroutine_threadsafe(...).result()` path.
Thread stacks vary between the worker's future wait and the event loop's
callback/weakref iteration, so a deadlock is not established. A standalone
32-iteration coroutine handoff takes **2.031 s** on XLang3 versus **0.032
s** on CPython; 128 iterations take **12.127 s** on XLang3. This points to
severe growing per-handoff overhead in general asyncio/thread scheduling.
No timing gate or workaround has been added; this is an open runtime
performance/concurrency investigation.

2026-09-23 native IOCP continuation (uncommitted): the event-loop
slowdown came from XLang3's `_overlapped.GetQueuedCompletionStatus`
returning immediately on an empty queue regardless of its timeout.
Python 3.14's Windows Proactor loop then spun and competed with worker
threads. The native boundary now waits for queued completion or timeout,
polling native socket/registered-handle operations as needed while
releasing the runtime execution lock. The unchanged Uvicorn streamed-WSGI
test passes in **8.37 s** under its normal 60-second timeout. A CPython
3.14 oracle verifies timeout and cross-thread completion, and a public
FastAPI route verifies 128 event-loop/worker handoffs. An earlier 32-
handoff diagnostic fell to **0.163 s** after the fix. The broader untouched
Uvicorn diagnostic advanced to **375 passed, 278 skipped** before finding
a separate `_socket.AF_IPX` omission. XLang3 now exports the platform's
native `AF_IPX` constant; the unchanged Uvicorn socket-utility file passes
**6/6**, as do its CPython-oracle fixture and public FastAPI route. The
full Uvicorn suite still cannot collect `test_server.py` until true native
`httptools` support is present. Final Release build passed; fixture,
CTest, and complete FastAPI gate reruns on this final code pass, including
live Uvicorn HTTP/HTTPS. The full untouched upstream matrix, genuine native
`httptools` and `cryptography` dependencies, production load/soak, and final
demo verification remain open.

2026-09-23 WebSocket continuation (uncommitted): the untouched Uvicorn
WebSocket suite exposed missing `bytearray * int` support in the pure-Python
websockets masker. General `bytearray` repetition now matches a CPython 3.14
oracle, a public FastAPI WebSocket route, and the exact upstream text-frame
case. A later 16 MiB frame timed out in XLang3's quadratic `int.from_bytes`
and `int.to_bytes` bigint conversion loops. Direct 32-bit-limb packing and
extraction now pass a 1 MiB CPython-oracle round trip, a 1 MiB public FastAPI
WebSocket frame, and the exact untouched 16 MiB Uvicorn case in **3.82 s**.
The current WebSocket run reached **49 passed, 237 skipped** before an
upstream test referenced `_WSProtocol` without importing it. CPython 3.14
produced the identical failure in that incomplete test environment. The
checkout pins pure-Python `wsproto==1.3.2`; it is now in the reproducible test
requirements and installed locally. With wsproto enabled, collection exposes
a new XLang3 `NameError: b` in wsproto's nested comprehension
`[bytes(a ^ b for a in range(256)) for b in range(256)]`. This is an open
general comprehension-scope defect. The final full fixture, CTest, and
FastAPI gates have not yet been rerun after the bigint change; the targeted
oracle, public route, and unchanged upstream large-frame case pass.

2026-09-23 nested-comprehension continuation (uncommitted): the missing
`wsproto` test dependency is now pinned to the upstream `wsproto==1.3.2`.
Its import exposed a general compiler closure-capture defect: generator
expressions nested inside comprehensions could not see the outer iteration
variable. Hidden comprehension targets now participate in closure analysis;
generator lowering carries active aliases; captured targets in ordinary and
async loops are promoted to cells after nested bodies are lowered; the fused
constant-range iteration path updates any captured cell. CPython 3.14 and
XLang3 match on constant, dynamic, deferred, and async nested-comprehension
cases. A public FastAPI TestClient route using a dynamic range passes on
both runtimes. The exact previously blocked Uvicorn `wsproto` case passes.
The full untouched Uvicorn WebSocket file passed **143 tests, 295 skipped**
on the final compiler build; the skips are predominantly missing native
`httptools`/`zttp` parser variants. The final Release build, all registered
fixtures, **53/53** CTest checks, and the complete local FastAPI gate pass,
including the new route and live Uvicorn HTTP/HTTPS. The broader untouched
matrix, native dependency boundaries, and load/soak remain open.

2026-09-23 Uvicorn multiprocessing continuation (uncommitted): the
untouched HTTP-plus-WebSocket protocol selection passed **234, skipped 475**.
The broader all-except-`test_server.py` diagnostic later stalled after 53%
with high CPU; both protocol files pass independently and together, so the
cross-file interaction remains unresolved. Running the other Uvicorn files
exposed two deterministic native/lifetime defects. The unchanged
`test_process_ping_pong` failed on XLang3 but passed on CPython 3.14 because
`BytesIO.getbuffer()` returned a memoryview that retained only an internal
bytearray, allowing the `BytesIO` exporter to finalize with a live export.
The view now retains the exporter through derived views and releases it after
the export count drops. A CPython-oracle lifetime fixture, a public FastAPI
route, and the exact unchanged upstream test pass.

The next unchanged `test_process_ping_pong_timeout` hung because XLang3's
native `_winapi.PeekNamedPipe(handle)` returned `(b'', 0, 0)` where CPython
3.14 returns `(0, 0)`; the standard-library poll therefore treated an empty
pipe as readable. `_winapi.PeekNamedPipe` now returns the two-int form when
size is omitted/zero and retains the three-item byte form for positive size.
A CPython-oracle named-pipe fixture, a public FastAPI route, and the exact
unchanged upstream test pass. The remaining supervisor file progresses five
tests, then `test_multiprocess_run` times out in a spawned subprocess on
XLang3; CPython passes. That worker startup/shutdown path is the current
unresolved native/process boundary. The timeout left three orphaned test
processes, which were stopped after checking their parentage. The final
Release build, all registered oracle fixtures, **53/53** CTest checks, and
the complete local FastAPI gate pass after the latest native fix, including
both new public routes and live Uvicorn HTTP/HTTPS.

2026-09-23 Uvicorn reloader continuation (uncommitted): the diagnostic
selection excluding the known blocked native/server and protocol files
reached **301 passed, 89 skipped** before the unchanged reloader test hit
`TypeError: kill() expected pid and signal integers`. Python 3.14's
`signal.CTRL_C_EVENT` is an `IntEnum`, which native `os.kill` had rejected
because it checked only the internal plain-int tag. Native `os.kill` now
uses the module's existing `__index__` conversion for pid and signal.
A CPython-oracle fixture, a public FastAPI route, and unchanged
`test_reloader_should_initialize[StatReload]` pass. The WatchFiles variant
is skipped because its optional dependency is absent. A full reloader-file
run was interrupted without a test result; no child processes remained on
inspection. The final Release build, registered fixtures, **53/53** CTest
checks, and complete local FastAPI gate pass after this `os.kill` change,
including its public route and live Uvicorn HTTP/HTTPS. The separate
`test_multiprocess_run` spawned-worker timeout remains open.

2026-09-23 Uvicorn config continuation (uncommitted): the broader diagnostic
selection reached **347 passed, 92 skipped** before `test_log_config_yaml`
raised `UnboundLocalError` inside the unmodified Python 3.14 `unittest.mock`.
The exact JSON-then-YAML upstream pair passes on CPython but failed on XLang3;
the YAML case alone passed. A reduced probe showed that deleting an attribute
from a Python module left an `Invalid` entry visible in its `__dict__`, so the
second `mock.patch` treated the deleted name as a local attribute. Both VM
`del module.attr` and generic `object_delete_attr` now call the existing
`module_delete_attr` path, which removes the namespace key and raises
`AttributeError` for a missing name. A CPython 3.14 oracle, a public FastAPI
TestClient route using two real `unittest.mock.patch` operations, and the
exact unchanged upstream pair pass. The complete unchanged Uvicorn config
file passes **118, skipped 8** (two missing optional `trustme` and six
Unix-only cases). The broader remainder and final full gates are being rerun
after this fix. The separate spawned-worker timeout and native dependency
boundaries remain open.

The final full Release build passed after the module deletion fix. All
registered CPython-oracle fixtures passed, including the new
`module_attribute_delete` fixture; CTest passed **53/53**; the complete local
FastAPI gate passed, including `module_attribute_delete_contract` and live
Uvicorn HTTP/HTTPS. `git diff --check` reports no whitespace errors. The
broader Uvicorn diagnostic selection advanced beyond the previous failure to
**86%**, then stopped making progress without a child process while its
XLang3 process remained live; that diagnostic was stopped and is not counted
as a pass. The config file passes alone and when preceded by the CLI, compat,
supervisor-signal, HTTP2, and protocol-utility files (**146 passed, 58
skipped** in the broadest of these shorter sequences). Earlier middleware or
benchmark shared state remains to be isolated. The full unmodified upstream
matrix and production load/soak remain open.

2026-09-23 Uvicorn main startup continuation (uncommitted): a verbose run
showed that the earlier apparent 86% stall was not a config failure; the run
passed config and stalled at unchanged `tests/test_main.py::test_run[default]`
after ASGI lifespan startup. The isolated CPython 3.14 case passed, while
XLang3 timed out. A minimal `asyncio.start_server(host=None, port=0)` probe
showed that the Python 3.14 stdlib needs native `socket.IPV6_V6ONLY` for the
IPv6 half of a dual-stack listener. XLang3 now exports the platform WinSock
constant. That exposed native `socket.setsockopt` rejecting `True`, though
Python treats bool as an integer; it now accepts the existing socket
integer-like forms for level, option, value, and optlen. The CPython-oracle
dual-stack listener returns `IPV6_V6ONLY=27` and families `[2, 23]` under
both runtimes; a public FastAPI route does the same. The exact unchanged
Uvicorn default/hostname/IPv6 host variants pass **3/3**, and the full
unchanged `test_main.py` passes **13/13**. The broader diagnostic selection
and final full gates are being rerun after this socket change. The separate
supervisor worker timeout and genuine native dependencies remain open.

After the socket fix, the broader unchanged Uvicorn diagnostic selection
(excluding the separately tracked server/protocol/supervisor files) completed
**480 passed, 106 skipped** in 119.74 s. The skipped cases are reported by
upstream for missing optional `httptools`, `zttp`, and `trustme` dependencies
or Unix-only behavior. This selection includes all of `test_main.py` and
`test_config.py`, and the prior broad-run stall is resolved. The full Release
build and registered fixtures pass on this code; CTest and the local FastAPI
gate are completing their final rerun.

Those final gates passed on the socket change: full Release build, all
registered CPython-oracle fixtures (including module deletion and dual-stack
listener), **53/53** CTest checks, and the complete local FastAPI gate
(including both new public routes and live Uvicorn HTTP/HTTPS). The Uvicorn
upstream checkout is unchanged, and `git diff --check` found no whitespace
errors. The overall goal remains active because the unmodified full upstream
matrix, genuine native `httptools`/other optional boundaries, the separate
supervisor worker path, production load/soak, and final production-style demo
are not yet complete.

The previously timed-out unchanged Uvicorn
`tests/supervisors/test_multiprocess.py::test_multiprocess_run` now passes
individually and in the supervisor-file sequence. That file reaches **5
passed** before `test_multiprocess_health_check` fails in its Windows
subprocess; CPython 3.14 passes the exact case. A scratch-only reproduction
with two real spawned workers shows that initial spawn, killing one worker,
replacement, and both `is_alive()` checks succeed. Shutdown reaches
`terminate_all()` but blocks in `join_all()` after sending
`CTRL_BREAK_EVENT`; the diagnostic wrapper then kills the parent at its
20-second timeout, after which no XLang3 worker remains. Inspection of
XLang3's native `_signal` module shows that
`signal.signal` currently records Python handlers but does not register a
Windows console or CRT signal handler, so real OS signal delivery to Python
handlers is the next native boundary to implement. This is an evidence-based
suspected cause, not yet a verified fix. No upstream code was modified.

2026-09-23 Windows supervisor signal audit: an experimental general
`SetConsoleCtrlHandler` bridge with queued VM dispatch and wakeup-FD writes
was built and exercised, then removed because it did not pass a separate
CPython-oracle process-group case. The first scratch timeout report was
misleading: `subprocess.run` killed the parent at its 20-second timeout and
then drained worker output, so the workers' later shutdown lines did not
prove that the earlier `CTRL_BREAK_EVENT` had been delivered. A live pipe
capture showed workers remained active before the timeout. An instrumented
native `os.kill` run enumerating the parent console's attached processes
before `GenerateConsoleCtrlEvent` passed the real two-worker lifecycle 3/3;
the same run without that console enumeration failed 0/3, as did a scheduler
yield or console enumeration only when installing the handler. A separate
XLang3-parent/XLang3-child process-group probe still missed the event even
with enumeration; XLang3-parent/CPython-child passed. CPython 3.14 passed the
Uvicorn worker probe without any upstream modification. Console enumeration
was rejected as an unexplained timing-dependent workaround; the experimental
signal bridge, diagnostics, and teardown tracing are absent from the
working runtime. The unmodified upstream supervisor health test remains an
open native Windows compatibility boundary. These experiments justify neither
a signal fix nor a full compatibility claim.

2026-09-24 upstream Starlette continuation: the full unchanged Starlette
1.6.0 suite collected 1,065 cases, deselected one CPython-proven Windows
test, and stopped after **420 passed, 2 xfailed** at
`tests/test_exceptions.py::test_handled_exc_after_response[trio]`.
CPython 3.14 passes the exact case; XLang3 returned HTTP 500 instead of the
already-sent HTTP 200. A scratch-only diagnostic exposed Trio's
`WakeupSocketpair.wakeup_on_signals()` warning, promoted to an error by
Starlette's unchanged test configuration. Trio detects the main thread by
calling `signal.signal(SIGINT, signal.getsignal(SIGINT))` and expecting
`ValueError` on worker threads. XLang3 accepted that call and
`signal.set_wakeup_fd(-1)` from workers; CPython 3.14 rejects both. The
native `_signal` boundary now enforces the main-thread restriction. The
registered CPython-oracle fixture `signal_main_thread.py` and public FastAPI
sync route `signal_worker_contract.py` agree on both runtimes, and the exact
unchanged Starlette Trio case passes on the rebuilt RelWithDebInfo binary.
The full Starlette rerun is still in progress; this does not resolve the
separate Windows console-control delivery required by Uvicorn supervisors.

The first full unchanged FastAPI 0.141.1 run with a 120-second per-test limit
stopped after **418 passed, 17 skipped** at
`tests/test_frontend.py::test_symlink_outside_directory_is_not_served`.
CPython 3.14 passes the exact case; XLang3 served the symlink target with
HTTP 200. The native `os.symlink` operation itself worked, but Python 3.14's
unchanged `ntpath.realpath()` could not import its native trio of helpers
because `nt._findfirstfile` was absent, and fell back to a path-only resolver.
XLang3 now provides the Win32 `FindFirstFileW` primitive and propagates real
Windows error numbers from `nt._getfinalpathname` so non-strict path
resolution can handle missing paths. The registered `symlink_realpath.py`
CPython oracle, public FastAPI symlink-containment route, and exact unchanged
upstream FastAPI case pass on the rebuilt Release binary. The full Release
build, registered fixtures including both new oracles, and CTest **53/53**
pass. The complete local FastAPI gate also passes, including the new public
signal and symlink routes and live Uvicorn HTTP/HTTPS. Full upstream reruns
are in progress. The overall production-stack goal is still open.

The unchanged Pydantic 2.13.5 suite currently cannot collect: its upstream
`tests/conftest.py` imports `jsonschema`, which was missing from the isolated
test environment. An isolated test-dependency directory now has
`jsonschema==4.26.0`, `referencing==0.37.0`, and their declared dependencies;
the matrix runner accepts an explicit additional package directory so this
does not alter other live suite environments. Collection then reaches
`rpds.rpds`, the native `rpds-py==2026.6.3` dependency. Its downloaded
CPython extension cannot load in XLang3, and no fallback or CPython ABI
dependency is being used. This is an open genuine native dependency boundary
for the full Pydantic test suite, not a Pydantic behavior pass or skip.
With its remaining upstream test-only packages installed in the same
isolated directory and the collection-only pytest deprecation filter,
CPython 3.14 collects **6,091 Pydantic tests**. XLang3 collection is still
blocked at `rpds.rpds`; no Pydantic test result is inferred from the CPython
collection count.

The first complete untouched pydantic-core 2.46.5 run collected **5,934**
cases and stopped after **28 passed** at
`tests/benchmarks/test_micro_benchmarks.py::test_definition_model_core`.
The exact case passes on CPython 3.14. Its input nests 97 distinct model
instances, matching the native pydantic-core's published Windows definition
recursion limit of 99. XLang3's older arbitrary 12-reference cap rejected
valid data. Raising only the limit exposed a Windows stack overflow because
one large C++ `validate_value` function retained a roughly 27 KB frame on
each schema walk. The general native validator now has eight smaller schema
branch groups, and the definition-reference guard matches the upstream limit.
An isolated Release build with the normal 8 MB stack and `/Ob3` optimizer
validates 12, 48, and 97 levels; the exact untouched benchmark test passes.
The new public FastAPI route
`tests/fastapi/pydantic_recursive_definition_contract.py` sends those three
depths over HTTP and matches the CPython 3.14 output. The full pydantic-core
rerun on the isolated build is in progress. These checks do not yet prove the
whole suite or the final normal Release gate.

That full pydantic-core rerun reached **3,986 passed, 126 skipped, 1 xfailed**
before the first `test_model_class_root_validator_wrap` failure. The native
error payload from its root validator matches CPython 3.14 in a direct oracle.
The unchanged test expects pytest's assertion-rewritten `AssertionError`
detail, but both XLang3 and CPython 3.14 fail the exact test when launched
with the matrix's `--assert=plain` option. The two adjacent root-validator
tests have the same known setup dependency. This is a remaining pytest AST
assertion-rewriting compatibility issue, not evidence for changing the
native pydantic-core error. A diagnostic rerun deselecting only those three
tests is in progress to expose any later runtime failures; it is not a full
matrix pass.

The diagnostic pydantic-core run completed **5,791 passed, 130 skipped,
10 xfailed, 3 deselected** in 457.97 seconds. This matches the corresponding
CPython 3.14 plain-assertion selection, but the three deselections remain a
real full-suite gap until XLang3 can run pytest's assertion rewriting.
The full Starlette rerun advanced through responses and into routing, then
timed out while an AnyIO portal thread joined and iterated Python 3.14's
unchanged `_weakrefset.WeakSet`. CPython 3.14 passes the exact adjacent
responses/routing selection **220 passed, 2 skipped**, and XLang3 also passes
that selection in isolation, so the full-suite failure depends on accumulated
runtime state. A general standard-library probe shows `asyncio.run()` leaves
3 live entries in `asyncio.tasks._scheduled_tasks` per run on XLang3, even
after `gc.collect()`; CPython leaves zero. A public TestClient probe leaves
9 entries per completed portal. This identifies a task lifetime or cycle-GC
defect that must be fixed generally; no Starlette or FastAPI behavior has been
special-cased. The full FastAPI rerun continues separately.

The weak-task accumulation was traced to a general set hash contract.
`weakref.WeakSet.add()` calls built-in `set.add()` with a weak reference.
XLang3's set insertion used an identity-only internal hash and did not call
the instance's Python `__hash__`, so the weak reference had no cached hash.
When its referent died, `WeakSet._remove()` called `set.discard()`, which
correctly invoked `weakref.__hash__` but then raised `TypeError: weak object
has gone away`; the runtime silently swallowed the weakref callback error.
Native set insertion and iterable updates now invoke the Python hash method
before internal storage. A CPython 3.14 oracle and public FastAPI route agree
on custom `__hash__`, dead weak-reference hashing, and WeakSet removal after
`gc.collect()`. A standalone `asyncio.run()` probe now leaves **zero** dead
entries after collection, matching CPython, whereas it previously left three
per run. The full registered fixtures pass on the rebuilt RelWithDebInfo
binary; its expanded FastAPI gate and a fresh full Starlette rerun are active.
The prior full FastAPI run remains on the older Release binary. This fix does
not yet establish immediate weakref callback timing or eliminate every live
task left by a TestClient portal.

A further CPython 3.14 set oracle found that set literals skipped custom
`__hash__`, and the iterable `set(...)` constructor wrapped an exception
raised by `__hash__` as a new `TypeError`. The runtime-aware set insertion
path now covers set methods and VM literal/comprehension insertion; the
constructor preserves the original Python exception. An isolated Release
build matches CPython for custom hash calls from `set.add`, `set(iterable)`,
and literals, and for the original `ValueError` from all four insertion
forms. The registered `set_weakref_hash.py` fixture includes these cases.
Normal Release has not been rebuilt with this later change while the older
full FastAPI process is still using its runtime DLL.
The complete local FastAPI gate passes on the RelWithDebInfo build immediately
before the set-literal/constructor exception follow-up, including the new
weakref route and live Uvicorn HTTP/HTTPS. The latest isolated Release binary
passes the expanded set CPython-oracle fixture; the full local gates and CTest
must be repeated on the final normal Release build.

The runtime also queued weak-reference callbacks until explicit `gc.collect()`.
That leaves three dead `asyncio.tasks._scheduled_tasks` entries per ordinary
`asyncio.run()` call even after the set hash fix. An isolated Release build
now holds queued weak references strongly and drains callbacks at the VM's
ordinary execution boundary. A no-`gc.collect()` asyncio probe leaves zero
dead entries after each of five runs, matching CPython 3.14. A direct
weak-reference callback probe now observes callback order and immediate
WeakSet removal before its caller resumes, matching CPython. The registered
CPython oracle and public FastAPI route now assert cleanup without forcing
collection, and both pass on the isolated Release build. The full Starlette
rerun already in progress uses the earlier RelWithDebInfo binary and cannot
prove this later safe-point change; the final normal Release gates still
need to run after all active older-binary suites finish.

A 50-cycle no-GC `FastAPI`/`TestClient` portal stress probe on the latest
isolated Release build exposes a separate task-lifetime gap: after each
closed client, `asyncio.tasks._scheduled_tasks` gains one **live**, completed
task (50 after 50 cycles), whereas CPython 3.14 has zero after every tenth
cycle. Explicit `gc.collect()` reports zero collected objects and does not
remove the XLang entries. Simple `asyncio.run()` leaves zero on both
runtimes, so the retention appears specific to portal/thread teardown or a
native reference held along that path. This was subsequently traced to the
native thread state; the direct weakref callback and public route checks did
not cover it.
The expanded local FastAPI gate on that isolated Release build passes
completely, including its live Uvicorn HTTP/HTTPS and TestClient cases.
The untouched Starlette rerun reached 50% on the earlier RelWithDebInfo
binary; the untouched FastAPI rerun reached 15% on the older Release binary.
Neither percentage measures overall goal completion.

The TestClient task retention was narrowed to AnyIO's ordinary worker-thread
path: 50 portal cycles without a worker left zero tasks on both runtimes,
but 50 cycles with `anyio.to_thread.run_sync()` left 50 live completed root
tasks and 100 finished `threading._dangling` entries on the earlier isolated
XLang build. The native `_thread` state retained its completed bound
`Thread._bootstrap` target and its arguments in a global registry. Clearing
those Python references after the target returns makes both the 50-cycle AnyIO
worker probe and the 50-cycle synchronous FastAPI TestClient probe leave zero
scheduled tasks and only MainThread in `threading._dangling`, matching
CPython 3.14. The registered pure-stdlib thread-lifetime oracle and public
12-request synchronous FastAPI route also match CPython. The exact untouched
Starlette `tests/test_testclient.py` selection passes 52/52 on the new
isolated Release build. The complete expanded local FastAPI gate is running
again on this build; final normal Release/CTest and full upstream reruns
remain outstanding.
The native registry still retains completed state records until runtime
shutdown; a separate reaper experiment was removed because its broader
pytest behavior was not established. Sustained resource use remains to be
measured in the production load/soak gate.

The first full untouched FastAPI 0.141.1 run stopped at
`tests/test_multi_body_errors.py::test_jsonable_encoder_requiring_error`
after **563 passed, 17 skipped**. It returned an invalid nested Pydantic
location `['body', 'age']` where CPython 3.14 returned
`['body', 0, 'age']`. The native prebuilt model validator was invoking its
public `validate_python` method, which reset the incoming list-item location.
It now validates through the same prebuilt schema state while passing the
incoming location. A two-invalid-item Pydantic/FastAPI oracle matches CPython,
the exact unchanged upstream test passes on the rebuilt normal Release, and
the complete unchanged `tests/test_multi_body_errors.py` file passes **4/4**.
The normal Release fixture runner passes, and CTest passes **53/53** after
this change. The complete local FastAPI gate and a fresh full untouched
FastAPI rerun are active. The earlier isolated test build hangs before pytest
setup for this selected FastAPI case even when these recent source changes
are individually reverted, so its pytest result is not being used as proof.

The next untouched FastAPI run stopped after **1869 passed, 17 skipped** at
`tests/test_serialize_response_model.py::test_validlist_exclude_unset`. Nested
prebuilt Pydantic serializers dropped `exclude_unset` and related public
serialization options. The native serializer now forwards those options to
the nested schema state. A CPython-oracle FastAPI route covers nested response
models with unset/default fields, and the untouched upstream test file passes
**8/8** on the rebuilt normal Release. A new full untouched FastAPI run passed
through the prior failure and reached at least 56%; it is still running.
The complete local FastAPI gate, including real HTTP/HTTPS and a new Uvicorn
WebSocket network echo with a 60 KB frame, passes. The full CP-oracle fixture
runner and CTest **53/53** pass on the same normal Release build.

The demo remains available on port 8765. A 16-worker, 120-second real HTTP
soak completed 9297 requests without HTTP errors, but XLang3 private memory
grew from about 262 MB to 1.28 GB. A CPython 3.14 run of the same demo grew
only from about 41 MB to 48 MB over 1000 requests. The issue reproduces in
raw Uvicorn ASGI without FastAPI or Pydantic, and is strongly correlated with
new TCP connections; 1000 keep-alive requests on one connection added only
about 4 MB. A weakref diagnostic showed closed protocol/transport objects
remain in cycles that explicit `gc.collect()` can reclaim when they are
registered as weakref candidates. An isolated all-instance GC candidate
experiment reclaimed 20 objects after 10 connections but then stalled server
processing; it was reverted before normal Release and is not a verified fix.
The production memory issue remains open. A full untouched Starlette 1.6.0
run on an older RelWithDebInfo build reached 71% then timed out in a worker
thread's `_weakrefset` iteration during routing tests. That build predates the
latest weakref safe-point and native thread-target fixes, so rerun on the
updated Release is required. Neither 50% nor 69% is an evidence-based
completion percentage; use verified gates and unresolved blockers instead.

The next normal Release untouched FastAPI run stopped at **1992 passed,
17 skipped**, during `test_tutorial002.py` setup. `httpx.Headers.clear()` calls
`iter(self.keys())`; the `dict_keyiterator` returned by a Python `__iter__`
method was rejected as a non-iterator by the runtime's `sequence_get_iter`.
That general iterator recognizer now accepts existing dict and set iterators
as themselves. The registered pure-stdlib CPython 3.14 fixture tests iterator
identity and Python `__iter__` delegation, and a public FastAPI TestClient
route clears the default HTTPX headers; both pass on an isolated Release
build. Normal Release has been rebuilt; full untouched FastAPI and Starlette
reruns and the expanded local gate are active. The normal Release CTest pass
was **52/53** while several suites ran concurrently: the native network test
failed on one large HTTP request, then passed **1/1** when rerun alone. A
clean full CTest rerun is still required. The demo was restarted on this
normal Release at port 8765 and its `/api/runtime` reports XLang3 3.14.7.

The expanded local FastAPI gate is green on this normal Release, including
the new header-clear case, HTTP/HTTPS, and WebSocket. The current untouched
Starlette 1.6.0 run passed its former 71% worker timeout, then stopped after
**888 passed, 2 skipped, 2 expected failures, and 1 platform deselection**
at `test_staticfiles_with_invalid_dir_permissions_returns_401`. The exact
unchanged upstream test fails under CPython 3.14 on this Windows host for
both asyncio and trio: it expects HTTP 401 after `chmod`, but the request
returns 200. This is now recorded as a narrowly scoped Windows platform
deselection, and a full Starlette rerun with both CPython-justified
deselections is active. The full FastAPI rerun is also active.

That full FastAPI rerun subsequently passed the previous
`test_additional_responses/test_tutorial002.py` failure: all three unchanged
tests in the file passed, and the suite reached at least 61%. This is stronger
evidence than the isolated direct-file pytest attempt, which failed during
collection in a separate AST traceback path despite the same public route
passing. The complete suite remains active.

The untouched FastAPI run then stopped at **2062 passed, 17 skipped** in
`test_tutorial/test_body/test_tutorial001.py::test_post_form_for_json`.
XLang3's Pydantic error for a URL-encoded form string was `model_type` while
the unchanged upstream expectation and a pinned CPython 3.14 +
`pydantic-core` 2.46.5 run gave `model_attributes_type`. The nested prebuilt
validator path copied its schema/config but dropped the caller's validation
environment, including `from_attributes=True`. It now copies that environment
before selecting the prebuilt model config. The registered Pydantic + public
FastAPI form-route oracle matches CPython on an isolated Release build.
Normal Release and the full upstream FastAPI suite still need rebuilding and
rerunning with this latest native package change.

The full untouched Starlette 1.6.0 rerun is green on normal Release:
**1056 passed, 4 upstream skips, 2 expected failures, 3 deselected**. The
three deselected cases come from the two CPython 3.14 verified Windows test
rules. This completed run includes the previously stuck routing region and
the untouched TestClient and WebSocket tests.

The nested-validator environment fix is now built into normal Release. The
unchanged FastAPI `test_post_form_for_json` passes **1/1** when selected from
the full 3335-item suite collection, and the registered Pydantic/FastAPI
oracle matches pinned CPython 3.14. Full normal Release CTest passes
**53/53** on a clean rerun, including the earlier transient native network
test. The expanded local FastAPI gate and the next full untouched FastAPI
rerun are active. The demo is running again at port 8765 on the updated
native package.

The updated full local FastAPI gate passes on normal Release, including the
new form-body regression and real Uvicorn HTTP/HTTPS/WebSocket checks. The
next full untouched FastAPI rerun is active. The unmodified AnyIO 4.15.1
suite cannot yet collect: `tests/conftest.py` imports `trustme`, which imports
the native `cryptography` package unavailable on XLang3. This is an explicit
test-dependency/native-support gap, not a test pass or a justified platform
skip. The untouched `pydantic-core` 2.46.5 suite is running separately.

That pydantic-core suite stopped after **3986 passed, 126 skipped, 1 expected
failure** at `test_model_class_root_validator_wrap`. The test expects pytest's
rewritten assertion expression inside the Pydantic error. Both XLang3 and
pinned CPython 3.14 produce `AssertionError()` without that expression when
the matrix uses `--assert=plain`; the exact CPython upstream selection also
fails under this option. XLang3 currently errors before test execution when
pytest's default assertion rewriting is enabled. The harness/runtime AST
rewrite gap remains unresolved; this is not a Pydantic native validation
failure and is not counted as a passing full suite.

The latest untouched FastAPI run stopped after **2463 passed, 17 skipped**
in the Strawberry GraphQL tutorial. Its validation rules tuple contained
the `graphql.validation.specified_rules` *module* instead of the exported
tuple, because XLang3 rebound an already cached child module onto its
parent package whenever it was imported again. A minimal CPython 3.14
fixture proves cached child imports preserve a parent's explicitly exported
value. The runtime cache path now binds the child only when that parent
attribute is absent. The minimal fixture and a public FastAPI/Strawberry
GraphQL route match CPython in an isolated Release build. Normal Release
and upstream reruns are pending. The untouched Uvicorn 0.53.0 suite cannot
finish collection because optional native `httptools` is unavailable on
XLang3; its real h11/WS network paths pass the local integration gate, but
the full upstream suite remains unverified.

The cached-child import fix is built into normal Release. The exact
unchanged FastAPI GraphQL query test passes **1/1** when selected from the
full 3335-item suite collection; the registered CPython-oracle fixture and
public FastAPI/Strawberry route pass. A fresh normal Release CTest is green
**53/53**. The expanded local FastAPI gate and the next complete untouched
FastAPI run are active, while the demo again reports XLang3 3.14.7 on port
8765.

The untouched HTTPX 0.28.1 suite also cannot collect because its `trustme`
test dependency imports native `cryptography`. The untouched Pydantic 2.13.5
suite first lacked pure-Python `jsonschema` in the default matrix dependency
path; with the existing `scratch/pydantic-test-deps` supplied, importing
`jsonschema` reaches native `rpds.rpds`, which XLang3 does not provide. This
is a separate native package boundary; the CPython `.pyd` cannot be used by
XLang3. Neither suite is counted as passing. Uvicorn's collection blocker is
the optional native `httptools` dependency noted above.

After scoping `scratch/fastapi-test-deps` to the GraphQL integration case,
the full expanded local FastAPI gate is green again on normal Release,
including the thread-target lifetime regression, GraphQL query, HTTP/HTTPS,
and WebSocket. A first run with test-only packages prepended for *every*
case had retained one task in that lifetime check, so the broad path change
was removed; the scoped rerun passed. The complete untouched FastAPI rerun
is still active and has passed the earlier form-body failure again.

The full untouched FastAPI 0.141.1 test collection has now completed on
normal Release with `--tb=no`: **3324 passed, 17 skipped, 4 xfailed**, exit
code 0, in 812.16 seconds. The test files were unchanged; only pytest's
traceback display was disabled after an earlier run crashed inside Python
3.14 `ast.iter_child_nodes` while formatting a result at 2797 passes. Do
not present the suite's progress percentage as overall compatibility.

That AST failure was reduced to `ast.NodeVisitor().visit(ast.parse('1+2j'))`
and a formatted-string assertion. The native `_ast` conversion was producing
invalid child nodes for complex literals and f-strings; an isolated Release
build now constructs those nodes and passes a CPython 3.14 oracle fixture
`ast_recursive_visitor`. The generic `compile(AST)` conversion also learned
big integers and complex constants; previously blocked pydantic-core
`tests/serializers/test_complex.py` now collects with normal pytest assertion
rewriting, but incorrectly as only 2 tests: XLang3's AST conversion drops
the `@pytest.mark.parametrize` decorator, so the parameterized test fails
at setup with `fixture 'value' not found`. This is **not** a pass. The isolated
build is not yet copied to normal Release.
The production memory growth and untouched-suite native dependency blockers
remain open.

The AST fix is built into normal Release and CTest is green **53/53**.
It does not make normal pytest assertion rewriting generally safe: parsing
the unchanged pydantic-core `tests/validators/test_model.py` gives an empty
`ast.Module` on XLang3, whereas CPython 3.14 gives 47 top-level nodes.
Pytest consequently reports no tests collected from that file under normal
rewriting. This is a serious native `_ast` parser/converter gap and must not
be counted as a test pass. The local FastAPI gate is being rerun on the new
normal Release build.

The expanded local FastAPI gate completed successfully on that build,
including Pydantic contracts, GraphQL, HTTP/HTTPS, WebSocket, and Uvicorn
network tests. The demo restarted on port 8765 and `/api/runtime` reports
`implementation=xlang3`, Python 3.14.7. The full untouched FastAPI suite
result above is from the preceding normal Release build; the recent AST
conversion changes were validated by the local gate and 53/53 CTests, not
yet by a second full upstream FastAPI run.

The later isolated Release work extends the native `_ast` conversion and
`compile(AST)` across classes, decorators, assignment targets, scope
statements, context managers, exceptions, loops, generators, lambdas,
comprehensions, slices, and constants. A general IR-codec BigInt constant
fix lets pytest marshal rewritten test modules. The full registered CPython
3.14 oracle fixture runner passes on this isolated build. With normal pytest
assertion rewriting, unchanged pydantic-core 2.46.5 files pass as follows:
`tests/validators/test_model.py` **44 passed, 1 skipped** (the same count as
CPython 3.14); `tests/serializers/test_complex.py` **13 passed**; and
`tests/serializers/test_any.py` **99 passed, 2 skipped** (also matching
CPython 3.14). Fresh full pydantic-core collection is still not green:
collection errors fell from 55 to 17 before the latest set/dict comprehension
fix. This work remains isolated from the normal Release binary. The full
FastAPI suite must be repeated after integration. Native dependency blockers
and production memory growth remain open. No evidence-based overall
completion percentage has been established; previously stated 50% and 69%
estimates are both retracted.

A subsequent isolated Release update repairs native AST `Starred` nodes in
literal and assignment contexts, plus all-zero decimal integer literals.
CPython 3.14 oracle fixtures for both pass. The starred conversion removes
invalid AST children from unchanged pydantic-core error, dataclass, decimal,
float, and list test modules; the all-zero fix removes the remaining invalid
child in `tests/validators/test_uuid.py`. The UUID module then collects all
**97** cases under normal assertion rewriting, but initially ran only
**81 passed, 16 failed**. A failure-reporting diagnostic found that the
rewritten `PyAndJsonValidator.__init__` had lost its `config=None` default.
The general `compile(AST)` FunctionDef converter now preserves positional
and keyword-only defaults, parameter annotations, return annotations, and
the proper `*args`/keyword-only/`**kwargs` order. Its CPython 3.14 oracle
passes, the complete registered fixture runner passes on the isolated build,
and unchanged `tests/validators/test_uuid.py` now passes **97/97**, matching
the CPython 3.14 oracle. A fresh full pydantic-core collection on this build
is in progress; the normal Release, full FastAPI suite, native package matrix,
and memory problem remain open.

With the same pinned pytest plugin set explicitly loaded (benchmark,
run-parallel, timeout, Hypothesis, and pytest-mock), untouched pydantic-core
2.46.5 collection now reaches **5,934 tests, zero collection errors** on the
isolated Release build. The matching CPython 3.14 environment also collects
**5,934** tests. This is collection parity only, not execution parity. To
reach it, the native AST bridge also gained general `Break`, `Continue`,
`NamedExpr` (`:=`), and `Await` conversions; their CPython 3.14 oracle
fixtures and the complete registered fixture runner pass. The pinned
`pytest-run-parallel` and `pytest-mock` plugin files themselves now parse,
compile, and marshal under XLang3. The suite must still be executed, and
the normal Release/FastAPI gates repeated after integrating this build.

Full pydantic-core execution with the pinned plugins and the same explicit
pytest-9 deprecation-warning filter as the CPython 3.14 oracle first reached
**1,191 passed, 9 skipped, 1 xfailed**, then failed the unchanged Hypothesis
`test_recursive`. CPython's complete baseline with that filter is **5,794
passed, 130 skipped, 10 xfailed, 26 subtests passed**. The initial XLang3
failure was not a native validator discrepancy: Hypothesis had cached an empty
TypedDict strategy for a class whose key was required. A general runtime
`exec(code, globals_dict)` defect copied the namespace and synchronized it
only after the module body finished, so forward references could not see the
class through `sys.modules[...].__dict__` during execution. The isolated build
now executes directly in module-backed dictionaries and live-binds ordinary
globals dictionaries. A CPython 3.14 oracle covers live namespace identity,
module visibility, and writes before an exception; the fixture runner passes.
The previously failing unchanged `test_recursive` now passes **1/1** with the
full plugin set. A separate general class `__name__` assignment fix was also
verified against a CPython oracle; it corrects TypedDict type introspection.
The subsequent full untouched pydantic-core run collected all **5,934** and
reached **5,753 passed, 130 skipped, 8 upstream xfailed, 26 subtests passed,
1 failed** before `-x` stopped it at 99% (410.07 seconds). The only unexpected
failure was `tests/validators/test_with_default.py::test_leak_with_default`:
its weak reference to a class remained live after its ten-second GC retry.
That unchanged test passes **1/1** in isolation; its full test file passes
**130 passed, 2 upstream xfailed**, and a standalone schema/class cycle probe
is collected on the first `gc.collect()`. The failure therefore depends on
the broader run or its memory/GC workload; it is not yet classified as a
native pydantic-core leak versus a runtime collection limit. The remaining
cases after the stop have not been executed in this full run. The normal
Release, FastAPI rerun, other pinned upstream suites, and memory-growth
analysis remain open.

The full-suite `test_leak_with_default` failure reproduced with the same
counts. A direct diagnostic timing hook measured its first `gc.collect()` at
**12.730 seconds**, returning **10 collected objects**. The upstream helper's
deadline is ten seconds and it checks the predicate only before collection;
it therefore times out without checking again after that long collection.
The standalone equivalent also collected ten objects and cleared the class
weak reference. The general XLang3 weakref cycle collector contained two
nested scans of every registered weak reference across candidate instance
attributes. Those scans now use indexed weakref and owner lookups. The
isolated Release build and full registered fixture runner pass after this
change. The next full untouched pydantic-core 2.46.5 run completed with
**5,794 passed, 130 skipped, 10 upstream xfailed, 26 subtests passed** in
338.96 seconds, matching the CPython 3.14 result counts. The same first
`gc.collect()` at the leak test took **0.150 seconds** and collected ten
objects. This proves full pydantic-core execution parity for this pinned
Windows matrix and flags; normal Release integration, the other project
suites, load/soak, and memory-growth work remain open.

Normal Release integration after the GC fix builds cleanly, passes **53/53**
CTests, and passes the expanded local FastAPI gate including live Uvicorn
HTTP, HTTPS, and WebSocket. The demo was restarted on the new Release at port
8765 and `/api/runtime` reports XLang3. A full untouched FastAPI 0.141.1
matrix run collected **3,335 cases / 10 collection skips** but the process
terminated near 4% with Windows heap-corruption code `0xC0000374`; this is a
runtime crash, not a test failure. The first 32 test files together passed
**157 passed, 3 skipped**, and full collection selecting only
`test_dependency_class.py` passed **12** with 3,323 deselected, so the crash
needs cumulative-execution or nondeterministic diagnosis. A verbose full
rerun is in progress to identify the last exact test if it recurs.

The normal pytest assertion-rewriting startup independently exposed a general
native importlib bridge gap: `importlib.util.spec_from_file_location()`
discarded its `submodule_search_locations` keyword, so pytest's rewritten
`inline_snapshot` package had no `__path__` and failed to import its pure
Python child package. The isolated build now preserves explicit package
search locations (including an empty list's directory population and an
explicit `None`) and infers `__init__.py` package location without the
keyword. The minimal CPython 3.14 fixture and full registered fixture runner
pass. The normal Release has since been rebuilt, but the full pytest rewrite matrix has not yet been run
with that importlib change.

A second full untouched FastAPI 0.141.1 execution on the normal Release,
using the pinned matrix and `--assert=plain`, completed with **3,324 passed,
17 skipped, 4 xfailed**, exit 0. The preceding Windows heap-corruption exit
remains intermittent and unresolved; one successful rerun does not clear it.

On the isolated Release, normal pytest assertion rewriting now collects the
selected unchanged FastAPI `test_dependency_class.py` file (**12 cases**).
Enabling this path exposed Python 3.14 structural-match AST conversions absent
from the native AST bridge. It now converts `Match`, `match_case`, and the
value, singleton, sequence, mapping, class, star, as, and or pattern nodes in
both parse and compile directions. General source-level pattern matching also
had defects: failed cases lost the subject, sequence patterns accepted
mappings and raised on non-sequences, mapping patterns used containment and
indexing instead of `get(key, sentinel)`, and `**rest` dereferenced a null
mapping key. These paths now have CPython 3.14 oracle fixtures
`ast_match_patterns` and `match_protocol_semantics`. The complete fixture
runner passes on the isolated Release, including the two new fixtures.
A registered `Mapping` without `get()` now raises `TypeError` as on CPython;
registered `Sequence` and `Mapping` implementations with the required
protocol methods match correctly.

The selected unchanged FastAPI `test_dependency_class` case also passed on
the current normal Release (**1/1**). Several isolated-build attempts seemed
to stall after collection, but this was a command-runner observation artifact:
Windows reported every test-process thread suspended with suspend count one.
After resuming those disposable test threads, the case passed, including a
run with pytest plugin autoload disabled. The isolated and normal Release
runtime and executable `.text` sections are identical after integration.
This does not establish full assertion-rewriting execution parity; the full
suite and intermittent heap-corruption issue remain open. Longer validation
runs must be launched detached with output logs to avoid tool-induced process
suspension between session polls. The normal Release demo was restarted on
port 8765 after the rebuild and `/api/runtime` responded successfully.
After the normal Release rebuild with the AST/match changes, CTest passed
**53/53**. An independently launched, logged run of untouched FastAPI
`tests/test_dependency_class.py` with normal pytest assertion rewriting
collected **12** and stopped on its first case (`--maxfail=1`): AnyIO
`CancelScope.__exit__` raised `AttributeError` for `_cancel_called`. Repeating
without a custom pycache prefix failed the same way. The same case passes
with `--assert=plain`; the normal rewrite path is therefore still a real
compatibility gap. A standalone pytest test can instantiate `CancelScope`
correctly, and directly compiling/executing the pytest-rewritten FastAPI
module also handles the request, so the failure requires the full test
execution path. The prior apparent process stall was unrelated to this
failure and is resolved as a command-runner suspension artifact.

Further reduction corrected the assertion-rewriting diagnosis. Direct
`ast.parse` -> `compile` -> `exec` of the unmodified AnyIO asyncio backend
reproduced the missing `CancelScope._cancel_called` attribute. A small
CPython-oracle fixture, `ast_while_live_local`, isolated the cause: the native
lowerer's local-load fusion moved a `while` condition's load before the loop
backedge when the AST had sparse source locations. The `WhileStmt` lowering
now marks its header as a control-flow entry, preserving a fresh condition
load on each iteration. The AST bridge also represents a single comparison
with the same `CompareChainExpr` form as source parsing. The complete isolated
fixture runner passed after these changes. Direct AST loading of the unchanged
AnyIO backend now handles the FastAPI request successfully. With a **fresh**
`PYTHONPYCACHEPREFIX`, unchanged upstream FastAPI
`tests/test_dependency_class.py` passed **12/12** with normal pytest
assertion rewriting on the isolated Release. Earlier failures persisted with
stale rewritten bytecode in an old cache prefix; the upstream matrix runner
now uses a fresh prefix and offers `-RewriteAssertions` for a full normal
rewrite run. The full rewrite-enabled upstream suite was then measured below.

The normal Release local FastAPI gate then completed successfully, including
the ASGI, TestClient, Uvicorn HTTP, and Uvicorn WebSocket end-to-end cases.
CTest on the same Release passed **53/53**, including the complete registered
fixture runner. A fresh full upstream FastAPI run with normal pytest assertion
rewriting produced the first-pass result below.

That full normal-rewriting FastAPI run completed its first pass with
**3,299 passed, 17 skipped, 4 xfailed, 1 failed** (exit 1). The failure was
`test_validation_error_context.py::test_request_validation_error_includes_endpoint_context`:
the validation message contained only the route, not endpoint `get_user`.
The unchanged FastAPI implementation uses `inspect.getsourcelines()` and
requires a truthy source line before formatting the function name. On the
pytest-rewritten endpoint, XLang3 reported `co_firstlineno == 0` despite the
source AST's valid line number. The native AST-to-internal-statement bridge
discarded statement location fields. It now copies line and column start/end
locations to every emitted statement, including expanded assignments. The
new `compile_ast_source_locations` fixture compares nested function first
lines against CPython 3.14. The rebuilt Release produces the CPython oracle
outputs (`2 2`, `3 3`), and all **7/7** unchanged upstream
`test_validation_error_context.py` cases pass with normal assertion rewriting.
A fresh full-suite rerun completed with **3,324 passed, 17 skipped,
4 xfailed**, exit 0, using normal pytest assertion rewriting on the
unchanged FastAPI 0.141.1 checkout. CTest immediately after the rebuild
passed **52/53**; its native net server/client case failed once under the
concurrent upstream run, then passed **1/1** in an isolated rerun. That
intermittent CTest result was followed by a clean, nonconcurrent full gate:
**53/53 passed**. The rebuilt Release also passed the complete local FastAPI
gate, including real Uvicorn HTTP and WebSocket end-to-end checks. The demo
was restarted on port 8765; `/api/runtime` returns XLang3 3.14.7 and
`status: ready`. The seven-project upstream matrix and long-running stability
checks remain open, so the overall goal is not yet complete.

2026-09-24 Starlette matrix continuation: unmodified pinned Starlette 1.6.0
passed its complete selected suite with normal pytest assertion rewriting:
**1,056 passed, 4 upstream skips, 3 documented Windows deselections,
2 xfailed** (exit 0, 189.52 seconds). The deselections are the pre-recorded
CPython 3.14 Windows failures in `test_config.py` and `test_staticfiles.py`.
The matrix result is in `scratch/upstream-results-starlette-rewrite-20260924`.

The first complete Pydantic 2.13.5 matrix attempt stopped during untouched
`tests/conftest.py` import because its `jsonschema` test dependency was absent
from the default matrix path. An isolated `scratch/pydantic-test-deps` target
contains `jsonschema` 4.26.0. With that target on `PYTHONPATH`, unmodified
`jsonschema` reaches `referencing`, which imports the `rpds` CPython binary
extension. XLang3 correctly refuses the `.pyd`. The Pydantic suite therefore
needs a true XLang3 native `rpds` implementation in `modules` before its
unmodified tests can be measured; this is an open native dependency boundary,
not a test skip or permission to load the CPython binary.

The full unmodified pinned pydantic-core 2.46.5 suite passed on the current
Release with normal pytest assertion rewriting: **5,794 passed, 130 upstream
skips, 10 xfailed**, exit 0 (5,934 collected, 439.60 seconds). The matrix
result is in `scratch/upstream-results-pydantic-core-rewrite-20260924`.

Independent full-matrix attempts expose test-environment/native boundaries
before assertions: untouched AnyIO 4.15.1 and HTTPX 0.28.1 `conftest.py`
import `trustme`, which requires `cryptography`; it is not in the pinned test
target. Its binary extension still needs an XLang3-compatible native boundary
after a reproducible test install. Untouched
Uvicorn 0.53.0 collected 1,324 tests but failed collection of
`tests/test_server.py` because it imports optional native `httptools`
directly. These are **open** matrix projects, not passed suites or justified
platform skips. Results are in the corresponding
`scratch/upstream-results-{anyio,uvicorn,httpx}-rewrite-20260924` folders.

The current demo also completed real HTTP load/soak probes via
`tests/fastapi/load_demo_website.py`: **1,000 requests / 16 workers / zero
errors** in 8.813 seconds (p95 147.1 ms), followed by **6,280 requests /
16 workers / zero errors** over 60.152 seconds (p95 172.0 ms). These probe
`/api/runtime` only and therefore do not stand in for the broader feature,
malformed-input, and long-duration soak requirements.

2026-09-24 VM memory safety continuation: the first post-AST full FastAPI rerun exited with Windows heap corruption. AddressSanitizer reproduced a use-after-free in memoryview result tracking after a local assignment freed a bytearray. The VM now tracks only opcodes with register results and handles fused paired results explicitly. With the old tracking code, the untouched upstream `test_dependency_after_yield_websockets.py` pytest launch fails under ASAN at startup; with the corrected code and matching ABI-24 native modules, both tests pass under ASAN (exit 0, 2 passed). The added `vm_borrowed_local_overwrite` CPython-oracle fixture passes on both builds, so it is supplemental coverage rather than the sanitizer reproducer. The full Release build, CTest 53/53, and complete local FastAPI gate pass. The fresh full unmodified FastAPI 0.141.1 suite under normal assertion rewriting passed **3,324 passed, 17 skipped, 4 xfailed, exit 0** in `scratch/upstream-results-fastapi-vmtrack-20260924`. On that build, the demo completed **1,000 requests / 16 workers / zero errors** and a **60-second, 5,646-request / 16-worker soak / zero errors**. The demo remains running on `127.0.0.1:8765`; `/api/runtime` reports XLang3 3.14.7. The seven-project matrix remains **3/7 green** because Pydantic, AnyIO, Uvicorn, and HTTPX have open dependency/collection work.

2026-09-24 native `httptools` continuation: XLang3 now builds real native
`httptools.parser.parser` and `httptools.parser.url_parser` packages under
`modules/httptools`, using the pinned 0.8.0 upstream llhttp/http-parser C
sources and retaining the upstream pure Python package files unchanged in the
isolated test target. The 0.8.0 sdist SHA-256 was checked against PyPI before
vendoring. The untouched upstream parser test module passes **41/41** on
XLang3, matching the CPython 3.14 oracle run. This exposed and fixed native
callback exception propagation, upgrade offsets, bytes return values, input
type errors, and read-only URL attributes. The new URL oracle contract also
passes. The first full Uvicorn matrix run now collects **1,352** tests and
reaches 82% with no HTTP or WebSocket failures, but records three failures in
`tests/supervisors/test_multiprocess.py` before the test process ended without
a pytest final report. An isolated rerun confirms
`test_multiprocess_health_check` fails because its spawned XLang3 subprocess
exits with Windows status `0xC000013A` after a CTRL_BREAK termination path;
the same untouched case passes on CPython 3.14. XLang3's current signal module
stores Python handlers but does not yet install a Windows console control
handler, so this is a genuine general runtime signal boundary to investigate.
Uvicorn remains **open**, and the seven-project matrix remains **3/7 green**.

2026-09-24 Windows signal and flush continuation: XLang3 now installs a
Windows console-control handler when Python code registers SIGINT/SIGBREAK,
records events without running Python on the OS callback thread, writes to
the configured wakeup descriptor, and dispatches the Python handler on the
main VM thread. The unchanged Uvicorn multiprocessing supervisor module
passes **11 passed, 4 upstream Windows skips**, including the prior
`test_multiprocess_health_check` failure. A new process-level CPython 3.14
oracle `signal_console_control_contract` passed on both runtimes with output
`ready handled 0`. That test also exposed `print(..., flush=True)` silently
skipping flush for implicit `sys.stdout`; the builtin now flushes the actual
stream. The corrected Release executable builds. A full serial Uvicorn rerun
passed through HTTP, WebSocket, and the entire multiprocessing supervisor
module to **82% with no failures**, but the task handoff interrupted the test
process before pytest wrote a final report. A parallel rerun was invalid at
collection because upstream `pytest-benchmark` intentionally errors when
xdist is active. Uvicorn is still **open** pending a complete serial exit-0
run; no skip or green matrix credit is claimed from partial runs.

2026-09-24 Uvicorn server continuation: the remaining `test_server.py`
module revealed missing CPython `contextvars.Context` mapping methods.
XLang3 now supplies snapshot `keys`/`items`/`values` iterator objects with
length, iteration, and one-shot behavior, plus `Context.get` and membership.
The new `context_mapping_contract` matches CPython 3.14 output. The full
untouched Uvicorn `test_server.py` module then passed **11 passed, 4 upstream
zttp-not-installed skips** (exit 0). One request-limit jitter case failed in
an earlier module run, passed alone, and passed in the module rerun; this is
recorded as intermittent, not a fixed defect. The full Uvicorn matrix run
still needs an uninterrupted final result. The matrix remains **3/7 green**.

2026-09-24 isolated Windows upstream Uvicorn result: the matrix harness now
supports `-IsolateConsole`, which launches only the test interpreter in its
own hidden Windows console. This contains upstream worker CTRL_C/CTRL_BREAK
events while keeping the test source unmodified and the runtime XLang3. The
complete pinned Uvicorn 0.53.0 suite then exited **0: 1,000 passed, 352
upstream skips, 1,352 collected, 462.79 seconds** in
`scratch/upstream-results-uvicorn-isolated-20260924`. The skips include
Windows/Unix platform markers and optional `zttp`, `watchfiles`, and
`trustme` dependencies; optional HTTP/2 and SSL variants therefore remain
unverified, not silently declared compatible. The seven-project matrix has
**4/7 exit-zero projects** (FastAPI, Starlette, pydantic-core, Uvicorn),
while Pydantic, AnyIO, and HTTPX remain open. The overall FastAPI goal is
not complete, and current Release/CTest/local-gate results must be refreshed
after the Context mapping changes.

The refreshed full Release build passed, CTest passed **53/53**, and the
complete local FastAPI gate passed (including new console-signal and Context
oracles plus real Uvicorn HTTP/HTTPS and WebSocket end-to-end checks). These
are current-build results from `scratch/full-build-contextvars-20260924.log`,
`scratch/ctest-contextvars-20260924.log`, and
`scratch/fastapi-local-contextvars-20260924.log`.
The production-style demo is running again at `http://127.0.0.1:8765/`
(PID 25232 at verification); `/api/runtime` returned implementation
`xlang3`, Python version `3.14.7`, framework `FastAPI`, and `status: ready`.

2026-09-24 rpds dependency investigation: Pydantic upstream collection requires jsonschema/referencing, which import rpds HashTrieMap/HashTrieSet/List. The installed rpds-py 2026.6.3 wheel is CPython-only and remains forbidden for XLang3. Verified upstream sdist is Rust rpds/archery with PyO3 wrapper; a separate XLang3 native bridge must call Rust without PyO3/CPython. Installed official rustup stable 1.98.1 workspace-locally under scratch/rust-local (no PATH or system change) to build that bridge. No rpds implementation or upstream Pydantic suite pass is claimed yet.


2026-09-24 native rpds bridge progress: added modules/rpds/rust using upstream rpds=1.2.1 and archery=1.2.2, with an XLang3 C ABI over persistent HashTrieMap, HashTrieSet, and List. Rust unit tests pass 3/3, covering structural persistence, key collisions, snapshot lifetime, and exactly-once opaque-handle release. A locked Release static library builds. The new modules/rpds/rpds_module.cpp exports those three collection classes through the XLang3 package ABI without loading PyO3, a CPython wheel, or the CPython runtime. tests/fastapi/rpds_contract.py has identical output under CPython 3.14 with the upstream rpds-py wheel and under XLang3 using this native package. More rpds API surface, error behavior, and concurrency checks remain pending; this is not full rpds parity.

2026-09-24 native submodule and parser continuation: upstream rpds/__init__.py revealed that XLang3 failed to bind a newly loaded native child module to its parent package and omitted the native module __doc__ attribute. The general native-package loader now binds the child on its parent and initializes __doc__ to None; tests/fastapi/native_child_module_contract.py passes on CPython 3.14 and XLang3. Pydantic upstream then reached a pathspec source file with tab indentation. The general lexer now computes eight-column tab stops, tracks an alternate indentation count for mixed-tabs errors, and keeps byte offsets separate from indentation columns. tests/fixtures/core/tab_indentation.py passes on both runtimes, and untouched pathspec.gitignore imports on XLang3. The next Pydantic matrix run gets through rpds and pathspec but stops in platformdirs.windows, where XLang3's pre-existing _ctypes stub returns an integer for an LPWSTR out-pointer whose .value is a str on CPython. tests/fastapi/ctypes_known_folder_contract.py reproduces the mismatch on a real SHGetKnownFolderPath call. The existing _ctypes module is a fake implementation and requires a genuine native FFI boundary; no Pydantic suite pass is claimed. The matrix remains 4/7 green. The demo was stopped temporarily for the runtime relink and must be restarted after verification.
2026-09-24 validation after rpds/import/tab changes: full local FastAPI gate passed on the rebuilt Release runtime (scratch/fastapi-local-rpds-tabs-20260924.log), including real Uvicorn HTTP/HTTPS and WebSocket checks. CTest first run had 52/53 with an intermittent Visual Studio debugpy adapter weakref failure on the host-side adapter; that test passed alone on rerun, and the complete second CTest run passed 53/53 (scratch/ctest-rpds-tabs-rerun-20260924.log). The rpds and native-child CPython-oracle outputs match on XLang3. The production demo was restarted on XLang3 at http://127.0.0.1:8765/ (PID 15000); /api/runtime reports xlang3, Python 3.14.7, FastAPI, ready. The seven-suite matrix remains 4/7 exit-zero; Pydantic cannot collect beyond the genuine _ctypes FFI blocker, and AnyIO/HTTPX remain open. No commit or push.
2026-09-24 Pydantic collection and execution continuation: replaced the borrowed VM-frame closure pointer with frame-owned closure storage, fixing immediate calls of temporary closures returned by descriptors. Added CPython-oracle `temporary_closure_contract`. The untouched Pydantic suite then passed conftest import and collected progressively. The parser now accepts `**` keyword expansion in class definitions and passes it to metaclass `__prepare__` and construction (`class_keyword_unpack_contract`). `_typing.Generic.__init_subclass__` now delegates to Python 3.14's existing pure-Python `typing._generic_init_subclass`, making type parameters visible inside metaclass `__new__` (`generic_metaclass_contract`); no pure-Python typing implementation was copied into C++. Starred collection expression precedence now covers `[*left + right]` (`starred_expression_precedence`). Pytest 9's generator-parametrize deprecation is reproduced under CPython 3.14 on the pinned Pydantic test and narrowly filtered in the matrix harness; upstream files remain unchanged. Pydantic collection now reaches all 6,091 tests.

The first Pydantic execution crashed with Windows stack overflow in native pydantic-core model `validate_assignment`: it called the model's Python `__setattr__`, recursively re-entering validation. The native bridge now writes validated model fields through `object.__setattr__`, consistent with its existing construction path. `pydantic_assignment_contract` matches CPython and the unchanged upstream attribute-assignment benchmark passes. A postponed annotation containing a lambda exposed `<annotation>` placeholder text; the parser now retains annotation source spans for unsupported stringification forms, so Python 3.14 annotationlib sees valid source (`future_annotation_source_contract`). The unchanged upstream FastAPI-startup benchmark passes in isolation. Faker 35.0.0 was installed only into the isolated Pydantic test dependency directory from Pydantic's uv.lock. Its dynamic `__getattribute__`/`__getattr__` method fallback exposed a general fused CallMethod bug; VM attribute and method calls now use the existing `getattr` semantics for classes with custom `__getattribute__` (`getattribute_getattr_fallback`). A real Faker uuid4 call matches CPython. The unchanged north-star benchmark passed in isolation with `--benchmark-disable`, but required 177.65 seconds, close to its 180-second timeout; performance remains a risk.

The rebuilt Release runtime passes the complete local FastAPI gate including all new oracles and real Uvicorn HTTP/HTTPS/WebSocket checks (`scratch/fastapi-local-ctypes-closure-20260924.log`). CTest passes 53/53 (`scratch/ctest-ctypes-closure-20260924.log`). The XLang3 demo is running again on 127.0.0.1:8765; `/api/runtime` returned `implementation=xlang3`, `python_version=3.14.7`, `framework=FastAPI`, `status=ready` (server PID 2916 at verification). The last complete Pydantic matrix run failed after 44 passes and 108 skips because Faker was absent; the isolated next test passed after dependency installation and general fallback fix. The full Pydantic suite is still open; AnyIO and HTTPX are open. The seven-project matrix remains 4/7 exit-zero. No commit or push.

2026-09-24 full Pydantic rerun after Faker/fallback: all 6,091 tests collected and early benchmarks passed. `tests/benchmarks/test_north_star.py` completed its first benchmark, then the pytest 180-second timeout fired in Python 3.14 `uuid.py` during the next benchmark; matrix exit 1 after 490.925 seconds (`scratch/upstream-results-pydantic-faker-20260924/pydantic.log` and `matrix-results.json`). This is a real unresolved performance/timeout failure, not a green Pydantic result. The separately isolated first north-star test with `--benchmark-disable` passed in 177.65 seconds. Need investigate why UUID/Faker data generation is repeated or slow under pytest and compare unchanged CPython behavior; do not mask the timeout or count the suite as passed. The generated `north_star_data.json` is gitignored upstream test output; tracked checkout remains clean. Seven-project matrix still 4/7 exit-zero. Demo remained running during this matrix attempt.

2026-09-24 north-star performance diagnosis: the unchanged first two Pydantic north-star cases with `--benchmark-disable` share their module-scoped generated-data fixture (no regeneration); the first passed and the second timed out inside Python 3.14 `json.loads`. The identical two upstream tests pass on CPython 3.14 in 0.21 seconds. A separate 2 KB/20 KB/60 KB JSON parse microbenchmark took XLang3 0.018/0.955/7.83 seconds, showing near-quadratic growth. General string slicing was independently quadratic even for ASCII due to repeated UTF-8 offset scans; `src/runtime/sequence.cpp` now handles ASCII slices with direct byte offsets and resolves non-ASCII codepoint offsets once per slice. `string_slice_offsets` is a CPython-oracle fixture and passed, as did the full fixture log through its final case. String slice microbenchmarks are now flat (~0.001 seconds at 3,000 positions), but JSON timing remains 0.018/0.955/7.83 seconds: this fix alone does not clear Pydantic. Repeated `_json.scanstring` calls also scale poorly (0.002/0.034/0.236 seconds for 100/1,000/3,000 strings) because `_json.scanstring` currently delegates to the pure Python scanner, which uses `_sre.Pattern.match`; `_sre` copies the entire subject and walks public UTF-8 offsets per match. The next general fix should address the true native JSON/SRE boundary without changing FastAPI or Pydantic code. CTest 53/53 passed after this string change; the local FastAPI gate is still running at this writing. Matrix remains 4/7 green. No commit or push.
The complete local FastAPI gate completed successfully after the slice change, including real Uvicorn HTTP/HTTPS and WebSocket integration (`scratch/fastapi-local-string-slice-20260924.log`); CTest is 53/53 (`scratch/ctest-string-slice-20260924.log`). The demo was restarted on the rebuilt Release runtime and `/api/runtime` returned ready at `127.0.0.1:8765` (PID 22160). The slice optimization is verified, while Pydantic's JSON performance bottleneck remains unresolved and the upstream matrix remains 4/7 exit-zero.
The full fixture gate was independently rerun with the PowerShell success state and exited 0 (`scratch/fixtures-string-slice-confirm-20260924.log`); its new string-slice oracle passed.

2026-09-24 Pydantic execution continuation: fixed general ASCII string metadata and borrowed immutable ASCII subjects in native `_sre`, reducing 200 KB JSON parsing from 26.358 to 0.795 seconds and allowing the untouched north-star benchmarks to pass. Added native Rust `regex` for Pydantic Core's default `rust-regex` engine while preserving Python `re` mode. Corrected alias error locations and consumed input names, dynamic instance `__qualname__`, and nested generic alias representation. The full untouched aliases file passed 121 tests and the annotated file passed 36 tests. CPython-oracle and FastAPI route tests cover regex, aliases, and annotations. The Rust bridge and oracles do not use the CPython runtime or binary wheels.

2026-09-24 further Pydantic execution: abstract-method detection now uses normal Python attribute semantics, including descriptor properties and forwarded `__getattr__`; zero-argument `super()` reads its defining `__class__` closure when a method is replaced by a decorator. Native Pydantic Core now applies computed-field `serialization_exclude_if`, config string transforms, tuple-subclass argument input, model/dataclass extra-config validation, Decimal `allow_inf_nan`, and union plain-serializer branch order. General iterator classes expose their protocol methods; class attribute access invokes metaclass `__getattr__`; `operator.or_` respects reflected Python operators and generic alias union/string rendering. The untouched computed-fields file passed 33 tests, 2 skips, 2 expected failures; the untouched config file passed **84/84**. Core fixtures and FastAPI/CPython oracles were added for each general behavior. The latest full Pydantic matrix before the config fixes stopped at 9% with **368 passed, 193 skipped, 2 xfailed**; a fresh full matrix is still required. The seven-project matrix remains **4/7 exit-zero**, with Pydantic, AnyIO, and HTTPX open. The refreshed full core fixture gate passed (`scratch/fixtures-config-green-20260924.log`) and CTest passed **53/53** (`scratch/ctest-config-green-20260924.log`). The local FastAPI gate is running; demo restart and fresh matrix are still pending. No commit or push.

2026-09-24 status continuation: The normal assertion-rewrite Pydantic matrix reached **450 passed, 193 skipped, 2 xfailed** before a model-construction warning mismatch. That warning was corrected in the native Pydantic Core module. A general compiler fix now resolves `value.__class__(...)` as an attribute load followed by a call; its CPython oracle passes and the untouched Pydantic construction file passes **51/51**. The next full Pydantic matrix reached **531 passed, 193 skipped, 2 xfailed**, 11% of 6,091 collected tests, before `tests/test_dataclasses.py::test_value_error`: the native validator returns location `('b',)` where CPython pydantic-core returns positional `(1,)`. Pydantic remains open; seven-project matrix stays **4/7 exit-zero**. Full fixture/CTest/local FastAPI gates must be refreshed after the latest changes. No commit or push.

2026-09-24 continuation: General vars(class) now returns an ordered, live mappingproxy; Python 3.14 typing.Generic.__class_getitem__ delegates to stdlib _generic_class_getitem, producing _GenericAlias and correct generic Pydantic dataclass specialization. Native dataclass validation now preserves positional error locations, bypasses frozen/assignment setters for internal field writes, clears preexisting __new__ attributes on validation, handles keyword-only fields and all unexpected positional arguments, reports alias locations, and normalizes ArgsKwargs empty kwargs/reflected comparison. The untouched Pydantic dataclass module passed 229 tests, 2 skips, 1 xfail. The untouched datetime module passed 233/233 after native temporal numeric/text error work; decorators passed 31/31 after builtin type signatures were corrected. Scope analysis now forwards nested class annotation reads through enclosing functions; untouched deferred-annotations passed 9/9. CPython-oracle fixtures and FastAPI TestClient cases accompany these fixes. The latest completed full Pydantic matrix reached 1,022 passed, 195 skipped, 3 xfailed at 20% before deferred-annotation failure; a fresh matrix after that fix is in progress. Core fixture/CTest/local FastAPI gates passed before the most recent changes and need refreshing; demo restart remains pending. No commit or push.

2026-09-24 metaclass/deprecated-field continuation: the general class-attribute paths (getattr, LOAD_ATTR, and method calls) now honor a metaclass's custom __getattribute__, with AttributeError fallback to __getattr__. The CPython oracle tests direct and builtin reads, method calls, and fallback; a FastAPI TestClient route confirms Pydantic Extra deprecation warnings match CPython. Untouched Pydantic test_deprecated.py passed 48/48. The next full matrix reached 1,078 passed, 195 skipped, 3 xfailed (20%) at test_deprecated_fields.py. This test fails identically on CPython 3.14 under the test environment's pytest 9.1.1; Pydantic's uv.lock pins pytest 8.3.5. Installed that pure-Python pinned runner into isolated scratch/pydantic-pytest-pinned. CPython then passed the file 12/12, and XLang3 exposed a genuine native model validate_assignment issue with a data descriptor. Native Pydantic Core now writes validated model fields into the instance __dict__, as CPython pydantic-core does; untouched deprecated-fields passed 12/12 on XLang3. A FastAPI TestClient assignment/warning oracle matches CPython. Full core fixtures passed, CTest 53/53 passed, and local FastAPI gate passed before the native assignment change. Fresh full Pydantic matrix with locked pytest and fresh local FastAPI gate are in progress. Demo was verified ready on the Release runtime, then stopped for native DLL relink; restart after verification. Seven-project matrix remains 4/7 exit-zero. No commit or push.

2026-09-24 tagged-union continuation: under locked pytest 8.3.5, the full untouched Pydantic suite passed deprecated_fields and deprecated_validate_arguments, reaching 1,118 passed, 195 skipped, 3 xfailed (21%) before discriminated-union validation on primitive input. Native Pydantic Core tagged-union validation now applies the same from_attributes input candidacy as model-fields before attempting a tag lookup: primitive string/int/list/tuple/None inputs yield model_attributes_type, while a dict without the tag yields union_tag_not_found. The untouched Pydantic test_discriminated_union.py passed 76 tests, 4 xfailed; public FastAPI route and direct-model oracles match CPython 3.14. A fresh full Pydantic matrix and FastAPI gate are in progress. Seven-project matrix remains 4/7 exit-zero. Demo remains stopped for native rebuild; restart after verification. No commit or push.

2026-09-24 docs/source continuation: after discriminated-union fix, the full pinned Pydantic suite reached 1,191 passed, 198 skipped, 7 xfailed (22%) at nested duplicate class doc extraction. Class __firstlineno__ matched CPython; native _tokenize.TokenizerIter eagerly read ahead to a later indentation error and raised before yielding valid class tokens, causing Python 3.14 inspect.getblock to return only the class header. The native tokenizer now defers lexical exceptions until buffered tokens have been yielded. Core inspect/tokenizer oracle matches CPython; untouched Pydantic test_docs_extraction.py passed 15 tests, 1 xfailed; a FastAPI TestClient route using a duplicate-name model returns the same field docstring as CPython. Fresh full Pydantic matrix, fixtures, CTest, and local FastAPI gate are in progress. Seven-project matrix remains 4/7 exit-zero. Demo remains stopped for rebuild; restart after verification. No commit or push.

2026-09-24 serialization continuation: the last full pinned Pydantic matrix reached 1,235 passed, 198 skipped, 8 xfailed (23% of 6,091) before edge-cases. General dict view __contains__, comprehension outer-iterable scope, container str/repr, and native nested validator location fixes advanced the untouched edge-cases module to 144 passed, 1 skipped. The next hashable-field serialization failure revealed two native pydantic-core issues: an is-instance schema retained unknown JSON objects instead of using the general serializer, and fallback=None was treated as a callable rather than absence of a fallback. Native is-instance now delegates to general serialization; JSON serialization propagates PydanticSerializationError directly; all three serializer entry paths treat fallback=None as absent. The unchanged upstream test_hashable_serialization passed, and new FastAPI TestClient hashable_serialization_contract matches CPython 3.14. The full edge-cases module is running. Full fixture/CTest/local FastAPI gates and seven-project matrix still need fresh runs; overall remains 4/7 exit-zero. Demo remains stopped for native rebuild. No commit or push.
2026-09-24 pathlike/importlib and gate continuation: untouched Pydantic edge-cases reached 151 passed, 1 skipped before spec_from_file_location rejected pathlib.Path. Its native importlib implementation now uses the existing general PathLike conversion and also handles PathLike when deriving empty submodule search locations. The unchanged upstream test_resolve_annotations_module_missing passed. CPython-oracle core importlib_pathlike_spec and FastAPI TestClient pathlike_module_spec_contract passed; the full edge-cases module then reached 155 passed, 1 skipped before bytes-subclass validation. The full fixture gate passed after inspect.stack and Enum container-repr expected output was updated to check the new CPython-compatible behavior. CTest passed 53/53. The local FastAPI gate caught duplicated URL validation error locations following the new nested-validator-prefix logic; conversion now retains a location already bearing the current prefix. Direct URL and nested validator FastAPI oracles match CPython. The complete local gate is rerunning; do not claim green until it exits 0. Latest full Pydantic matrix remains 23%, overall 4/7 suites. Demo still stopped. No commit or push.
2026-09-24 checkpoint: full fixture gate exits 0 (scratch/fixtures-final-20260924.log), CTest passes 53/53 (scratch/ctest-final-20260924.log), and the complete local FastAPI gate exits 0 after the location-prefix correction (scratch/fastapi-local-final-3-20260924.log), including Uvicorn HTTP/HTTPS and WebSocket end-to-end checks. The Release production demo is running on XLang3 at http://127.0.0.1:8765/ (PID 9116 at startup); /api/runtime returned implementation xlang3, Python 3.14.7, FastAPI, ready. git diff --check passes. The latest full upstream seven-suite result is still 4/7; latest full Pydantic result is 1,235 passed, 198 skipped, 8 xfailed at 23%, with later targeted edge-cases progressing to 155 passed, 1 skipped and stopping on bytes subclass validation. A new full Pydantic matrix run after these fixes is required before claiming further percentage. AnyIO and HTTPX remain open. No commit or push.
2026-09-24 bytes subclass and class-body continuation: CPython 3.14 preserves bytes-subclass identity through strict and lax Pydantic bytes validation; XLang3 previously constructed bytes.__new__(Subclass, data) without its byte payload or __len__. General bytes.__new__, bytes.__len__, and bytes repr behavior now honor subclass payloads, while native Pydantic bytes validation retains bytes subclass input after length checks. The unchanged upstream bytes-subclass test passes; core bytes_subclass_constructor and FastAPI bytes_subclass_validation_contract match CPython. The full edge-cases file then reached 160 passed, 1 skipped and exposed missing class-body `with` bindings. Class lowering now recursively handles `with` body statements and class namespace targets, sharing the normal with-exit control flow; bytecode magic bumped from 0x3f to 0x40 in all nine sites. All three unchanged abstract-decorator cases pass, and core class_with_bindings plus FastAPI class_with_abstract_validator_contract match CPython. The full pinned Pydantic matrix reached 1,371 passed, 199 skipped, 8 xfailed (26% of 6,091 collected) before a model input mismatch. Native model-fields validation now converts only true Mapping inputs, not any iterable of pairs; unchanged forward-ref model test and FastAPI model_mapping_input_contract pass. Targeted full edge-cases reaches 183 passed, 1 skipped before test_nested_type_statement. XLang3 currently parses Python 3.14 `type` statements as ordinary assignment (src/parser/parser.cpp), so TypeAliasType metadata and lazy evaluation are missing; CPython oracle scratch/type-alias-oracle.py confirms the difference. This is a general language gap, not a FastAPI-specific workaround. Full fixtures and CTest 53/53 pass on current Release. Complete local FastAPI gate is running. Demo stopped for runtime relink. Seven-suite matrix remains 4/7 exit-zero; AnyIO and HTTPX open. No commit or push.
2026-09-24 verified checkpoint after class-with/mapping fixes: full fixture gate exits 0 (scratch/fixtures-classwith-20260924.log), CTest 53/53 passes (scratch/ctest-classwith-20260924.log), and complete local FastAPI gate exits 0 (scratch/fastapi-local-classwith-20260924.log), including new bytes subclass, abstract-validator, mapping-input oracles, plus real Uvicorn HTTP/HTTPS and WebSocket integration. Production demo restarted on XLang3 at http://127.0.0.1:8765/ (PID 5500 at startup), /api/runtime reports xlang3, Python 3.14.7, FastAPI, ready. git diff --check passes. Latest full Pydantic run before mapping fix: 1,371 passed, 199 skipped, 8 xfailed, stop at 26%; targeted full edge-cases after mapping fix: 183 passed, 1 skipped, next failure at Python 3.14 nested `type` statement. CPython oracle confirms required TypeAliasType lazy semantics and generic type parameters; current parser's plain-assignment lowering is incomplete. Seven-project upstream matrix stays 4/7 exit-zero; AnyIO/HTTPX and full Pydantic remain open. No commit or push.

2026-09-24 type-alias continuation: Python 3.14 `type` statements now lower to native `_typing.TypeAliasType` instances with lazy cached values, simple TypeVar parameters, recursive aliases, and captured function/class scope values. The unchanged upstream `test_nested_type_statement` and the entire `tests/test_edge_cases.py` module pass (195 passed, 1 skipped). New core `type_alias_statement` and FastAPI TestClient `type_alias_model_contract` match CPython 3.14. The rebuilt Release runtime passes the full fixture gate, CTest 53/53, and the complete local FastAPI gate, including real Uvicorn HTTP/HTTPS and WebSocket checks (`scratch/fixtures-typealias-20260924.log`, `scratch/ctest-typealias-20260924.log`, `scratch/fastapi-local-typealias-20260924.log`). The production demo is running at http://127.0.0.1:8765/ and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. One fresh full Pydantic run stopped at 8% with a pytest AST-reporting internal error masking the original failure; the full computed-fields module passes alone (33 passed, 2 skipped, 2 xfailed). A second full run with native traceback is in progress. Do not promote Pydantic to green or claim a new overall percentage yet; the seven-project matrix remains 4/7 exit-zero. Python 3.14 type-parameter bounds/defaults/variadics and `exec` globals without `__name__` remain general compatibility work. No commit or push.

2026-09-24 root-model continuation: the second full untouched Pydantic run passed the earlier computed-fields interruption and reached **1,576 passed, 199 skipped, 8 xfailed**, then stopped at `tests/test_fields.py::test_root_model_arbitrary_private_field_works` (about 29% of 6,091 collected). The native pydantic-core root-model validator had returned before the same schema `post_init` hook used for ordinary models. It now calls the hook after setting validated root/fields, enabling Pydantic's pure-Python private-attribute initialization. The unchanged failing test passes, and the full `tests/test_fields.py` module passes 68/68. New public FastAPI TestClient `root_model_private_contract` matches CPython 3.14. Native Release package rebuild passed; fresh fixture/CTest/local FastAPI gates are running. Demo stopped for DLL relink and must be restarted after verification. Overall matrix remains 4/7 exit-zero. No commit or push.

2026-09-24 verified root-model checkpoint: full fixtures exit 0 (`scratch/fixtures-rootpost-20260924.log`), CTest passes 53/53 (`scratch/ctest-rootpost-20260924.log`), and the complete local FastAPI gate exits 0 (`scratch/fastapi-local-rootpost-20260924.log`), including the new private-field contract and real Uvicorn HTTP/HTTPS and WebSocket end-to-end checks. The production demo is running at http://127.0.0.1:8765/ (PID 17684 at startup); `/api/runtime` reports XLang3 3.14.7, FastAPI, ready. `git diff --check` has no whitespace errors. Latest completed full Pydantic run stopped at 29%; a fresh full run after the root-model fix remains required. Seven upstream projects remain 4/7 exit-zero, with Pydantic, AnyIO, and HTTPX open. No commit or push.

2026-09-24 generic typing and forward-reference checkpoint: fresh untouched full Pydantic reached 1,683 passed, 199 skipped, 8 xfailed before `test_invalid_forward_ref` (about 31% of collected tests reached). General class subscription now rejects unsubscriptable classes and exposes real `__class_getitem__` on built-in generic classes and weakref.ReferenceType. Python 3.14 AST conversion now preserves TypeAlias nodes and simple TypeVar type parameters on generic functions and classes; TypeAliasType supports union operands; generic function `__type_params__` contains native TypeVar objects; and generic classes inherit Generic[params] naturally. These changes pass CPython 3.14 oracle fixtures and public FastAPI TestClient contracts. The untouched Pydantic `tests/test_forward_ref.py` now reaches 61 passed, 3 xfailed, then fails `test_implicit_type_alias_recursive_error_message`: XLang3 raises RecursionError without the Pydantic-added explanatory note. This remains a general recursion/type-evaluation discrepancy to investigate, not a place for a Pydantic-specific patch. Full fixtures exit 0, CTest passes 53/53, complete local FastAPI gate exits 0 including Uvicorn HTTP/HTTPS/WebSocket, and `git diff --check` is clean. Production demo is live at http://127.0.0.1:8765/ and `/api/runtime` reports XLang3 3.14.7 ready. Overall upstream matrix remains 4/7 exit-zero; Pydantic, AnyIO, and HTTPX are open, with AnyIO/HTTPX collection blocked by cryptography/trustme native dependency. No commit or push.

2026-09-24 forward-union and native generic collection continuation: XLang3 now converts string arguments to annotationlib.ForwardRef when substituting a GenericAlias type parameter and when creating typing.Union[...] directly, matching Python 3.14. The unchanged upstream Pydantic `test_string_annotation_union_type` passes, as do the new core `generic_string_forward_ref` oracle and public FastAPI `generic_string_union_model_contract`. A proposed whole-Python-stack recursion guard made the recursive alias test pass in the main pytest thread but exposed an UnboundLocalError in FastAPI/worker-thread recursion; that guard and its incomplete regression were rolled back. The pre-existing recursive alias note gap remains open. A fresh full Pydantic collection exposed native generic support missing on collections.deque, _sre.Pattern/Match, and BaseExceptionGroup. These classes now provide real inherited `__class_getitem__` returning GenericAlias, with CPython 3.14 core oracles and FastAPI public-route tests. Release target builds passed after each change. A full Pydantic run with upstream default pytest addopts, the full fixture runner, and CTest are currently running after the latest build; rerun the complete local FastAPI gate and restart the demo after they finish. The overall upstream matrix remains 4/7 exit-zero; no commit or push.

2026-09-24 verified native-generic checkpoint: the latest Release xlang3 build passes. The full fixture runner exits 0 (`scratch/fixtures-native-generics-20260924.log`), CTest passes 53/53 (`scratch/ctest-native-generics-20260924.log`), and the complete FastAPI local gate exits 0 (`scratch/fastapi-local-native-generics-20260924.log`), including the new generic-string union, deque, regex pattern, and exception-group contracts plus real Uvicorn HTTP/HTTPS and WebSocket end-to-end checks. The fresh untouched full Pydantic 2.13.5 run, with its upstream default benchmark-disable setting, passed 1,701 tests, skipped 199, xfailed 11, then stopped at `tests/test_forward_ref.py::test_implicit_type_alias_recursive_error_message` (`scratch/pydantic-full-excgroup-defaultopts-20260924.log`). The remaining failure is the same missing explanatory note on RecursionError; no upstream files or skip list were changed to hide it. The production demo is running at http://127.0.0.1:8765/ on XLang3 3.14.7. The seven-project upstream matrix remains 4/7 exit-zero, with Pydantic, AnyIO, and HTTPX still open. No commit or push.

2026-09-24 recursion and generic-alias hash continuation: VM recursion accounting now includes reentrant saved Python frames, and optimized property access propagates a deferred getter error instead of silently returning to its caller. Core `recursive_property_error` and the public FastAPI TestClient `recursive_alias_error_contract` match CPython 3.14, including the worker-thread case. The unchanged full Pydantic `tests/test_forward_ref.py` passes 66 tests with 4 expected failures. Full fixtures, CTest 53/53, and the complete local FastAPI gate passed on this recursion build (`scratch/fixtures-recursion-fixed-20260924.log`, `scratch/ctest-recursion-fixed-20260924.log`, `scratch/fastapi-local-recursion-fixed-20260924.log`), including real Uvicorn HTTP/HTTPS and WebSocket. The next complete untouched Pydantic run reached 1,724 passed, 199 skipped, 12 xfailed before `tests/test_generics.py::test_cache_keys_are_hashable` (about 31% of 6,091 collected tests; `scratch/pydantic-full-recursion-fixed-20260924.log`). Its cause was structural `GenericAlias` equality paired with identity hashing: separately constructed equal Callable aliases hashed differently, duplicating Pydantic generic-model cache entries. GenericAlias hashing now follows its structural equality, including order-insensitive union members; the unchanged failing Pydantic test passes. New core `generic_alias_hash` and public FastAPI `generic_alias_cache_contract` match CPython 3.14. Release build passed; fresh full Pydantic, fixtures, CTest, and local FastAPI gates are running for this hash build. Demo was stopped for relink and should be restarted after verification. Overall seven-project matrix remains 4/7 exit-zero; AnyIO/HTTPX still open. No commit or push.

2026-09-24 verified alias-hash checkpoint: latest Release build (`scratch/build-generic-alias-hash-20260924.log`), full fixtures (`scratch/fixtures-alias-hash-20260924.log`), CTest 53/53 (`scratch/ctest-alias-hash-20260924.log`), and complete local FastAPI gate (`scratch/fastapi-local-alias-hash-20260924.log`) all pass, including real Uvicorn HTTP/HTTPS/WebSocket checks. The unchanged full Pydantic run now stops one test later: 1,725 passed, 199 skipped, 12 xfailed, failure at `tests/test_generics.py::test_caches_get_cleaned_up`, where 600 weak-value cache entries remain after `gc.collect` instead of fewer than 100 (`scratch/pydantic-full-alias-hash-20260924.log`). A scratch exact-body probe passes under pytest outside Pydantic's conftest and fails with the upstream conftest plugin; overriding only its autouse `validate_json_schemas` fixture makes that probe pass again. The fixture monkeypatches `GenerateJsonSchema.generate`, so the next investigation is how that active monkeypatch/closure interacts with GC reachability in XLang3. Do not change, skip, or disable the upstream fixture to claim parity. The production demo is live at http://127.0.0.1:8765/ (PID 7732 at startup), and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. `git diff --check` exits 0. Overall upstream matrix remains 4/7 exit-zero, with Pydantic, AnyIO, and HTTPX open. No commit or push.

2026-09-24 generic/native-boundary continuation: generalized class-cycle GC now clears cycles among locally defined classes, including an unweakrefed parent retaining a child; unchanged upstream Pydantic `test_caches_get_cleaned_up` and `test_circular_generic_refs_get_cleaned_up` passed together. Generic model validation now revalidates instances from a different specialization of the same generic origin; unchanged Pydantic `test_nested` passed. Mutable set and frozenset subclass construction, dictionary-subclass pydantic-core iteration/serialization, and tuple-subclass native `len` were repaired; the unchanged `test_replace_types_with_user_defined_generic_type_field` passes. TypeVar/ParamSpec representation now matches Python 3.14, including variance markers, and inferred PEP 695 type parameters use `infer_variance=True`. Fully bound GenericAlias re-subscription raises TypeError, restoring the upstream `test_generic_model_as_parameter_to_generic_type_alias` expected failure. CPython-oracle fixtures `gc_class_components`, `builtin_generic_subclasses`, `type_parameter_repr`, and `generic_alias_resubscript`, plus public FastAPI TestClient contracts `gc_class_cycle_contract`, `generic_model_revalidation_contract`, and `generic_native_container_contract`, match CPython 3.14. Latest Release xlang3 build passed (`scratch/build-generic-alias-pending-retry-20260924.log`); CTest 53/53 passed (`scratch/ctest-generic-alias-final-20260924.log`). Full fixture script completed all cases, including intentional nonzero-error cases (`scratch/fixtures-generic-alias-final-20260924.log`); the outer shell reported 1 only because `$LASTEXITCODE` retained the expected last negative fixture. Full unchanged Pydantic generics module and complete local FastAPI gate are currently running. A fresh full Pydantic suite, seven-project matrix, full Release build, and production demo restart remain outstanding. No commit or push.

2026-09-24 set-equality checkpoint: untouched Pydantic `tests/test_generics.py` is now fully green: 120 passed, 2 skipped, 4 xfailed (`scratch/pydantic-generics-set-fixed-20260924.log`). The final failure was frozen generic models with equal hashes and `__eq__` comparing equal occupying two set entries; `set_add_runtime` now deduplicates on matching runtime hashes and Python equality. CPython-oracle `set_hash_equality` and the public FastAPI `generic_native_container_contract` frozen-model case match Python 3.14. Release xlang3 target build passed (`scratch/build-set-equality-20260924.log`). Full fixture gate exits 0 (`scratch/fixtures-set-equality-20260924.log`); CTest first had one intermittent parser-test failure, which passed alone and on a complete rerun 53/53 (`scratch/ctest-set-equality-rerun-20260924.log`). The full local FastAPI gate is still running on this build; the unchanged full Pydantic suite has been started (`scratch/pydantic-full-set-fixed-20260924.log`). Seven-project matrix remains last confirmed 4/7 exit-zero, with Pydantic/AnyIO/HTTPX open. No commit or push.

2026-09-24 JSON-schema continuation: the untouched full Pydantic suite advanced to 1,993 passed, 203 skipped, 18 xfailed, then stopped at `test_path_types[Annotated-file-path]` because a lambda dictionary unpack in XLang3's AST clone turned `{**handler(source), ...}` into `{None: handler(source), ...}`. General AST DictExpr cloning now preserves null keys as unpack markers; the pinned upstream test and public FastAPI file-path schema route pass, as does CPython-oracle `lambda_dict_unpack`. The XLang3 bytecode magic was advanced to invalidate old compiler output for unchanged Python source. A full Release ALL_BUILD, full fixture gate, CTest 53/53, and complete FastAPI local gate all passed on that checkpoint (`scratch/build-full-release-dict-unpack-20260924.log`, `scratch/fixtures-dict-unpack-20260924.log`, `scratch/ctest-dict-unpack-20260924.log`, `scratch/fastapi-local-dict-unpack-20260924.log`).

The unmodified JSON-schema module then exposed `typing.Literal[['a', 1]]`: `dict.fromkeys` emitted RuntimeError for an unhashable key instead of Python 3.14's TypeError and message. General `dict.fromkeys` propagation now matches CP; core `dict_fromkeys_unhashable` and public `literal_list_schema_contract` pass. Next, `class ListEnum(list[int], Enum)` sent GenericAlias to `EnumType.__prepare__`. Class lowering now resolves bases once, passes the resolved tuple to metaclass selection and `__prepare__`, and retains originals for `__orig_bases__`; core `resolved_prepare_bases` verifies Enum and single `__mro_entries__` invocation, public `generic_enum_schema_contract` matches CP. The complete JSON-schema module reached 408 passed, 1 skipped, 1 xfailed before a plain-serializer test revealed an AST-compiled nested f-string lambda capturing synthetic `format` as a free variable. AST FormattedValue now uses internal f-string formatting builtins, which closure capture excludes; core `ast_nested_fstring_builtin`, public `plain_serializer_schema_contract`, and the unchanged upstream `test_plain_serializer_applies_to_default` pass. Bytecode magic was advanced again with the compiler changes so cached unchanged modules recompile. Current Release xlang3 target build passed (`scratch/build-ast-fstring-capture-20260924.log`); full JSON-schema module, full fixtures, CTest, and local FastAPI gate are running. Full Pydantic suite, 7-project matrix, full Release ALL_BUILD on this final build, and production demo restart remain outstanding. No commit or push.

2026-09-24 verified AST-f-string checkpoint: untouched Pydantic `tests/test_json_schema.py` is fully green, 534 passed, 1 skipped, 1 xfailed (`scratch/pydantic-json-schema-ast-fstring-20260924.log`). The latest xlang3 Release target build (`scratch/build-ast-fstring-capture-20260924.log`), full fixture runner (`scratch/fixtures-ast-fstring-20260924.log`), CTest 53/53 (`scratch/ctest-ast-fstring-20260924.log`), and complete local FastAPI gate (`scratch/fastapi-local-ast-fstring-20260924.log`) pass, including the new contracts and real Uvicorn HTTP/HTTPS/WebSocket checks. The unchanged full Pydantic 6,091-test suite is running on this build (`scratch/pydantic-full-ast-fstring-20260924.log`); it has passed the prior generics stop and is still advancing. The last completed full-suite count remains 1,993 passed, 203 skipped, 18 xfailed before the now-fixed file-path schema case. The production demo was stopped for relinking and must be restarted after validation. The pinned cryptography 46.0.7 package from Pydantic's local uv.lock was installed in isolated `scratch/cryptography-test-deps` without affecting active suite paths. CPython 3.14 plus trustme generated a real CA cert; XLang3 loads cryptography metadata but cannot import `cryptography.x509` because the wheel's `_rust.pyd` CPython extension is unavailable to XLang3. AnyIO/HTTPX matrix collection therefore still needs a genuine native cryptography boundary, not a skip. Seven-project matrix remains last confirmed 4/7 exit-zero. Final full Release ALL_BUILD on the current code, full matrix rerun, and demo restart remain outstanding. No commit or push.

2026-09-24 environment-boundary checkpoint: the unchanged full Pydantic suite reached 2,445 passed, 204 skipped, 19 xfailed, then stopped at tests/test_main.py::test_ultra_simple_missing. Its autouse fixture sets PYDANTIC_ERRORS_INCLUDE_URL=false via os.environ; on Windows the statically linked native pydantic-core module's std::getenv had a separate C runtime environment copy. The native formatter now reads the Windows process environment; general os.putenv/os.unsetenv also synchronize the executable C runtime copy. The exact unchanged upstream test passes (1 passed). Full-suite and full gates must be rerun on this change. Seven-project matrix last confirmed 4/7 exit-zero; demo still stopped. No commit or push.

2026-09-24 Pydantic main-module checkpoint: fixed Windows process-environment lookup for pydantic-core's URL setting and matched its one-time cache behavior; fixed ordinary-instance vs dict comparison, deletion and dir() of materialized instance __dict__ keys, the __eq__-without-__hash__ class rule, dict method and mapping-protocol leakage from ordinary instance attribute dictionaries, pydantic-core nested filter TypeError propagation, object.__init_subclass__ keyword rejection including implicit-object classes, type.__subclasses__ descriptor binding, and all-underscore private-name mangling. Each change has a Python 3.14 oracle and public FastAPI TestClient contract in the registered gates. The unchanged Pydantic tests/test_main.py is fully green: 244 passed, 25 skipped, 1 xfailed (scratch/pydantic-main-private-underscore-20260924.log). Full Release ALL_BUILD passed before the subsequent instance-dict/filter/class/compiler edits (scratch/build-full-release-model-compare-20260924.log); current xlang3 target build passed (scratch/build-private-underscore-20260924.log), and the nested-filter native package build passed (scratch/build-nested-filter-20260924.log). Full 6,091-test Pydantic suite is running on this build (scratch/pydantic-full-main-green-20260924.log). Full fixtures, FastAPI gate, CTest, final ALL_BUILD, matrix rerun, and demo restart remain outstanding. Seven-project matrix last confirmed 4/7 exit-zero; cryptography boundary for AnyIO/HTTPX remains open. No commit or push.

2026-09-24 MISSING sentinel checkpoint: the unmodified 6,091-test Pydantic suite reached 2,916 passed, 229 skipped, 21 xfailed before `tests/test_missing_sentinel.py::test_missing_sentinel_model` failed (`scratch/pydantic-full-main-green-20260924.log`). Native pydantic-core model-field and extra-field serialization now omits the pure-Python `pydantic_core.MISSING` sentinel by identity; standalone missing-sentinel schema serialization preserves it and raises PydanticSerializationUnexpectedValue for other values. The entire unchanged upstream `tests/test_missing_sentinel.py` passes 8/8 (`scratch/pydantic-missing-20260924.log`). New CPython 3.14-oracle public FastAPI TestClient `tests/fastapi/missing_sentinel_contract.py` matches XLang3 and is registered in the local gate. The latest package build passes (`scratch/build-missing-20260924.log`), and `git diff --check` passes. The prior current-build full fixture runner, CTest 53/53, and local FastAPI gate passed before only the MISSING native package change (`scratch/fixtures-main-green-20260924.log`, `scratch/ctest-main-green-20260924.log`, `scratch/fastapi-local-main-green-20260924.log`). Full Pydantic rerun is in progress (`scratch/pydantic-full-missing-20260924.log`); Pydantic and AnyIO/HTTPX remain open, seven-project matrix last confirmed 4/7 exit-zero, production demo stopped, no commit or push.

2026-09-24 class-signature checkpoint: the previous full unmodified Pydantic rerun passed the entire missing-sentinel file and advanced to 2,935 passed, 229 skipped, 21 xfailed before `tests/test_model_signature.py::test_signature_is_class_only` failed (`scratch/pydantic-full-missing-20260924.log`). General explicit `object.__getattribute__` now invokes class descriptors with the correct instance/owner, respecting data-descriptor precedence over instance storage and preserving instance value precedence for non-data descriptors. Core CPython-oracle `object_getattribute_descriptor` and public FastAPI `model_class_signature_contract` match Python 3.14. The full unchanged signature module passes 14 passed, 1 skipped (`scratch/pydantic-signature-20260924.log`). Full Release ALL_BUILD passed before this latest runtime edit (`scratch/build-full-missing-20260924.log`); xlang3 target rebuild passed after it (`scratch/build-signature-20260924.log`). Full local FastAPI gate including MISSING passed before this latest runtime edit (`scratch/fastapi-local-missing-20260924.log`); CTest 53/53 passed before it (`scratch/ctest-missing-20260924.log`). Full Pydantic suite and fixture runner are running on the new runtime. Matrix last confirmed 4/7 exit-zero, demo stopped, no commit or push.

2026-09-24 signature gate verification: full fixture runner passes on latest descriptor runtime (`scratch/fixtures-signature-20260924.log`), full Release ALL_BUILD passes (`scratch/build-full-signature-20260924.log`), and CTest passes 53/53 (`scratch/ctest-signature-20260924.log`). Full local FastAPI gate and full unmodified Pydantic suite are currently running on this same build (`scratch/fastapi-local-signature-20260924.log`, `scratch/pydantic-full-signature-20260924.log`). No commit or push.

2026-09-24 URL/network checkpoint: the full unchanged Pydantic suite on the signature fix reached 3,023 passed, 231 skipped, 21 xfailed before `tests/test_networks.py` hit URL whitespace error classification (`scratch/pydantic-full-signature-20260924.log`). Full untouched networks module exposed seven general native URL/serialization gaps: nonempty whitespace-only URL vs empty-string error, unbracketed IPv6 port error, empty userinfo canonicalization, file URL localhost/leading-slash normalization, empty-host non-special schemes, strict URL syntax wording, and Python serializer PydanticSerializationUnexpectedValue accumulation when warnings='error'. Native pydantic-core now follows CPython 3.14 for each; two public FastAPI CPython-oracle contracts (`url_blank_input_contract`, `url_network_semantics_contract`) match XLang3 and are registered. The unchanged networks module is fully green: 304 passed, 1 skipped, 1 xfailed (`scratch/pydantic-networks-green-20260924.log`). Full Release ALL_BUILD passes (`scratch/build-full-networks-20260924.log`); git diff --check passes. The full Pydantic suite is running on this build (`scratch/pydantic-full-networks-20260924.log`). The last full fixture, CTest 53/53, and local FastAPI gate passed before only the latest native URL package changes; rerun pending. Seven-project matrix last confirmed 4/7 exit-zero; demo stopped; no commit or push.

2026-09-24 URL gate verification: latest full Release ALL_BUILD passes (`scratch/build-full-networks-20260924.log`), full fixture runner passes (`scratch/fixtures-networks-20260924.log`), CTest passes 53/53 (`scratch/ctest-networks-20260924.log`), and complete local FastAPI gate passes including URL contracts and real Uvicorn HTTP/WebSocket (`scratch/fastapi-local-networks-20260924.log`). Production-style demo restarted on 127.0.0.1:8765, PID 27260; live `/api/runtime` returned implementation xlang3, Python 3.14.7, FastAPI, status ready. Full unmodified Pydantic suite is still running (`scratch/pydantic-full-networks-20260924.log`). No commit or push.

2026-09-24 union pipeline checkpoint: the full unchanged Pydantic suite passed the URL/network section and reached 3,487 passed, 253 skipped, 22 xfailed before `tests/test_pipeline.py::test_composition` found an extra side-effectful transform call in a later successful union branch (`scratch/pydantic-full-networks-20260924.log`). Native pydantic-core smart-union input-exactness probing now descends nested function-after/wrap schemas without running validators, so an exact successful first branch stops before the second branch. Exact untouched test passes; the entire unchanged pipeline module passes 64/64 (`scratch/pydantic-pipeline-20260924.log`). Public FastAPI CPython-oracle `union_pipeline_side_effect_contract` matches XLang3 for first-branch failure, first-branch success, and both-branch failure, and is registered in the local gate. Full Release ALL_BUILD passes (`scratch/build-full-pipeline-20260924.log`); full Pydantic suite rerun is active (`scratch/pydantic-full-pipeline-20260924.log`). The XLang3 demo was stopped for the Windows native package relink and restarted on 127.0.0.1:8765, PID 25980; `/api/runtime` again returned xlang3, Python 3.14.7, FastAPI, ready. Full fixtures, CTest, and FastAPI gates passed before only this native union edit and will be rerun. Matrix last confirmed 4/7 exit-zero; no commit or push.

2026-09-24 pipeline gate verification: full Release ALL_BUILD passes (`scratch/build-full-pipeline-20260924.log`), full fixture runner passes (`scratch/fixtures-pipeline-20260924.log`), CTest passes 53/53 (`scratch/ctest-pipeline-20260924.log`), and `git diff --check` passes. The full local FastAPI gate including the new union side-effect contract is still running (`scratch/fastapi-local-pipeline-20260924.log`); full Pydantic suite is running (`scratch/pydantic-full-pipeline-20260924.log`). Production demo is live at 127.0.0.1:8765, PID 25980, and `/api/runtime` reports xlang3 Python 3.14.7/FastAPI ready. No commit or push.

2026-09-24 pipeline final local gate: complete local FastAPI gate passes on current native union fix, including new side-effect oracle and real Uvicorn HTTP/WebSocket (`scratch/fastapi-local-pipeline-20260924.log`). Current full Release build, fixtures, and CTest 53/53 are also green. Full Pydantic suite is still running. XLang3 demo live at 127.0.0.1:8765. No commit or push.

2026-09-24 exec globals checkpoint: unchanged full Pydantic suite reached 3,503 passed, 253 skipped, 22 xfailed before test_plugins.py::test_all_handlers exposed AST-compiled f-string formatting of a dict containing a model. General builtin_format empty-spec now delegates to builtin_str_from_value; exact test passes and public ast_model_container_format_contract matches CPython 3.14. The next unchanged plugin test exposed exec(code, {'bar':'baz'}) wrongly synthesizing __name__='<exec>'. General module-backed exec globals now preserve an absent __name__ while module bootstrap remains for named imported modules. Core CPython oracle exec_missing_name and public FastAPI TypeAdapter/TestClient exec_missing_name_contract match CPython 3.14; unchanged entire Pydantic test_plugins.py passes 14/14 (scratch/pydantic-plugins-exec-name-4-20260924.log). Full Release ALL_BUILD passes (scratch/build-full-exec-name-20260924.log). Full fixture/CTest/FastAPI gates are running; full Pydantic suite rerun pending. Seven-project matrix remains last confirmed 4/7; AnyIO/HTTPX cryptography native boundary open; demo stopped; no commit or push.

2026-09-24 exec gate verification: full Release ALL_BUILD passed, core fixture runner passed including exec_missing_name, and complete local FastAPI gate passed including the new TypeAdapter/TestClient contract and real Uvicorn HTTP/HTTPS/WebSocket checks (scratch/build-full-exec-name-20260924.log, scratch/fixtures-exec-name-20260924.log, scratch/fastapi-local-exec-name-20260924.log). CTest combined run was 52/53: final Visual Studio debugpy launch timed out while FastAPI gate ran concurrently; the exact test passed alone in 3.8 seconds (scratch/ctest-exec-name-20260924.log, scratch/ctest-debugpy-isolated-exec-name-20260924.log). Full unchanged Pydantic suite rerun is active (scratch/pydantic-full-exec-name-20260924.log). Demo stopped; matrix last confirmed 4/7; no commit or push.

2026-09-24 private int subclass checkpoint: unchanged full Pydantic suite cleared plugins (14/14) and reached 3,543 passed, 253 skipped, 22 xfailed before test_private_attributes.py::test_private_attr_set_name failed: deepcopy of an int subclass reconstructed value 0 instead of 1. General object.__reduce_ex__ now includes an int subclass's underlying integer payload in __newobj__ args when no custom __getnewargs__ exists. Core CPython oracle int_subclass_copy and public FastAPI PrivateAttr/TestClient private_int_default_contract match Python 3.14. Exact unchanged failing test passes, and whole unchanged private-attributes module passes 35/35 (scratch/pydantic-private-int-module-20260924.log). Full Release ALL_BUILD and full fixtures pass on latest build; local FastAPI gate is running. Full Pydantic rerun and CTest pending. Matrix last confirmed 4/7; demo stopped; no commit or push.

2026-09-24 private int gate verification: latest full Release ALL_BUILD, core fixtures including int_subclass_copy, local FastAPI gate including private_int_default_contract and real Uvicorn HTTP/WebSocket, and CTest 53/53 all pass (scratch/build-full-int-subclass-20260924.log, scratch/fixtures-int-subclass-20260924.log, scratch/fastapi-local-int-subclass-20260924.log, scratch/ctest-int-subclass-20260924.log). Full unchanged Pydantic suite rerun active at scratch/pydantic-full-int-subclass-20260924.log. Seven-project matrix last confirmed 4/7, demo stopped, no commit or push.

2026-09-24 RootModel checkpoint: unchanged full Pydantic suite passed all 35 private-attribute tests and reached 3,572 passed, 253 skipped, 22 xfailed before test_root_model.py::test_validate_assignment_true found a native pydantic-core assignment error location ('root',) instead of CPython's (). Native root assignment now validates at empty location; exact upstream test and public FastAPI TestClient root_assignment_location_contract match CPython 3.14. Whole root_model module then had 4 further failures (74 passed): 3 RootModel extra-config errors were wrapped as TypeError by type.__new__ even though __init_subclass__ raised PydanticUserError. General type.__new__ now preserves that original exception; core init_subclass_exception oracle matches CPython, and all 3 unchanged extra-config cases pass. The remaining module failure is tagged-union serialization fallback and missing UserWarning in test_mixed_discriminated_union[IModel]; no warning-only workaround applied. Full Release ALL_BUILD and core fixtures pass on latest build; local FastAPI gate running, CTest pending. Full Pydantic remains open, matrix last confirmed 4/7, demo stopped, no commit or push.

2026-09-24 RootModel gate verification: full Release ALL_BUILD, complete core fixture runner including init_subclass_exception, complete 151-case local FastAPI gate including root_assignment_location_contract and real Uvicorn HTTP/WebSocket, and CTest 53/53 all pass on latest build (scratch/build-full-root-assignment-20260924.log, scratch/fixtures-root-assignment-20260924.log, scratch/fastapi-local-root-assignment-20260924.log, scratch/ctest-root-assignment-20260924.log). Whole unchanged root_model module with --maxfail=1 now passes 44 tests before tagged-union serializer fallback warning is missing (scratch/pydantic-root-module-init-subclass-20260924.log); exact test passes on CPython 3.14 (scratch/pydantic-root-union-cpython-20260924.log). Production demo restarted on 127.0.0.1:8765, PID 22712 at verification; /api/runtime reports xlang3, Python 3.14.7, FastAPI, ready. Seven-project matrix remains last confirmed 4/7 exit-zero. Pydantic full suite open; AnyIO/HTTPX cryptography native boundary open. No commit or push.

2026-09-25 tagged-union serializer checkpoint: native pydantic-core now handles a missing or unknown discriminator by serializing choices left to right, with CPython-equivalent combined warning behavior for warnings=True/False/'error'. Direct CPython 3.14 probes cover nested RootModel, matching tag, unknown tag, and absent tag; XLang3 outputs match exactly including three warning details for each raw failure case. Public FastAPI TestClient tagged_union_fallback_contract is registered and matches CPython. Entire unchanged Pydantic tests/test_root_model.py passes 78/78 (scratch/pydantic-root-tagged-warnings-20260925.log). Full Release ALL_BUILD and core fixtures pass; complete local FastAPI gate running. Full Pydantic suite, CTest, matrix and demo restart pending. Last confirmed matrix 4/7, no commit or push.

2026-09-25 tagged-union gate verification: latest full Release ALL_BUILD, full core fixture runner, 152-case local FastAPI gate including new tagged_union_fallback_contract and real Uvicorn HTTP/WebSocket, and CTest 53/53 all pass (scratch/build-full-tagged-fallback-20260925.log, scratch/fixtures-tagged-fallback-20260925.log, scratch/fastapi-local-tagged-fallback-20260925.log, scratch/ctest-tagged-fallback-20260925.log). Direct CPython 3.14 vs XLang3 probes now match for RootModel tagged fallback under warn/none/error and raw typed-dict tagged fallback under matching, unknown, and missing tags, including combined branch-warning text. Whole unchanged RootModel test module 78/78. Full unchanged Pydantic suite running (scratch/pydantic-full-tagged-fallback-20260925.log). Demo stopped for native package relink; matrix last confirmed 4/7; no commit or push.

2026-09-25 serialization and annotation checkpoint: the tagged-union full Pydantic run reached 3,628 passed, 253 skipped, 22 xfailed before parent-typed subclass extra serialization failed. Native pydantic-core now distinguishes an instance of a parent class from an exact parent-class instance and omits subclass extras under parent-typed serialization. Nested wrap serializers now suppress their wrapper only for the immediate handler call, preserving wrappers on nested objects. Serializer debug repr includes return serializers. CPython 3.14 oracle probes and public FastAPI contracts were added for all three. The unchanged Pydantic serialize module advanced through those failures and then exposed a general lazy annotation issue: builtin getattr(function, '__annotations__') returned None before evaluation, so annotationlib FORWARDREF used fake globals and produced a symbolic f-string serializer. Runtime getattr now evaluates function annotations, a first-access CPython oracle matches, and the exact unchanged computed-field serializer test passes. The serialize module now reaches 78 passed, 1 skipped, then fails test_subclass_support_unions_with_forward_ref because two serialized list items are empty dicts (scratch/pydantic-serialize-annotation-getattr-20260925.log). These latest serialization and runtime changes have a passing Release xlang3 target build, but full ALL_BUILD/fixtures/FastAPI/CTest gates have not yet been rerun. Seven-project upstream matrix remains last confirmed 4/7 exit-zero; AnyIO/HTTPX still blocked by cryptography native boundary; production demo stopped; no commit or push.

2026-09-25 forward-union serialization checkpoint: union branch matching now checks list item schemas, so list[Foo] is rejected for Baz instances and the list[Bar] branch serializes only Bar fields. The unchanged upstream test_subclass_support_unions_with_forward_ref passes, including a nested Foo case, and the whole unchanged Pydantic tests/test_serialize.py passes 88 with 1 skip (scratch/pydantic-serialize-union-list-20260925.log). New public FastAPI/TestClient forward_union_serialization_contract also exercises first-access annotationlib FORWARDREF serializer evaluation and matches CPython 3.14 output. Full Release ALL_BUILD, complete core fixtures, 156-case local FastAPI gate including real Uvicorn HTTP/WebSocket, and CTest 53/53 pass (scratch/build-full-union-list-20260925.log, scratch/fixtures-union-list-20260925.log, scratch/fastapi-local-union-list-20260925.log, scratch/ctest-union-list-20260925.log). git diff --check exits 0. A fresh untouched full Pydantic run is active at scratch/pydantic-full-union-list-20260925.log. Seven-project matrix last confirmed 4/7 exit-zero; AnyIO/HTTPX cryptography native boundary open; demo stopped; no commit or push.
2026-09-25 Pydantic types continuation: After the last fully gated 156-case local FastAPI/53-case CTest checkpoint, the untouched full Pydantic suite reached 4,112 passed, 254 skipped, 23 xfailed before `test_conlist`. Native pydantic-core list length error ordering/location now matches direct CPython 3.14 probes and the exact unchanged test passes. Subsequent types-module failures led to general missing-module error semantics and `.name`, bool Decimal/bytes coercion, duration parsing/details, annotationlib fake-global evaluation, modulo TypeError/frozenset naming, enum validator repr, set length singularization, and ValidatorIterator repr/lazy error location fixes. CPython-oracle core fixtures and public FastAPI TestClient contracts were added; the latest unchanged `test_infinite_iterable_int` passes. The complete unchanged `tests/test_types.py` now gets 385 passed, 3 skipped, 1 xfailed before `test_strict_str`, where a string Enum member validates to `FruitEnum.banana` instead of its underlying `banana` value (`scratch/pydantic-types-iterator-location-20260925.log`). Latest targeted Release native package build passes. The post-checkpoint full ALL_BUILD, fixtures, local FastAPI gate, CTest, full Pydantic suite, and seven-project matrix still need reruns; last verified matrix remains 4/7 exit-zero, with Pydantic, AnyIO, and HTTPX open. Demo is stopped for native package relinks. No commit or push.
2026-09-25 Pydantic types module green checkpoint: The complete untouched upstream `tests/test_types.py` now passes 951 tests, skips 4, and xfails 1 (`scratch/pydantic-types-ascii-only-20260925.log`). General fixes include strict string Enum underlying-value coercion, UUID parse-error contexts, Decimal significant-digit/whole-digit constraints and signaling-NaN finite checks plus schema validation, CPython `re.Pattern`/`re.Match` class names, compiled-pattern error contexts, int-subclass division, Json[None] source-mode error wording, binascii.Error's ValueError base, explicit wrap-serializer handler schema behavior, AST-compiled annotation comparisons and lambda qualnames with bytecode cache version 48, structural tuple-subclass hashing for namedtuple Enum values, and ASCII-only string validation. Each exercised by CPython 3.14 oracle and public FastAPI TestClient contracts; unchanged targeted upstream tests pass. Full Release ALL_BUILD passed (`scratch/build-full-types-20260925.log`), full fixture runner passed under explicit exception/final-marker verification (`scratch/fixtures-full-types-verified-20260925.log`), expanded 170-case local FastAPI gate passed including real Uvicorn HTTP/WebSocket (`scratch/fastapi-local-full-types-20260925.log`), and CTest passed 53/53 (`scratch/ctest-full-types-rerun-20260925.log`). One old CTest expected native candidate diagnostics for a wholly absent module; its expectation was updated to the correct Python-facing `No module named ...` error. A new complete untouched Pydantic suite is active at `scratch/pydantic-full-types-green-20260925.log`. Seven-project upstream matrix last confirmed 4/7 exit-zero, AnyIO/HTTPX cryptography native boundary open. Demo stopped for package relinks; no commit or push.

2026-09-25 continuation after types checkpoint: The unchanged full Pydantic run reached **5,103 passed, 258 skipped, 24 xfailed** before a string-Enum hashing failure; builtin str hash inheritance was corrected and the entire payment-card module passed 46/46. Native pydantic-core union serialization now prefers an exact model-class branch for recursive `Self` references, and the exact unchanged upstream test passes 2/2. In the remaining upstream tail, 299 passed and 3 skipped before missing builtin `breakpoint`; implemented builtin forwarding to the current sys.breakpointhook, with CPython 3.14 fixture and exact upstream test green. The next tail reached 335 passed and 3 skipped before arguments-schema `AliasChoices`; native pydantic-core now uses its general validation-alias lookup for argument parameters. Exact upstream alias tests passed 3/3, and a FastAPI/TestClient CPython oracle passes for both choices and missing-argument location. The tail then reached 354 passed, 3 skipped before PEP 695 generic-class decorated-method annotation resolution. Compiler now binds generic class type parameters lexically for class body and method `__annotate__`; a CPython fixture verifies direct class reference, no class-local leakage, and method annotation identity. The unchanged Pydantic decorated-method test still fails on `NameError: T` because XLang3 executes a class body inline at module level, so Pydantic `parent_frame_namespace()` sees no class frame or `__type_params__`. A separate tail triage run deselecting that one test reached 77 passed before `test_int_overflow_validation[nan]`, where nested ValidationError dict equality lacks CPython float-NaN object identity through the native boundary. These are open gaps, not justified skips. Compiler IR codec version is now 49 with matching pyc magic. The latest native package and xlang3 Release target builds passed; full ALL_BUILD/fixtures/FastAPI/CTest and full unmodified Pydantic suite need reruns after these changes. Seven-project matrix last confirmed 4/7 exit-zero; AnyIO/HTTPX cryptography native boundary remains open; demo stopped; no commit or push.

2026-09-25 further validator triage: General float values now carry identity tokens through the X3 native-package boundary; `is`, `id`, hash of NaN, and identity shortcuts in dict/list/tuple equality use them while ordinary `nan == nan` stays false. CPython 3.14 oracle fixture `float_identity_containers` matches, exact unchanged upstream `test_int_overflow_validation` passes 3/3, and a FastAPI/TestClient validation-error comparison route matches CPython. Native model assignment now validates and stores allowed extra fields in `__pydantic_extra__`, updates fields-set, and returns the original model; exact unchanged upstream test passes and public route matches CPython. Invalid model-fields validator results now raise CPython-shaped TypeError on non-dict `__dict__` content; exact unchanged upstream root-validator test and public route pass. The subsequent validator run reached 75 passes, then `test_model_validator_returns_ignore` triggered `TypeError: print_exception(): Exception expected for value, <invalid> found` and hung until timeout. Exact direct reproduction shows the failure only when the Pydantic model-constructor warning is recorded under `warnings.catch_warnings(record=True)` with `warnings.simplefilter('always')`; the same warning emitted without the model and the model warning without that filter both succeed. This is an open general warning/exception-state bug. The earlier PEP 695 generic class decorated-method namespace failure also remains open. Latest xlang3 and native Pydantic Core Release target builds passed; full gates, complete unmodified Pydantic suite, seven-project matrix, and demo restart remain required. No commit or push.

2026-09-25 validator tail correction and checkpoint: The earlier `test_model_validator_returns_ignore` hang was a native pydantic-core ownership error for a borrowed model instance, not a warning subsystem defect; retaining the instance fixed the exact unchanged upstream test. Native literal validation now returns the matching expected object, preserving Enum identity. Assignment `function-before` validators now receive the full model data with the raw assigned value before field validation. Compiler decorator and runtime AST source positions now give CPython-compatible warning lines, and arguments-schema validators receive the current argument field name. Each change has a CPython 3.14 oracle or public FastAPI contract and its exact unchanged upstream target green. The uninterrupted later Pydantic tail `test_validators.py`, `test_validators_dataclass.py`, `test_version.py`, and `test_warnings.py` passed **198 passed, 2 xfailed** (`scratch/pydantic-tail-argument-info-20260925.log`). Latest full Release ALL_BUILD, core fixtures, and local FastAPI gate including real HTTP/WebSocket passed (`scratch/build-full-validator-20260925.log`, `scratch/fixtures-validator-20260925.log`, `scratch/fastapi-local-validator-20260925.log`), but they predate the final argument-field change and must be rerun. The remaining PEP 695 decorated-method test fails because the synthetic class frame reports empty `f_locals` instead of class `__type_params__`; `scratch/generic_frame_probe.py` reproduces this against CPython 3.14. The complete unchanged Pydantic suite and seven-project matrix still need fresh runs; last matrix confirmation is 4/7, with AnyIO/HTTPX blocked by the cryptography native boundary. Demo is stopped for relinks; no commit or push.

2026-09-25 generic-class frame checkpoint: Class lowering now maintains a live class namespace for logical class frames, including `__type_params__`, and the IR codec serializes the logical-frame range and namespace slot (version 52, matching new pyc magic). `locals()` and `sys._getframe(1).f_locals` in a PEP 695 class match CPython 3.14 on `tests/fixtures/core/generic_class_frame_locals.py`, including source and cached-pyc imports. Public `tests/fastapi/generic_class_frame_contract.py` matches CPython under TestClient; the exact unchanged Pydantic `test_pep695_with_class` passes and the complete unchanged `test_validate_call.py` module passes 64/64. The `f_lineno` source-position correction also needed current-instruction semantics for debugger, trace, profile, and DAP pause frames; full core fixtures pass (`scratch/fixtures-trace-frame-20260925.log`) and latest full Release ALL_BUILD plus CTest 53/53 pass (`scratch/build-full-dap-frame-20260925.log`, `scratch/ctest-dap-frame-20260925.log`). The local FastAPI gate is running on this build (`scratch/fastapi-local-dap-frame-20260925.log`). The first new full Pydantic sweep was stopped after appearing stalled at 33%, but the entire unchanged generics module subsequently passed **120 passed, 2 skipped, 4 xfailed in 276.16s** with a longer timeout; the unchanged 1,000-model generics test alone passed in 72.92s versus 0.46s on CPython. A complete unmodified suite rerun with sufficient time is required. Seven-project matrix last confirmed 4/7; cryptography native boundary for AnyIO/HTTPX and demo restart remain open. No commit or push.

2026-09-25 final local gate checkpoint: The complete local FastAPI contract runner now passes on the latest Release build (`scratch/fastapi-local-dap-frame-20260925.log`), including `generic_class_frame_contract` and real Uvicorn HTTP and WebSocket end-to-end tests. Release ALL_BUILD, full core fixtures, and CTest 53/53 also pass on current code. The unchanged Pydantic full suite is live at `scratch/pydantic-full-dap-frame-20260925.log` with a 600-second per-test timeout to accommodate its measured generics cost. This does not yet prove Pydantic or the seven-project upstream matrix green. Cryptography native dependency work, final broad integration/load/soak evidence, and demo restart remain open; no commit or push.

2026-09-25 Pydantic tail checkpoint: The untouched full Pydantic suite reached **5,670 passed, 261 skipped, 26 xfailed** before `tests/types/test_dataclass.py::test_polymorphic_serialization[dataclass1-True-True]` failed (`scratch/pydantic-full-dap-frame-20260925.log`). General native Pydantic Core polymorphic serialization now honors schema config and runtime override for derived Pydantic models and dataclasses; CPython 3.14 and XLang3 outputs match on `scratch/polymorphic_probe.py`, public FastAPI contract `polymorphic_serialization_contract`, and the complete unchanged dataclass/model types modules pass 54/54 (`scratch/pydantic-types-poly-20260925.log`). A subsequent untouched `tests/types` sweep found `Fraction(True)` missing inherited int numeric properties. Bool `real`, `imag`, `numerator`, and `denominator` now match CPython 3.14, with core `bool_numeric_properties` and public FastAPI `bool_fraction_contract`; the unchanged Fraction module passes 77/77 (`scratch/pydantic-fraction-bool-20260925.log`). The next `tests/types` sweep reached 131 passes before `tests/types/test_union.py::test_field_serializer_in_nested_union_called_only_twice` failed: XLang3 called a nested model field serializer once, versus twice in pinned Pydantic Core (`scratch/pydantic-types-all-bool-20260925.log`). Both it and the adjacent tagged-union test reproduce the same 1-versus-2 difference when run unchanged. Upstream Pydantic Core's union serializer performs strict and then lax checking at the top-level union while nested unions inherit the current check level; XLang3 currently selects the structural branch and serializes once. That general serializer behavior is the current open fix. Full Release/fixtures/FastAPI/CTest gates passed before these latest changes and must be rerun. Seven-project matrix last confirmed 4/7; cryptography true-native boundary for AnyIO/HTTPX, fresh full Pydantic suite, final integration/load/soak evidence, and demo restart remain open. No commit or push.

2026-09-25 union checkpoint: Native Pydantic Core union serialization now tracks strict/lax check level through nested unions and retries the selected top-level branch lax after strict failure. Int-subclass serialization follows the level, including bool under an int schema. This matches the pinned upstream behavior for both unchanged nested field-serializer tests; `tests/types/test_union.py` passes **12/12**. The next untouched test exposed a separate validation issue: a union choice raising `PydanticOmit` stopped validation before later choices could succeed. Union validation now continues through remaining choices and propagates omit only when no choice succeeds. Entire unchanged `tests/types` passes **143/143** (`scratch/pydantic-types-all-union-20260925.log`). New public FastAPI TestClient contract `nested_union_retry_contract` matches CPython 3.14 for nested serializer call count (2), JSON output, and mixed `OnErrorOmit` validation. Latest Release ALL_BUILD (`scratch/build-full-union-20260925.log`), full core fixtures (`scratch/fixtures-union-20260925.log`), and CTest **53/53** (`scratch/ctest-union-20260925.log`) pass. Complete local FastAPI gate and fresh unchanged full Pydantic suite are running (`scratch/fastapi-local-union-20260925.log`, `scratch/pydantic-full-union-20260925.log`). Seven-project matrix remains last confirmed 4/7; cryptography native boundary, final integration/load/soak evidence, and demo restart remain open. No commit or push.

2026-09-25 union gate verification: The complete local FastAPI runner passed **183 contracts** (`scratch/fastapi-local-union-20260925.log`), including `nested_union_retry_contract`, real Uvicorn HTTP/HTTPS, and WebSocket. The unchanged full Pydantic suite is still running at `scratch/pydantic-full-union-20260925.log`, beyond 10% at this checkpoint. Its live local process was PID 26916 and tool session 98429; poll that session or check the process and log before restarting it. Full suite result remains unproven. No commit or push.

2026-09-25 full-suite process recovery: The prior untouched Pydantic run reached **22%** without a pytest failure (`scratch/pydantic-full-union-20260925.log`), but its process and tool session were gone when checked at 17:42 UTC, and no pytest summary or exit status exists. Treat that run as interrupted, not passed or failed. A fresh complete run is started under hidden PowerShell PID 26148 with XLang3 child PID 5100 at startup. Its log is `scratch/pydantic-full-union-background-20260925.log`, PID marker `scratch/pydantic-full-union-background-20260925.pid`, and terminal exit record `scratch/pydantic-full-union-background-20260925.result.json`. The worker script is `scratch/run-pydantic-full-union-20260925.ps1`. Before starting another run, inspect the result record and confirm the recorded parent/child process state. Local Release, fixtures, 183 FastAPI contracts, and CTest 53/53 remain green on current code. Seven-project matrix last confirmed 4/7; no commit or push.

2026-09-25 demo/load/soak checkpoint: Production-style `tests/fastapi/demo_website.py` is live on `http://127.0.0.1:8765/` as XLang3 process PID 18756 (`scratch/demo-union-20260925.pid`). Real `/api/runtime` HTTP reports XLang3 3.14.7/FastAPI ready. Concurrent load on this same build completed **1,000 requests, 16 workers, zero errors** in 45.263 seconds (`scratch/demo-union-load-20260925.log`). A 60.914-second soak completed **539 requests, 8 workers, zero errors** (`scratch/demo-union-soak-20260925.log`), and the demo still answered afterward. These latency figures include contention with the full Pydantic suite and are not a standalone benchmark. The untouched background Pydantic run remains live at 22% with PID 5100 and no final result; inspect its result marker before restarting. Cryptography's `_rust` native boundary still prevents untouched AnyIO/HTTPX TLS-test collection; `trustme` uses unchanged cryptography Python layers for X.509 construction, RSA/EC keys, hashing, PEM encoding, and certificate loading. No commit or push.

2026-09-25 demo feature smoke: The live XLang3 FastAPI demo served `/` with HTTP 200 and completed a real POST `/api/tasks`, PATCH toggle, DELETE, and GET verification cycle. The temporary task was removed; the demo remains live. The background untouched Pydantic run was still active at 24% with no reported failure at this checkpoint.

2026-09-25 multipart and import continuation: New public `multipart_upload_contract` exercises real FastAPI/TestClient form and file upload through unchanged `python-multipart`, HTTP bearer authorization, and missing-file 422 behavior. CPython 3.14 and XLang3 outputs match exactly after sorting JSON keys. It is registered in the local runner with the pinned test dependency path; the restarted full local gate is in progress at `scratch/fastapi-local-multipart-20260925.log` (new case passed at the start). A separate `trustme` import with unchanged cryptography 46.0.7 Python files first exposed a general from-package import error: when an existing child module raises during execution, XLang3 suppressed its pending exception and produced a misleading circular-import `ImportError`. CPython oracle `tests/fixtures/core/import_submodule_failure.py` demonstrates that `ValueError` and `ModuleNotFoundError` from child execution must propagate. `Runtime::import_from` now distinguishes a genuinely absent child from a failed existing child and preserves the latter exception. New public FastAPI route contract `import_submodule_failure_contract` has a CPython 3.14 expected output. This runtime change is **not built or verified yet** because the untouched full Pydantic suite and demo still use the current Release executable; build and rerun full gates after those processes release it. The background Pydantic run was active past 40% with no reported failure. No commit or push.

2026-09-25 multipart gate verification: The complete local FastAPI runner passed **184 contracts** including multipart upload, bearer authentication, real Uvicorn HTTP/HTTPS, and WebSocket (`scratch/fastapi-local-multipart-20260925.log`). This gate ran on the pre-import-fix executable; the newly registered `import_submodule_failure_contract` was added while it was already running and is not part of this pass. The untouched full Pydantic suite is still active past 72% with no reported failure. Build and rerun gates once its process and the demo release the executable; no commit or push.

2026-09-25 import and union continuation: The preceding untouched full Pydantic run reached **4,893 passed, 258 skipped, 24 xfailed** (83%) before `tests/test_types.py::test_union_compound_types` exposed that the native schema-match check for a dictionary ignored its key/value schemas. Recursive key/value matching now selects the correct union serializer branch. The exact test and the entire untouched `tests/test_types.py` module pass (**951 passed, 4 skipped, 1 xfailed**), and the public `compound_union_serialization_contract` matches CPython 3.14. General `Runtime::import_from` now preserves errors raised by real child modules and resolves package `__getattr__` before looking for a missing child; the core import oracle and public FastAPI import contract match CPython. Unchanged `trustme` now reaches the actual missing `cryptography.hazmat.bindings._rust.x509` native boundary. The latest full Release build, core fixtures, and CTest (**53/53**) pass. A fresh complete untouched Pydantic run (`scratch/pydantic-full-import-dict-20260925.log`) and complete local FastAPI gate (`scratch/fastapi-local-import-dict-20260925.log`) are running; their final results are pending. The production demo is stopped for rebuild and must be restarted on the latest executable. No commit or push.

2026-09-25 current gate observation: The production demo was restarted on the latest Release executable (PID 26256 at verification); `GET /` returned HTTP 200 and `/api/runtime` returned XLang3 3.14.7, FastAPI, ready. The untouched Pydantic run remains live past 30% with an active XLang3 process; the local FastAPI runner remains live with 84 contracts passed so far. The top matrix table now reflects the last verified 4/7 exit-zero result and current open projects. No commit or push.

2026-09-25 latest local FastAPI gate: `tests/fastapi/run_fastapi_tests.ps1` exited 0 on the Release build after **186 passing contracts**, including the new multipart upload, compound union serialization, and import-failure contracts and real Uvicorn HTTP/HTTPS/WebSocket integration (`scratch/fastapi-local-import-dict-20260925.log`). The untouched full Pydantic suite remains live past 42% without a reported failure; its result is still pending. No commit or push.

2026-09-25 latest demo/load/soak verification: On the same Release build, the demo at `http://127.0.0.1:8765/` completed 1,000 concurrent HTTP requests with 16 workers and zero errors in 43.827 seconds (`scratch/demo-import-dict-load-20260925.log`), then a 60.94-second soak with 542 requests, 8 workers, and zero errors (`scratch/demo-import-dict-soak-20260925.log`). A real HTTP task flow created a task (201), found it in the active list, toggled it to completed, found it in the completed list, deleted it (204), and confirmed zero remaining tasks. `/api/runtime` still returned XLang3 3.14.7/FastAPI ready after the soak. These timings include contention with the unchanged Pydantic suite and are stability evidence, not an isolated latency benchmark. No commit or push.

2026-09-25 full Pydantic upstream result: The fresh, unchanged Pydantic 2.13.5 suite completed with `--assert=rewrite`, `-n 0`, and a recorded **exit code 0: 5,804 passed, 261 skipped, 26 xfailed in 1147.51 seconds** (`scratch/pydantic-full-import-dict-20260925.log`, `scratch/pydantic-full-import-dict-20260925.result.json`). This includes the previous 83% union failure in the complete sequence. The verified upstream matrix is now **5/7 exit-zero** (FastAPI, Starlette, Pydantic, pydantic-core, Uvicorn). AnyIO and HTTPX remain open behind unchanged `trustme` and cryptography's real native `_rust` X.509 backend. The same Release build passes core fixtures, local FastAPI 186/186, CTest 53/53, and the current demo/load/soak verification. No commit or push.

2026-09-25 cryptography native-boundary inventory: The exact cryptography 46.0.7 source archive is in `scratch/cryptography-source/cryptography-46.0.7.tar.gz` and its SHA-256 `e4cfd68c5f3e0bfdad0d38e023239b96a2fe84146481852dffbcca442c245aa5` matches the package-index digest. Its unchanged Python layer imports native `_rust` submodules including `x509`, `openssl`, `asn1`, `ocsp`, `exceptions`, `declarative_asn1`, `pkcs12`, and `pkcs7`; `trustme` reaches the missing `_rust.x509` first. AnyIO's unchanged TLS fixtures use `trustme.CA()`, `issue_cert()`, and SSL-context configuration. HTTPX's unchanged fixtures additionally load and reserialize PEM private keys with password encryption. The XLang3 `_ssl` module already links native OpenSSL and `modules/net/cypher_module.cpp` has existing RSA/PEM routines, which can inform a genuine XLang3 native cryptography backend. No binary CPython extension may be loaded, and no import-only placeholders should be counted as compatibility. This inventory does not close the native boundary. No commit or push.

2026-09-25 native cryptography first implementation: `modules/cryptography` now builds a genuine `cryptography.hazmat.bindings._rust.x3pkg` native package through XLang3's SDK, linked to native OpenSSL without the CPython runtime or ABI. Its `asn1` child implements DER DSS signature encode/decode with arbitrary-size integers and canonical decode validation, plus SPKI public-key BIT STRING extraction. A public FastAPI TestClient contract exercises these operations and exactly matches CPython 3.14 with the unchanged cryptography 46.0.7 wheel Python layer for successful cases and negative integers (`tests/fastapi/cryptography_asn1_contract.py`/`.out`). The dependency is now pinned in `requirements-test.txt` so test installation is reproducible; the CPython `.pyd` is only an oracle, and XLang3 loads its own `.x3pkg`. Full Release ALL_BUILD, core fixtures, and CTest 53/53 pass on this build. The complete local FastAPI runner is in progress with its new ASN.1 contract already passing. The demo restarted on the latest build at `http://127.0.0.1:8765/` (PID 15052 at verification) and reports XLang3/FastAPI ready. This does **not** yet provide `_rust.x509` or `_rust.openssl`; unchanged `trustme` and the AnyIO/HTTPX suites still cannot collect. Some malformed ASN.1 error strings differ from cryptography's Rust binding and remain unsupported. No commit or push.

2026-09-25 ASN.1 differential detail: A deterministic 204-pair roundtrip spanning 0 through 1,024-bit integers produced the same SHA-256 digest `9c0cb7ff3e98e093bef7e826badf7b7a2d546237b611f34668daad0bb76f20fd` on CPython 3.14 and XLang3 (`scratch/cryptography_asn1_roundtrip.py`). Separate probes of malformed DER show the expected exception class but currently different error text and location details; do not claim complete cryptography ASN.1 parity. During the first differential run, a seeded `random.Random(90314).getrandbits(32)` sequence differed between CPython and XLang3; `src/runtime/modules/system/random_module.cpp` currently uses `std::mt19937_64`, so CPython-compatible `_random` state/output is a separate general runtime gap to address after the native TLS path. The deterministic ASN.1 probe avoids relying on that discrepancy. No commit or push.

2026-09-25 native ASN.1 gate result: The complete local FastAPI runner exited 0 with **187 passing contracts**, including `cryptography_asn1_contract` and real Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-cryptography-asn1-20260925.log`). Full Release ALL_BUILD, core fixtures (outer PowerShell exit 0), CTest 53/53, and `git diff --check` pass. The production demo remains live on this build and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. The last complete upstream matrix remains **5/7 exit-zero**; AnyIO/HTTPX still cannot collect because `_rust.x509` and other cryptography native APIs remain unimplemented. No commit or push.

2026-09-25 native ObjectIdentifier continuation: The genuine cryptography `_rust.ObjectIdentifier` class now uses OpenSSL's OID parser and canonicalizer. CPython 3.14 and XLang3 exactly match the direct native oracle for valid and invalid numeric OIDs, equality with OID/non-OID values, set and dictionary behavior, and deepcopy identity (`scratch/cryptography_oid_core.py`). The class reports the same `__module__` and `__name__`. The new public FastAPI route `cryptography_oid_contract` matches the CPython expected response and is registered in the local runner. The class's `_name` and `repr` delegate to unchanged `cryptography.hazmat._oid` and currently reach the missing native `_rust.openssl` hash module; no native copy of its pure-Python OID-name table was added. Raw numerical hash values differ from cryptography's Rust hasher, while equal OIDs hash equally and work as dictionary keys. Full Release ALL_BUILD passed; core fixtures, CTest, and complete local FastAPI gate are running. Demo restarted on this build at `http://127.0.0.1:8765/` (PID 11804 at verification). `_rust.x509` remains unimplemented; AnyIO/HTTPX are still open. No commit or push.

2026-09-25 OID gate result: Full Release ALL_BUILD, core fixtures, CTest **53/53**, and the complete local FastAPI runner **188/188** exited 0 on the OID build (`scratch/build-full-cryptography-oid-20260925.log`, `scratch/fixtures-cryptography-oid-20260925.log`, `scratch/ctest-cryptography-oid-20260925.log`, `scratch/fastapi-local-cryptography-oid-20260925.log`). The demo on this build still reports XLang3 3.14.7/FastAPI ready after the gate, and `git diff --check` passes. CPython 3.14 hash behavior for SHA-256, SHA-512, SHAKE128, copy/finalize, and XOF squeezing is captured in `scratch/cryptography_hash_oracle.py` for the next genuine `_rust.openssl.hashes` implementation. The seven-project upstream matrix remains **5/7 exit-zero**; `trustme`/AnyIO/HTTPX remain blocked by the unimplemented native X.509/OpenSSL surface. No commit or push.

2026-09-25 native hash continuation: `modules/cryptography` now exports genuine `_rust.openssl.hashes.Hash` and `XOFHash` classes backed by bundled OpenSSL 3.3.2, plus hash support and native OpenSSL version/FIPS queries. It also exports a native `_rust.exceptions._Reasons` type so unchanged `cryptography.exceptions` loads and native contexts can raise its unchanged `AlreadyFinalized` exception. The unchanged `cryptography.hazmat.primitives.hashes` Python layer imports and the direct CPython 3.14 hash oracle matches XLang3 exactly for SHA-256, SHA-512, SHAKE128, context copy/finalize, XOF squeezing/copy, unsupported input type, post-finalize update, post-squeeze update, and squeeze limit errors (`scratch/cryptography_hash_oracle.py`). The unchanged `cryptography.hazmat._oid` mapping now imports, so native ObjectIdentifier `_name`/`repr` match CPython (`scratch/cryptography_oid_oracle.py`); the public OID contract now covers these properties. A new public `cryptography_hash_contract` matches CPython via FastAPI/TestClient and covers the native enum reason representation too. Full Release ALL_BUILD passed, core fixtures/CTest/complete local FastAPI gate are running with all three cryptography contracts already passed. Demo restarted on this build at `http://127.0.0.1:8765/` (PID 6664 at verification), `git diff --check` passes. `trustme` still stops at the missing `_rust.x509`; the upstream matrix remains **5/7 exit-zero** until AnyIO and HTTPX can collect and pass. No commit or push.

2026-09-25 native hash gate result: Full Release ALL_BUILD, core fixture runner, CTest **53/53**, and complete local FastAPI gate **189/189** exited zero (`scratch/build-full-cryptography-hashes-20260925.log`, `scratch/fixtures-cryptography-hashes-20260925.log`, `scratch/ctest-cryptography-hashes-20260925.log`, `scratch/fastapi-local-cryptography-hashes-20260925.log`). The latter includes the three real cryptography contracts and Uvicorn HTTP/HTTPS/WebSocket. The demo on this build reported XLang3 3.14.7/FastAPI ready. The matrix remains **5/7 exit-zero**; `_rust.x509` is still the collection blocker for AnyIO/HTTPX. No commit or push.

2026-09-25 general `_random` continuation: `src/runtime/modules/system/random_module.cpp` now uses CPython-compatible MT19937 array seeding, 32-bit output/`getrandbits` packing, 53-bit `random()` construction, and 625-entry state export/import. The CPython 3.14 oracle fixture `random_mt19937_state` matches XLang3 exactly for zero, positive and negative integers, a >128-bit integer, and string/bytes seeds through unchanged Python 3.14 `random.py`, including state restoration. `random_state_contract` exercises seeded generation and restored state through a real FastAPI/TestClient route and matches the CPython response. This does not yet establish parity for every direct `_random.Random.seed` object type, especially floats/non-integers; the older fallback seed hashing remains for those. Full Release ALL_BUILD, core fixtures including the new case, CTest **53/53**, and `git diff --check` pass. Complete local FastAPI gate is running (`scratch/fastapi-local-random-mt-20260925.log`). Demo restarted on current Release at `http://127.0.0.1:8765/` (PID 21988 at verification), `/api/runtime` reports xlang3 Python 3.14.7/FastAPI ready. Matrix remains **5/7 exit-zero**; no commit or push.

2026-09-25 `_random` gate result: The complete local FastAPI runner exited zero with **190/190** passing contracts, including `random_state_contract` and real Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-random-mt-20260925.log`). Full Release ALL_BUILD, core fixtures including `random_mt19937_state`, CTest **53/53**, and `git diff --check` also pass on this build. The production demo at `http://127.0.0.1:8765/` served its page, created a task, listed it as active, toggled it, listed it as completed, deleted it, and reported zero remaining tasks; `/api/runtime` still reports XLang3 3.14.7/FastAPI ready. The upstream matrix remains **5/7 exit-zero** pending genuine native cryptography X.509/key support and unchanged AnyIO/HTTPX collection. No commit or push.

2026-09-25 native HMAC continuation: `modules/cryptography/hmac_module.cpp` now implements the unchanged cryptography 46.0.7 `_rust.openssl.hmac.HMAC` dependency on bundled OpenSSL EVP_MAC. It has real buffer-key initialization, streamed update, copy, finalize, constant-time verification, `algorithm` property, and native context lifetime management; it raises unchanged Python `AlreadyFinalized` and `InvalidSignature` classes. `scratch/cryptography_hmac_oracle.py` produces exact CPython 3.14/XLang3 output for SHA-1/SHA-256/SHA-512/SHA3-256, a 12-KiB streamed payload, valid/invalid signatures, copied state, finalized errors, and key/signature type errors. `tests/fastapi/cryptography_hmac_contract.py` uses the unchanged HMAC Python API for a real TestClient authentication route and matches CPython's 200 and 401 responses. Full Release ALL_BUILD, core fixtures, and CTest **53/53** pass. Windows `dumpbin /dependents` shows the XLang3 native package depends on system DLLs (WS2_32, CRYPT32, KERNEL32, USER32, ADVAPI32), with no Python DLL. The complete local FastAPI gate is running (`scratch/fastapi-local-cryptography-hmac-20260925.log`), with the new HMAC contract already passed. Demo restarted at `http://127.0.0.1:8765/` (PID 21136), reporting XLang3 3.14.7/FastAPI ready. `trustme` still reaches missing real `_rust.x509`, so upstream matrix remains **5/7 exit-zero**. No commit or push.

2026-09-25 native HMAC gate result: The complete local FastAPI runner exited zero with **191/191** contracts, including the new HMAC authentication route and real Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-cryptography-hmac-20260925.log`). Full Release ALL_BUILD, core fixtures, CTest **53/53**, and `git diff --check` pass on this build. The running demo still reports XLang3 3.14.7/FastAPI ready. The verified upstream matrix remains **5/7 exit-zero** because unchanged `trustme` still fails specifically at missing native `_rust.x509`; no import placeholder was added. No commit or push.

2026-09-25 native RSA continuation: `modules/cryptography/rsa_module.cpp` adds genuine OpenSSL-backed `_rust.openssl.rsa` key generation and key objects, public/private number classes and reconstruction, PEM/DER public and private key export for supported no-encryption formats, PKCS#1 v1.5 and PSS signing/verification, and PKCS#1 v1.5 and OAEP encryption/decryption. Unchanged `cryptography.hazmat.primitives.asymmetric.rsa` now imports and its generated key, number, serialization, signing, and encryption paths run on XLang3. The direct CPython 3.14 oracle `scratch/cryptography_rsa_oracle.py` has matching stable output, and `tests/fastapi/cryptography_rsa_contract.py` produces the same real TestClient response on both runtimes. A separate cross-runtime check exported an XLang3-generated private/public PEM pair; CPython cryptography loaded both and completed sign/verify with them. This proves interoperable key material, not just matching PEM headers. Full Release ALL_BUILD, core fixtures, and CTest **53/53** pass. Complete local FastAPI gate is running (`scratch/fastapi-local-cryptography-rsa-20260925.log`), with the new RSA contract passed. The current demo at `http://127.0.0.1:8765/` (PID 9124) reports XLang3 3.14.7/FastAPI ready. `trustme` still stops at missing native `_rust.x509`; importing unchanged `serialization` separately now reaches missing genuine `_rust.openssl.keys` load methods, followed by DH parameters. Encrypted private-key serialization, key loading, Prehashed signatures, and some RSA edge/error semantics are still open; no placeholders claim them. Upstream matrix remains **5/7 exit-zero**. No commit or push.

2026-09-25 RSA gate result: The complete local FastAPI runner exited zero with **192/192** contracts, including the genuine RSA route and Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-cryptography-rsa-20260925.log`). Full Release ALL_BUILD, core fixtures, and CTest **53/53** passed. This result predates the subsequent key-loader addition. No commit or push.

2026-09-25 native key-loader continuation: The `_rust.openssl.keys` child now uses OpenSSL to load PEM and DER RSA private/public keys, including password-protected PKCS#8 PEM; RSA private-key export now supports `BestAvailableEncryption` in PEM TraditionalOpenSSL and PEM/DER PKCS8. The direct unchanged native API comparison `scratch/cryptography_keys_oracle.py` matches CPython 3.14 for the four PEM/DER loaders, encrypted PEM roundtrip, and signing after load. XLang3-generated password-protected PKCS8 PEM was independently loaded by CPython cryptography and used to sign a message verified with the XLang3-generated public key. The public FastAPI RSA contract now covers these real load/export paths and matches CPython's response. Full Release ALL_BUILD, core fixtures, CTest **53/53**, and `git diff --check` pass on the current build. The complete local FastAPI gate is running (`scratch/fastapi-local-cryptography-keys-20260925.log`) with its expanded RSA contract already passed. Demo restarted on this build at `http://127.0.0.1:8765/` (PID 2740), reporting XLang3 3.14.7/FastAPI ready. Unchanged `serialization` import now advances past `keys` to the missing real `_rust.openssl.dh` parameter loader; `trustme` still reaches missing `_rust.x509` first. EC, DH, X.509, additional key types and exact error parity remain open. Upstream matrix remains **5/7 exit-zero**. No commit or push.

2026-09-25 native key-loader gate result: The complete local FastAPI runner exited zero with **192/192** contracts, including expanded RSA encrypted/unencrypted key-loading coverage and real Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-cryptography-keys-20260925.log`). Full Release ALL_BUILD, core fixtures, CTest **53/53**, and `git diff --check` pass on the same build. `/api/runtime` on the running demo reports XLang3 3.14.7/FastAPI ready. Windows `dumpbin /dependents` still shows no Python DLL dependency for the native cryptography package. The upstream matrix remains **5/7 exit-zero** pending genuine DH/EC/X.509 and the rest of the cryptography native API needed by unmodified `trustme`. No commit or push.

2026-09-25 module-subclass/EC continuation: A first OpenSSL-backed native EC package (`modules/cryptography/ec_module.cpp`) compiles and exposes genuine P-256 key generation, key/number objects, serialization, and reconstruction, but has not yet passed a CPython oracle or complete cryptography API coverage. Unchanged `cryptography.hazmat.primitives.asymmetric.ec` import exposed a general `types.ModuleType` subclass bug: `module.__new__` incorrectly validated the subclass's constructor argument and class construction skipped its `__init__`; module subclass methods and `__getattr__` also failed to bind/forward. The runtime now allocates a blank module in `__new__`, invokes the subclass initializer, binds class methods, and uses class `__getattr__` on missing attributes. `module_subclass_constructor` exactly matches the CPython 3.14 oracle; a real `module_subclass_contract` FastAPI/TestClient route returns the same response on both runtimes. Full Release ALL_BUILD, core fixtures, and CTest **53/53** pass. The expanded 193-case local FastAPI runner is active at `scratch/fastapi-local-module-subclass-193-20260925.log`; the new module-subclass route has passed directly, while the full runner remains in progress. The production demo is stopped for relinking. The verified upstream matrix remains **5/7**; AnyIO and HTTPX remain blocked by genuine cryptography X.509 and related native APIs. No commit or push.

2026-09-25 EC oracle checkpoint: The first EC implementation now matches CPython 3.14's unchanged cryptography Python layer for deterministic P-256 scalar 7, exact public x/y coordinates, public/private number reconstruction, PKCS8 DER private key, SubjectPublicKeyInfo DER public key, compressed X9.62 point, and decoded-point reconstruction (`scratch/cryptography_ec_oracle.py`). A `cryptography_ec_contract` FastAPI/TestClient route exercises the same real native dependency and matches the CPython response. The import revealed that a module subclass's `__getattr__` can raise an expected `AttributeError` during import metadata probes; XLang3 left it pending, then incorrectly raised it on the next expression. Module attribute lookup now clears only that expected `AttributeError` while retaining other exceptions, preserves the attribute error message, and invokes the fallback once. The CPython-oracle fixtures `module_subclass_import_probe`, updated `module_subclass_constructor`, and existing `module_getattr_call` pass. Native EC coordinates also exposed that `bin`, `oct`, and `hex` rejected XLang3 big integers; all three now format arbitrary-size positive and negative integers, and `bigint_base_format` matches the CPython 3.14 oracle. Full Release ALL_BUILD, complete core fixture runner, CTest **53/53**, and `git diff --check` pass. The expanded 194-case local FastAPI runner passed all **194/194** contracts, including real Uvicorn HTTP/WebSocket (`scratch/fastapi-local-ec-module-getattr-20260925.log`). `dumpbin /dependents` for the native cryptography package lists system DLLs only, with no Python DLL. The production demo has been restarted on this Release build at `http://127.0.0.1:8765/` (PID 23256); `/` returns 200 and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. Full EC signing/exchange, DH, X.509, and other native cryptography APIs remain open; unchanged `trustme` still stops at `_rust.x509`, so the upstream matrix remains **5/7 exit-zero**. No commit or push.

2026-09-25 initial native X.509 reader: `modules/cryptography/x509_module.cpp` now uses OpenSSL to parse real PEM and strict DER certificates, including PEM bundles and certificates preceded by unrelated text and exposes a native `Certificate` object with PEM/DER serialization, serial number, and digest fingerprint. The direct CPython 3.14 comparison `scratch/cryptography_x509_reader_oracle.py` matches for a CPython-generated RSA self-signed certificate: both runtimes report the same serial, exact DER/PEM roundtrips, and exact SHA-256 fingerprint, and 1/2-certificate PEM bundle counts. `cryptography_x509_reader_contract` exercises this native path through a real FastAPI/TestClient route and matches CPython. The native package target builds, and `dumpbin /dependents` still shows no Python DLL. Invalid DER currently raises ValueError on both runtimes but has different diagnostic text; that parity remains open. This reader is deliberately incomplete: unchanged `trustme` now advances from missing `_rust.x509` to missing real `x509.Sct`, and X.509 creation, extensions, verification, CRL/CSR, and other APIs remain unimplemented. The new route is registered as case 195. Full Release ALL_BUILD, complete fixtures, and CTest **53/53** pass; the 195-case local FastAPI gate is running (`scratch/fixtures-x509-reader-20260925.log`, `scratch/ctest-x509-reader-20260925.log`, `scratch/fastapi-local-x509-reader-20260925.log`). Its X.509 route passed before the PEM-bundle extension was added; the expanded bundle route and direct CPython oracle passed separately afterward, so a final complete gate on the final source remains required. No commit or push.

2026-09-25 initial native DH parameters: `modules/cryptography/dh_module.cpp` adds OpenSSL-backed PEM/DER DH parameter loaders, PKCS3 PEM/DER serialization, parameter-number construction/reconstruction, real parameter generation, and `DH_check` validation. The fixed 512-bit sample in `scratch/dh-sample.pem` produces identical p/g/q, PEM roundtrip, DER SHA-256, DER loader, and number reconstruction under CPython 3.14 and XLang3 (`scratch/cryptography_dh_parameters_oracle.py`). A public `cryptography_dh_parameters_contract` FastAPI/TestClient route matches CPython, including invalid-modulus/generator/parameter errors. The native package target builds. Unchanged `cryptography.hazmat.primitives.serialization` now advances past `_rust.openssl.dh` and stops at missing genuine `_rust.openssl.dsa`; unchanged `trustme` still stops earlier at missing real `x509.Sct`. DH private/public keys and exchange are not yet implemented, so native DH remains partial. The DH route is registered for the next complete local gate. No commit or push.

2026-09-25 DH gate continuation: Full Release ALL_BUILD, the complete core fixture runner, CTest **53/53**, and `git diff --check` exit zero after the final DH parameter change (`scratch/fastapi-release-dh-20260925.log`, `scratch/fastapi-fixtures-dh-20260925.log`). The prior **195/195** local FastAPI gate for the X.509 reader also exited zero; the new DH route extends the next gate to **196** cases. That complete 196-case gate is running in `scratch/fastapi-local-dh-20260925.log`. The production demo at `http://127.0.0.1:8765/api/runtime` responds with XLang3 3.14.7/FastAPI ready. Upstream matrix remains **5/7 exit-zero**; AnyIO/HTTPX collection and full cryptography support remain open. No commit or push.

2026-09-25 native DH key continuation: The real OpenSSL-backed DH module now has private/public key classes, parameter-based private key generation, public-key extraction, parameter and key-size accessors, and padded shared-secret exchange with peer-parameter/public-key checks and cleansing of the temporary secret. `scratch/cryptography_dh_exchange_oracle.py` matches CPython 3.14: 512-bit keys, a 64-byte equal shared secret, and matching parameter roundtrips. The public `cryptography_dh_parameters_contract` FastAPI route was expanded to exercise both-party exchange, key sizes, and parameters and matches its updated CPython `.out` file exactly. The native target and full Release ALL_BUILD compile, core fixtures exit zero, CTest **53/53** exits zero, `git diff --check` exits zero, and the native package depends only on system DLLs, not Python. The earlier 196-case gate was interrupted after 108 cases because it had already run the older DH route before the expansion; a fresh full gate on final sources is running in `scratch/fastapi-local-dh-keys-20260925.log`. DH private/public number objects and key serialization remain unimplemented. Upstream matrix remains **5/7 exit-zero** due broader cryptography support. No commit or push.

2026-09-25 native DH number continuation: `DHPrivateNumbers` and `DHPublicNumbers` now hold actual Python integer/key-number components, extract them from OpenSSL DH keys, and reconstruct validated private/public keys for real exchange. This enables unchanged `cryptography.hazmat.primitives.asymmetric.dh` to import. `scratch/cryptography_dh_numbers_oracle.py` matches CPython 3.14 for type names, component values, parameter links, number reconstruction, and matching secrets; `scratch/cryptography_dh_numbers_invalid_probe.py` matches CPython's constructor TypeErrors for string components and a non-parameter object. The public DH FastAPI route now covers reconstruction and these invalid inputs; its output matches CPython exactly. Native target and full Release ALL_BUILD compile, full core fixtures exit zero (`scratch/fastapi-fixtures-dh-numbers-20260925.log`), CTest **53/53** exits zero (`scratch/fastapi-ctest-dh-numbers-20260925.log`), and `git diff --check` passes. A 196-case gate using the prior route was interrupted after 85 cases because it did not test the final route; the fresh complete gate is running in `scratch/fastapi-local-dh-numbers-20260925.log`, and the expanded DH case has passed within it. DH key serialization is still absent; unchanged `serialization` still needs native DSA and broader cryptography APIs. Matrix still **5/7**. No commit or push.

2026-09-25 initial native DSA parameters and general bigint formatting: `modules/cryptography/dsa_module.cpp` adds real OpenSSL DSA 1024/2048/3072/4096-bit parameter generation, p/q/g extraction, numeric reconstruction with OpenSSL parameter validation, and CPython-compatible argument/invalid-parameter errors. `scratch/cryptography_dsa_parameters_oracle.py` and its invalid probe match CPython 3.14 using the fixed `scratch/dsa-sample.json`. A public `cryptography_dsa_parameters_contract` route matches CPython, including fresh generated parameters and invalid inputs. Its first XLang3 run exposed a general `int.__format__` gap for big integers; `src/builtins/functional_builtins.cpp` now formats arbitrary-size ints in decimal/binary/octal/hex with sign, prefixes, grouping, and padding. The expanded `bigint_base_format` CPython oracle fixture matches exactly. Full Release ALL_BUILD, full fixtures, CTest **53/53**, and `git diff --check` pass (`scratch/fastapi-release-dsa-bigint-format-20260925.log`, `scratch/fastapi-fixtures-dsa-bigint-20260925.log`, `scratch/fastapi-ctest-dsa-bigint-20260925.log`). The native package links system DLLs only, no Python DLL. Demo restarted on this Release at `http://127.0.0.1:8765/` (PID 21864) and reports XLang3 3.14.7/FastAPI ready. A fresh **197-case** local FastAPI gate runs in `scratch/fastapi-local-dsa-bigint-20260925.log`; both DH and DSA routes have passed within it. The prior 196-case DH gate was interrupted after about 100 cases because the runtime was rebuilt. Unchanged `dsa.py` now advances beyond DSA parameters and stops at missing native `DSAPrivateKey`; genuine DSA key types, signing, verification, serialization, and broader X.509 support remain open. Upstream matrix remains **5/7**. No commit or push.

2026-09-25 native DSA key continuation: The OpenSSL DSA backend now generates private/public keys from genuine parameters, extracts and reconstructs `DSAPrivateNumbers`/`DSAPublicNumbers`, exposes key sizes and parameters, and signs/verifies DER DSA signatures with SHA-256 and `Prehashed` digests. Verification rejects tampering with the unchanged Python `InvalidSignature` class; private-key reconstruction checks the public number with a constant-time modular exponentiation and clears private BIGNUMs on failure. `scratch/cryptography_dsa_keys_oracle.py` and `scratch/cryptography_dsa_sign_oracle.py` match CPython 3.14, and the public DSA FastAPI route now covers key reconstruction, signing, prehashed verification, and invalid signatures with exact CPython output. Unchanged `cryptography.hazmat.primitives.asymmetric.dsa` now imports on XLang3; unchanged `serialization` has advanced to missing native `ed25519`. Full Release ALL_BUILD, full fixture runner, CTest **53/53**, and `git diff --check` pass (`scratch/fastapi-release-dsa-keys-20260925.log`, `scratch/fastapi-fixtures-dsa-keys-20260925.log`, `scratch/fastapi-ctest-dsa-keys-20260925.log`). The native DLL still has no Python DLL dependency. Demo at `http://127.0.0.1:8765/` reports XLang3 3.14.7/FastAPI ready. The previous 197-case gate passed 135 cases before deliberate interruption because it had already executed the older DSA route. A fresh complete **197-case** gate runs in `scratch/fastapi-local-dsa-keys-20260925.log`. DSA key serialization and some other methods remain open; matrix is still **5/7**. No commit or push.

2026-09-25 native Ed25519 and cipher continuation: `modules/cryptography/ed25519_module.cpp` implements real OpenSSL Ed25519 key creation/import, raw and PEM/DER export, encrypted PKCS8, signing/verification, copying, and unchanged cryptography `InvalidSignature`. Fixed-seed low-level CPython 3.14 oracles and a public FastAPI/TestClient route match exactly. Ed25519 Release ALL_BUILD, complete fixtures, and CTest **53/53** passed (`scratch/fastapi-release-ed25519-20260925.log`, `scratch/fastapi-fixtures-ed25519-20260925.log`, `scratch/fastapi-ctest-ed25519-20260925.log`). Its full 198-case local gate was intentionally interrupted after the new Ed25519 case passed because the following cipher source changed the revision; do not count that gate as complete.

`modules/cryptography/ciphers_module.cpp` now implements genuine OpenSSL EVP cipher contexts for AES CBC/CTR/ECB/OFB/CFB/CFB8/GCM/XTS and available Camellia variants, including streaming update/update_into/finalize, AES-GCM AAD/tag authentication and rejection, and CTR reset_nonce. Unchanged `cryptography.hazmat.primitives.ciphers.Cipher` and `serialization` import on XLang3. `scratch/cryptography_ciphers_oracle.py` matches CPython 3.14 for AES CBC/CTR/ECB/GCM encryption and round trips, invalid GCM tag, finalized/tag state exceptions, update_into, and CTR nonce reset. New public `cryptography_ciphers_contract` FastAPI/TestClient route also matches CPython exactly. Full Release ALL_BUILD and `git diff --check` pass; `dumpbin /dependents` shows only system DLLs, no Python DLL. New complete fixtures, CTest, and **199-case** local FastAPI gate run in `scratch/fastapi-fixtures-ciphers-20260925.log`, `scratch/fastapi-ctest-ciphers-20260925.log`, and `scratch/fastapi-local-ciphers-20260925.log` respectively; await exit status before claiming success. Demo restarted at `http://127.0.0.1:8765/` (PID 5960) and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. `trustme` unchanged import still stops at missing native `cryptography.hazmat.bindings._rust.x509.Sct`, so upstream AnyIO/HTTPX collection is still blocked and matrix remains **5/7**. X.509 certificate-transparency must be real, not a placeholder. High-level Ed25519 `.from_private_bytes` still reaches missing `_rust._openssl` binding; low-level native Ed25519 works. No commit or push.

2026-09-25 cipher regression checkpoint: The cipher-source Release ALL_BUILD, full fixtures, and CTest **53/53** all exit zero (`scratch/fastapi-release-ciphers-20260925.log`, `scratch/fastapi-fixtures-ciphers-20260925.log`, `scratch/fastapi-ctest-ciphers-20260925.log`). The public AES/GCM FastAPI route output exactly matches CPython; the direct oracle also confirms GCM `finalize_with_tag` and CTR `reset_nonce`. The complete 199-case local FastAPI gate is still running in `scratch/fastapi-local-ciphers-20260925.log`. X.509 inspection shows `trustme` depends on unchanged `cryptography.x509`, which expects real native SCT, verification, certificate/CSR/CRL, and certificate builder/extension functions. This is the next broad native boundary, not an import-only fix.

The same unchanged cipher oracle additionally matches CPython 3.14 for AES OFB, CFB, CFB8, XTS and Camellia CBC ciphertext plus decrypt round trips, with no changes to the code under the running 199-case gate.

Live demo load on this build: `py -3.14 tests/fastapi/load_demo_website.py --base http://127.0.0.1:8765 --requests 1000 --workers 16` exits zero, reports 1,000 requests and zero errors (`scratch/fastapi-demo-load-ciphers-20260925.log`). It overlapped the full local gate, so latency/throughput numbers include test contention and are not a standalone benchmark.

2026-09-25 completed cipher gate: The complete **199/199** local FastAPI runner exits zero on the cipher build, including the new Ed25519 and cipher routes plus real Uvicorn HTTP/HTTPS and WebSocket (`scratch/fastapi-local-ciphers-20260925.log`). Full Release ALL_BUILD, all core fixtures, CTest **53/53**, and `git diff --check` pass on this revision. Live demo load: 1,000 requests, 16 workers, zero errors (`scratch/fastapi-demo-load-ciphers-20260925.log`); an independent 30-second soak is running. Demo `/api/runtime` reports XLang3 3.14.7/FastAPI ready. Upstream 7-project matrix remains **5/7** because unchanged `trustme` still fails to import missing genuine `x509.Sct` and related broad X.509 APIs; AnyIO/HTTPX not counted as passing. No commit or push.

The independent 30-second live demo soak also exits zero: 324 requests, 16 workers, zero errors (`scratch/fastapi-demo-soak-ciphers-20260925.log`). The demo remains live at `http://127.0.0.1:8765/`, and `/api/runtime` still reports XLang3 3.14.7/FastAPI ready after the soak. `git diff --check` passes. The FastAPI goal remains active because the unmodified AnyIO/HTTPX suites still cannot collect through the incomplete native cryptography X.509 boundary.

2026-09-25 native X.509 document continuation: `modules/cryptography/x509_module.cpp` now provides real OpenSSL-backed `CertificateSigningRequest` and `CertificateRevocationList` types, PEM/DER CSR and CRL loaders, PEM/DER public serialization, CSR signature verification, and CRL revoked-entry count (including empty lists). The fixed tracked PEM fixtures `tests/fastapi/x509_csr_sample.pem`, `x509_crl_sample.pem`, and `x509_crl_empty_sample.pem` were generated with a deterministic Ed25519 key by CPython cryptography. Direct native `scratch/cryptography_x509_csr_crl_oracle.py` matches CPython 3.14 for exact DER SHA-256, round trips, valid/tampered CSR signatures, one-entry and empty CRL counts. Public `cryptography_x509_documents_contract` matches CPython through FastAPI/TestClient. Full Release ALL_BUILD and `git diff --check` pass; native DLL has system DLL dependencies only, no Python DLL. New full fixture, CTest, and **200-case** local FastAPI gates are running in `scratch/fastapi-fixtures-x509-documents-20260925.log`, `scratch/fastapi-ctest-x509-documents-20260925.log`, and `scratch/fastapi-local-x509-documents-20260925.log`; await exit status. Demo at `http://127.0.0.1:8765/` remains live and reports XLang3 3.14.7/FastAPI ready. The upstream matrix is still **5/7**, because unchanged `trustme` requires broader genuine X.509 API, beginning with `Sct` and verification types. Do not count this CSR/CRL slice as unblocking upstream collection. No commit or push.

2026-09-25 X.509 CSR/CRL expanded checkpoint: The same native types now expose actual CSR/CRL signature bytes, DER signed-body properties, and CRL SHA-256 fingerprint, all matching CPython 3.14 exact hashes. Four malformed/trailing DER cases raise `ValueError` on both runtimes. The public `cryptography_x509_documents_contract` route includes these and matches CPython. The earlier 200-case gate was intentionally stopped after 13 cases when this source changed; it does not count as complete. The final Release ALL_BUILD exits zero (`scratch/fastapi-release-x509-documents-final-20260925.log`); final full fixtures, CTest, and 200-case local gate are running in the corresponding `*-x509-documents-final-20260925.log` files. No X.509 import-only symbols or FastAPI-specific code were introduced. Upstream matrix remains 5/7 pending real certificate-transparency, verification, and builder APIs.

Next X.509 evidence: `scratch/probe_sct_certificate.py` generates a real Ed25519-signed certificate with a syntactically valid embedded SCT extension. Unmodified CPython cryptography 46.0.7 parses its SCT and reports the fixed log ID `000102...1f`, `2025-01-01 00:00:00`, `Version.v1`, `LogEntryType.PRE_CERTIFICATE`, SHA-256, ECDSA, and exact signature bytes. This is a concrete oracle for the next native `Sct`/certificate-extension slice. No import-only `Sct` class has been added.

2026-09-25 completed native X.509 document gate: The final Release ALL_BUILD, complete core fixtures, CTest **53/53**, and complete local FastAPI **200/200** gate all exit zero (`scratch/fastapi-release-x509-documents-final-20260925.log`, `scratch/fastapi-fixtures-x509-documents-final-20260925.log`, `scratch/fastapi-ctest-x509-documents-final-20260925.log`, `scratch/fastapi-local-x509-documents-final-20260925.log`). The local gate includes the new real CSR/CRL route, real Uvicorn HTTP/HTTPS, and WebSocket. `git diff --check` passes. The live demo remains available at `http://127.0.0.1:8765/` and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. Exact CPython-oracle checks additionally cover CSR/CRL signature bytes, signed bodies, fingerprint, malformed DER, and empty CRL. Native DLL has no Python DLL dependency. Unmodified `trustme` still fails at missing native `_rust.x509.Sct`; upstream matrix remains **5/7**, so the goal is active. No commit or push.

2026-09-25 native X.509 SCT and trust-store continuation: `modules/cryptography/x509_module.cpp` now decodes real OpenSSL certificate-transparency SCT lists from a certificate extension into native `Sct` objects, with genuine log ID, timestamp (UTC, naive datetime), signature, extension bytes, and SHA-256 signature hash algorithm. The fixed `tests/fastapi/x509_sct_sample.pem` is a real Ed25519-signed certificate with a syntactically valid embedded SCT; this checks parsing, not CT log signature verification. The private native `Certificate._signed_certificate_timestamps()` accessor is an intermediate backend path until unchanged high-level `Certificate.extensions` is implemented. `scratch/probe_sct_certificate.py` generates the fixture and CPython high-level oracle; public `cryptography_x509_sct_contract` returns the same SCT fields under XLang3 FastAPI/TestClient. Native `Store` now owns a real OpenSSL `X509_STORE` and retains trusted certificates, including duplicate anchors; CPython and XLang3 match for nonempty, duplicate, empty, and invalid inputs, with public `cryptography_x509_store_contract` matching CPython exactly. Unchanged `trustme` has advanced from missing `_rust.x509.Sct` past `Store` and currently stops at missing `VerifiedClient`; this is still broader native verification work, not a completed upstream matrix. Full Release ALL_BUILD and `git diff --check` pass, system DLLs only/no Python DLL. Full fixtures, CTest, and **202-case** local gate are running in `scratch/fastapi-*-x509-sct-store-20260925.log`; await exit status. Demo at `http://127.0.0.1:8765/` remains live, XLang3 3.14.7/FastAPI ready. Upstream matrix remains **5/7**. No commit or push.

Verification oracle prepared while the SCT/store gate runs: `scratch/generate_x509_verifier_samples.py` creates deterministic P-256 root and leaf certificates in `tests/fastapi/x509_verify_root.pem` and `x509_verify_leaf.pem` using fixed private scalars and deterministic ECDSA/SHA-256 signing. Unmodified CPython cryptography `PolicyBuilder`/`Store`/`ServerVerifier` at 2025-07-01 verifies the leaf to serial chain `[1002, 1001]`, rejects `other.example`, accepts `service.example`, and rejects validation at 2028-01-01 (`scratch/x509_verifier_cp_oracle.py`). The first Ed25519 certificate attempt was correctly rejected by cryptography's verifier default public-key policy; these EC fixtures are the authoritative oracle for the next genuine OpenSSL-backed verification implementation. No verifier code was added yet and upstream matrix remains 5/7.

2026-09-25 completed SCT/store gate: Full Release ALL_BUILD, complete core fixtures, CTest **53/53**, and complete local FastAPI **202/202** exit zero (`scratch/fastapi-release-x509-sct-store-20260925.log`, `scratch/fastapi-fixtures-x509-sct-store-20260925.log`, `scratch/fastapi-ctest-x509-sct-store-20260925.log`, `scratch/fastapi-local-x509-sct-store-20260925.log`). The gate includes both new real X.509 SCT and OpenSSL-backed Store routes plus Uvicorn HTTP/HTTPS and WebSocket. SCT expected output was generated by CPython's unchanged high-level certificate-extension parser; XLang3 uses a private native extractor while high-level `Certificate.extensions` remains incomplete. `git diff --check` passes. Native cryptography package dependencies are system DLLs only, no Python DLL. Demo `/api/runtime` still reports XLang3 3.14.7/FastAPI ready at `http://127.0.0.1:8765/`. Unchanged `trustme` now stops at missing real `_rust.x509.VerifiedClient` after advancing past `Sct` and `Store`; AnyIO/HTTPX upstream collection remains blocked and the matrix remains **5/7**. A deterministic EC verification oracle is prepared with valid chain/hostname/time outcomes for the next native verification slice. No commit or push.

2026-09-25 native X.509 verification backend continuation: `Store._verify_server_dns` now performs real OpenSSL `X509_STORE_CTX` path validation using the trusted anchors, intermediate certificates, explicit validation time, SSL-server purpose, and SAN-only DNS hostname matching. It returns the actual verified certificate chain as native Certificate objects and raises a genuine native `VerificationError` subclass on failure. The deterministic P-256 certificates in `tests/fastapi/x509_verify_root.pem`, `x509_verify_leaf.pem`, and `x509_verify_cn_only.pem` are signed by fixed ECDSA scalars and form the CPython oracle. Native and unmodified CPython cryptography verifier outputs agree for chain serials `[1002,1001]`, wrong hostname rejection, expiry rejection, and rejection of a certificate with matching CN but missing required SAN. An initial OpenSSL default accepted CN-only; setting `X509_CHECK_FLAG_NEVER_CHECK_SUBJECT` fixed that genuine policy difference. Public `cryptography_x509_verification_contract` exercises the backend through FastAPI/TestClient and matches the CPython high-level policy/verifier oracle. This internal Store method is a foundation for the unchanged `PolicyBuilder`/`ServerVerifier` API, which is not implemented yet. Unchanged `trustme` still stops at missing `VerifiedClient`; upstream matrix remains **5/7**. Full Release ALL_BUILD and `git diff --check` pass, with system DLL dependencies only and no Python DLL. Full fixtures, CTest, and **203-case** local FastAPI gate are running in `scratch/fastapi-*-x509-verify-20260925.log`; await exit status. No commit or push.

Standard policy API oracle for the next slice (`scratch/x509_policy_cp_probe.py`): CPython `PolicyBuilder.store()` and `.time()` return new builders; `build_server_verifier(DNSName('service.example'))` exposes `ServerVerifier`, `Store`, and `Policy` with default max chain depth 8, subject `service.example`, naive UTC validation time `2025-07-01T00:00:00`, server-auth EKU OID `1.3.6.1.5.5.7.3.1`, and minimum RSA modulus 2048. `build_client_verifier()` returns `ClientVerifier`. Building either verifier without a trust store raises `ValueError`; negative max depth raises `OverflowError`. These are observed CPython behaviors, not yet XLang3 claims.

Additional CPython policy evidence: `ExtensionPolicy.permit_all()` used for both CA and EE is rejected at server-verifier construction with `ValueError: A CA extension policy must require the basicConstraints extension to be present.` The pair `webpki_defaults_ca()`/`webpki_defaults_ee()` verifies the deterministic EC chain successfully. This is a real policy condition to implement in the future standard native verifier types, not a justification to expose placeholder aliases. The current 203-case local gate has passed the new path/hostname/time/SAN verification route and is still running.

2026-09-25 completed native verification-backend gate: Release ALL_BUILD, complete core fixtures, CTest **53/53**, and complete local FastAPI **203/203** all exit zero (`scratch/fastapi-release-x509-verify-20260925.log`, `scratch/fastapi-fixtures-x509-verify-20260925.log`, `scratch/fastapi-ctest-x509-verify-20260925.log`, `scratch/fastapi-local-x509-verify-20260925.log`). The new FastAPI route verifies the actual OpenSSL certificate chain against CPython's unchanged verifier and rejects wrong hostname, expired validation time, and matching-CN/no-SAN certificates. Real Uvicorn HTTP/HTTPS and WebSocket still pass. `git diff --check` passes; native package links system DLLs only, no Python DLL. The demo remains live at `http://127.0.0.1:8765/` and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. This private Store verification method is genuine backend functionality, but the unchanged public `PolicyBuilder`, `ServerVerifier`, `ClientVerifier`, `VerifiedClient`, and extension-policy types remain missing. Unchanged `trustme`/upstream AnyIO and HTTPX collection therefore remain blocked; upstream matrix is **5/7**. No commit or push.

2026-09-25 standard X.509 policy continuation (in progress): Native `PolicyBuilder` now implements immutable `store()`, `time()`, and `max_chain_depth()` and constructs a real `ServerVerifier` with retained Store/Policy state. `ServerVerifier.verify()` invokes the OpenSSL trust-chain/hostname/time path, and its `store`, `policy`, `Policy.subject`, `Policy.max_chain_depth`, and naive UTC `Policy.validation_time` accessors are native. The private Store path accepts a maximum-depth argument. The cryptography Release package builds, the existing FastAPI certificate-verification contract still returns the CPython oracle result, and a direct XLang3 builder probe confirms immutable builder identity with a real trusted certificate. This standard public path is **not yet validated end-to-end**: unchanged `cryptography.x509.verification` still requires real `VerifiedClient`, `ClientVerifier`, `ExtensionPolicy`, and `Criticality`, and standard `Policy` default EKU/RSA-key policy is not implemented. Do not count it as upstream matrix progress; matrix remains **5/7**. Next: complete those native policy types and compare the unchanged public verifier API to CPython before any compatibility claim. No commit or push.

2026-09-25 standard builder refinement and gates: The pinned cryptography 46.0.7 Rust source confirmed that `PolicyBuilder` settings are single-use, max depth is `u8`, naive datetime is interpreted as UTC, and `PolicyBuilder`/`Policy` report `cryptography.x509.verification` as `__module__`. The native implementation now follows those rules, rejects direct construction of Policy/ServerVerifier, and floors pre-epoch fractional timestamps consistently with seconds-only policy times. `scratch/x509_policy_native_probe.py` produces **identical output** on CPython 3.14 and final XLang3 for immutable builders, missing trust store, negative/256 depth, and repeated depth/store/time settings. The final Release ALL_BUILD, core fixtures, CTest **53/53**, direct builder differential, certificate-verification FastAPI route, and `git diff --check` pass (`scratch/fastapi-release-x509-policy-final-20260925.log`, `scratch/fastapi-fixtures-x509-policy-final-20260925.log`, `scratch/fastapi-ctest-x509-policy-final-20260925.log`). The complete local FastAPI gate passed **203/203** on the immediately preceding DLL (`scratch/fastapi-local-x509-policy-20260925.log`); only the native policy refinements changed afterward, and the targeted X.509 route/probe passed on the final DLL. Demo `/api/runtime` still reports XLang3 3.14.7/FastAPI ready. Unchanged `import trustme` still stops at missing native `VerifiedClient`, so AnyIO/HTTPX upstream collection and the matrix remain **5/7**. The standard verifier path is not yet public-end-to-end verified. No commit or push.

2026-09-25 native client verifier continuation: `modules/cryptography/x509_module.cpp` now includes real OpenSSL `SSL_CLIENT` chain verification, native `ClientVerifier` and `VerifiedClient` objects, and `PolicyBuilder.build_client_verifier()`. The policy exposes a genuine client-auth EKU ObjectIdentifier, no subject, default depth 8, a retained trust store, and the expected missing-store error. A valid client-auth fixture (`tests/fastapi/x509_verify_client.pem`) has a deterministic P-256 signature and DNS SAN; unchanged CPython 3.14 verifies it to chain `[1004,1001]` with subject `client.example` (`scratch/x509_client_cp_oracle.py`). A separate deterministic client certificate without SAN (`x509_verify_client_no_san.pem`) and the existing server-auth certificate are rejected by both runtimes as `VerificationError`. The public FastAPI `cryptography_x509_client_policy_contract` matches CPython exactly on client policy metadata and both rejection paths; the final expanded route was separately rerun after the complete gate. Full Release ALL_BUILD, core fixtures, CTest **53/53**, and local FastAPI **204/204**, including real Uvicorn HTTP/HTTPS and WebSocket, exited zero (`scratch/fastapi-*-x509-client-20260925.log`). `git diff --check` passes, and the native DLL imports only Windows system DLLs, no Python DLL. Demo `/api/runtime` reports XLang3 3.14.7/FastAPI ready. **Positive client verification on XLang3 remains unverified:** it completes OpenSSL chain validation but cannot construct unchanged Python `DNSName` subjects because the high-level `cryptography.x509` import stops at missing native `ExtensionPolicy`. `VerifiedClient` is a real result type but the full high-level path is incomplete; no upstream AnyIO/HTTPX pass is claimed. Matrix stays **5/7**. Next native work is genuine `ExtensionPolicy`/`Criticality` behavior, followed by the remaining X.509 builder and extension APIs needed by unchanged `trustme`. No commit or push.

2026-09-25 native X.509 extension policy continuation: `modules/cryptography/x509_module.cpp` now exports real `ExtensionPolicy` and `Criticality` objects in the standard verification module. Immutable `permit_all`, WebPKI CA/EE defaults, and custom no-callback presence/criticality rules are represented natively and applied to the verified X.509 chain through OpenSSL extension inspection. `PolicyBuilder.extension_policies(ca_policy=..., ee_policy=...)` enforces single assignment, mandatory CA BasicConstraints, and mandatory server EE SAN. The explicit CA-default/EE-permissive policy verifies the real client-auth certificate **without SAN** to chain `[1004,1001]`, `subjects=None`, matching unchanged CPython 3.14; default EE policy still rejects it. The public FastAPI `cryptography_x509_extension_policy_contract` matches CPython exactly for that successful chain, Criticality representation, invalid CA policy, and repeated policy assignment. Full Release ALL_BUILD, core fixtures, CTest **53/53**, and the local FastAPI gate **205/205** exited zero (`scratch/fastapi-*-x509-extension-policy-20260925.log`), including Uvicorn HTTP/HTTPS and WebSocket; `git diff --check` passes. The native DLL imports only Windows system DLLs and no Python DLL. Demo `/api/runtime` reports XLang3 3.14.7/FastAPI ready. Unchanged `import trustme` now advances to missing `_rust.openssl.x25519`, so the untouched upstream matrix remains **5/7**. Extension-policy Python validator callbacks still raise an explicit `NotImplementedError`, and not every pinned WebPKI semantic validator is implemented; these are open parity gaps, not successes. The standard positive client path with DNSName subjects remains unverified because the high-level import is still blocked. A deterministic CPython X25519 oracle is captured in `scratch/cryptography_x25519_cp_oracle.py` for the next genuine native dependency slice. No commit or push.

2026-09-25 native X25519 continuation: `modules/cryptography/x25519_module.cpp` adds OpenSSL-backed X25519 raw private/public import, key generation, public derivation, shared-secret exchange, raw/DER/PEM serialization, copy and public equality, with no CPython binary dependency. The deterministic direct oracle `scratch/cryptography_x25519_cp_oracle.py` matches CPython 3.14 exactly for both fixed public keys, shared secret, exchange symmetry, raw private bytes, copying, and invalid key lengths. The public FastAPI `cryptography_x25519_contract` matches CPython for those cases plus DER SHA-256 digests, generated-key exchange, and low-order public-key rejection. Release ALL_BUILD, complete core fixtures, and CTest **53/53** exit zero (`scratch/fastapi-*-x25519-20260925.log`). The first full local FastAPI run failed **once** at `sync_endpoint_task_lifetime` with `tasks 1 1; dangling 2` instead of `0 0; 1`, after the X25519 route had passed. Eight isolated exact-case reruns and 500 additional TestClient portal cycles (100 sequential plus four concurrent 100-cycle processes) showed zero anomalies. Without changing the test, timing, or runtime, the second complete local gate exited zero with **206/206** and Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-x25519-rerun-20260925.log`). The one observed lifetime failure is unresolved intermittent evidence and must remain visible; a green rerun does not prove it impossible. `git diff --check` passes, the native DLL imports only system DLLs/no Python DLL, and the live demo reports XLang3 3.14.7/FastAPI ready. Unchanged `import trustme` now advances to missing native `_rust.x509.RevokedCertificate`, so the upstream matrix remains **5/7**. The unchanged high-level `cryptography.hazmat.primitives.asymmetric.x25519` constructor also reaches missing `_rust._openssl`; the direct native oracle is not a claim of complete high-level cryptography parity. `scratch/x509_revoked_cp_oracle.py` records the real one-entry CRL behavior for the next native slice. No commit or push.

2026-09-25 native CRL revoked-entry continuation: `modules/cryptography/x509_module.cpp` now owns duplicated OpenSSL `X509_REVOKED` records and exports a real `RevokedCertificate`, CRL integer/slice indexing and iteration, serial-number lookup, serial/date properties, and a genuine empty `Extensions` collection for entries with no extensions. Nonempty revoked-entry extension decoding remains an explicit `NotImplementedError`; do not claim full X.509 parity. The pinned one-entry/empty CRL oracle (`scratch/x509_revoked_cp_oracle.py`) matches CPython 3.14 exactly for serial 42, UTC date, empty extensions, lookup/miss, iteration, and a full slice. Public `cryptography_x509_revoked_contract` matches CPython through unchanged FastAPI/TestClient, including rejection of a string serial. The full Release ALL_BUILD, core fixtures, CTest **53/53**, and local FastAPI **207/207** gate exit zero (`scratch/fastapi-*-x509-revoked-20260925.log`), including Uvicorn HTTP/HTTPS and WebSocket. The serial-type validation and extra negative contract assertion were applied after the full gate passed its CRL case; the final native target builds, the expanded public route and direct oracle pass on the final DLL, and `git diff --check` passes. The live demo `/api/runtime` reports XLang3 3.14.7/FastAPI ready. Unchanged `import trustme` now succeeds, but actual `trustme.CA()` stops at missing native `_rust.x509.create_x509_certificate`; this is the next real cryptography builder boundary. Running the unchanged AnyIO/HTTPX upstream suites now collects past the earlier trustme import failure, then fails collection on absent `psutil` and `chardet` respectively. Pinned pure-Python `chardet==5.2.0` is added to test dependencies and imports on XLang3; HTTPX then advances to absent native-backed `zstandard`. AnyIO and HTTPX are **not** green, so the matrix remains **5/7**. Do not use CPython extension binaries or test-only fake modules for these dependencies. The earlier intermittent sync endpoint lifetime observation remains unresolved. No commit or push.

2026-09-25 native X.509 name-encoding continuation: The unchanged `cryptography.x509.Name.public_bytes()` now calls genuine OpenSSL-backed `_rust.x509.encode_name_bytes` in `modules/cryptography/x509_module.cpp`. It walks actual Python Name/RDN/NameAttribute objects, preserves explicit ASN.1 value types, encodes BMP/Universal strings correctly, sorts entries inside each RDN by DER, and emits the canonical DER Name. The public `cryptography_x509_name_contract` route matches CPython 3.14 byte for byte for simple and grouped names, Unicode, BMPString, IA5String, and empty names. A first native attempt grouped separate RDNs and was corrected; a second attempt failed canonical sorting within a multivalued RDN and was corrected before the final oracle. The native target builds, the full local FastAPI gate exits zero **208/208** including Uvicorn HTTP/HTTPS and WebSocket (`scratch/fastapi-local-x509-name-20260925.log`), `git diff --check` passes, and live demo `/api/runtime` still reports XLang3 3.14.7/FastAPI ready. Full Release ALL_BUILD, fixtures, and CTest **53/53** passed immediately before this isolated native name addition; the native target and complete FastAPI gate were rerun afterward. Actual `trustme.CA()` is still blocked by missing genuine `create_x509_certificate`, which requires extension DER encoding and certificate methods; do not count it as working. The upstream matrix remains **5/7**, with AnyIO/HTTPX collection still blocked by `psutil`/`zstandard`. No commit or push.

2026-09-25 native X.509 certificate builder and verified TLS continuation: `modules/cryptography/x509_module.cpp` now implements real OpenSSL DER for BasicConstraints, SubjectKeyIdentifier, KeyUsage, AuthorityKeyIdentifier key IDs, ExtendedKeyUsage, and SubjectAlternativeName (DNS, email, IPv4/IPv6 addresses and networks), with byte-for-byte CPython 3.14 agreement through the public `cryptography_x509_extension_der_contract` FastAPI route. Native `_rust.x509.create_x509_certificate` reads the unchanged Python certificate builder, serializes its real public/signing keys through the public serialization API, builds X.509 names/times/serial/extensions, and signs the actual certificate with OpenSSL. Certificate `subject`, `issuer`, and `extensions` getters decode the native X.509 object back into unchanged Python cryptography types for the exercised extensions. Unchanged `trustme.CA()` and `issue_cert()` now work, including RSA root -> intermediate -> RSA leaf. The public `trustme_tls_contract` FastAPI route matches CPython for a verified ECDSA TLS 1.3 handshake, a verified RSA intermediate-chain TLS 1.3 handshake, certificate SAN/extension decoding, and rejection of wrong-hostname and untrusted-root connections. The untrusted-root differential exposed an `_ssl` error-class bug; `modules/net/ssl_module.cpp` now raises `SSLCertVerificationError` whenever OpenSSL's peer verification result is non-OK even when `SSL_get_error()` reports `SSL_ERROR_SYSCALL`, while preserving syscall errors without a verification failure. The demo was restarted after rebuilding its loaded SSL DLL. Full Release ALL_BUILD, complete core fixtures, CTest **53/53**, and complete local FastAPI **210/210** gate pass (`scratch/fastapi-*-x509-builder-20260925.log`), including real Uvicorn HTTP/HTTPS/WebSocket. The live demo at `http://127.0.0.1:8765/` reports XLang3 3.14.7/FastAPI ready; 1,000-request/16-worker load and independent 30-second/16-worker soak pass with zero errors (`scratch/fastapi-demo-load-x509-builder-20260925.log`, `scratch/fastapi-demo-soak-x509-builder-20260925.log`). `git diff --check` passes; both native cryptography and SSL DLLs import only Windows system DLLs, no Python DLL. The untouched AnyIO/HTTPX matrix was rerun on this build and remains **5/7**: AnyIO collection stops at missing `psutil`, HTTPX at missing native-backed `zstandard` (`scratch/fastapi-upstream-x509-builder-20260925.log`). Certificate parity is still incomplete: unsupported extension/general-name forms are explicit errors, RSA-PSS and deterministic ECDSA certificate signing are not implemented, and several Certificate getters remain absent. Previous intermittent sync endpoint lifetime evidence remains open. No commit or push.

2026-09-25 compression and upstream HTTPX continuation (in progress): The official pure-Python `zstandard==0.25.0` package now imports its real XLang3 `zstandard.backend_c` native backend, statically linked to the bundled zstd 1.5.7 C source. `scratch/zstandard_cp_oracle.py` matches CPython 3.14 for exact compression frames, class roundtrips, malformed/truncated/multiple frames, and output-size behavior. The official pure-Python `brotli==1.2.0` package now imports a genuine `_brotli` native backend linked to upstream Brotli 1.2.0 C sources in `third_party/brotli-1.2.0`; the `brotli_contract` FastAPI/TestClient route matches CPython for an actual `Content-Encoding: br` response, exact compressed bytes, streaming decode, and malformed-input errors. Both native DLLs list only Windows system DLLs, no Python DLL. The two compression backends implement the exercised public APIs, not full upstream API parity. Pinned unchanged pure-Python HTTPX test dependencies `h2==4.4.1`, `hpack==4.2.0`, `hyperframe==6.1.0`, and `socksio==1.0.0` were added. The untouched 1,418-item HTTPX suite now collects and has advanced from missing `zstandard` through successive **160**, **164**, **207**, **231**, and **338** passing-test checkpoints before genuine runtime failures. The runtime fixes were general CPython behavior: `IntEnum` indices in sequences; memoryview iteration end-of-view and empty views; `bytes`/`bytearray` conversion of character-format memoryviews; `_socket.getaddrinfo` bytes hosts plus Windows `EAI_*` constants; and dictionary-values membership via runtime equality. Direct CPython-oracle fixtures and the failing upstream cases pass for these slices. The current full HTTPX rerun is `scratch/fastapi-upstream-httpx-dict-values-20260925.log`; do not count HTTPX green until it exits zero. Official `psutil==7.2.2` is pinned, but unchanged AnyIO collection still stops at missing genuine `psutil._psutil_windows` (`scratch/fastapi-upstream-anyio-psutil-20260925.log`); no fake/test-only psutil was used. The current complete local FastAPI gate and final core/CTest gates are running at `scratch/fastapi-local-compression-socket-dict-20260925.log` and `scratch/fastapi-{fixtures,ctest}-socket-dict-20260925.log`. No commit or push.

2026-09-25 verified compression/HTTPX continuation: `modules/zstandard/zstandard_module.cpp` now provides a real zstd streaming decompression object with `decompress`, `flush`, `eof`, and `unused_data`. All five unchanged HTTPX zstd decoder cases pass, covering normal frames, malformed input, empty data, truncation, and skippable/multiple frames. General runtime fixes also repair set-constructor hashing/equality without double-hashing new elements, `str.format` property and `__getitem__` field access, and socket connection failure subclasses/errno from actual OS errors. Focused unchanged HTTPX `raise_for_status` and connection exception tests pass. The complete untouched HTTPX run before the later native `mmap` addition reached **1,400 passed, 1 skipped, 14 failed, 3 errors** out of 1,418 (`scratch/fastapi-upstream-httpx-stream-20260925.log`). Ten CLI failures were from absent native `mmap`; three setup errors still need genuine cryptography `_rust._openssl`, while multipart/logging assertions also fail on CPython under the same current dependencies. A real native OS-backed `modules/mmap` package now provides anonymous and file-backed mappings with read/write/seek/flush/close and context lifecycle; its CPython/XLang3 fixture matches, and unchanged HTTPX CLI help, GET, and download tests pass. Full CLI remains open: Rich's ordinary repr highlighter feeds a 960-character alternation to XLang3 `_sre`, whose current `std::regex` search aborts with process exit 3 for `<3>`; `scratch/rich_regex_probe.py` isolates the failure. This is a general regex-engine gap, not a FastAPI-specific case. `click==8.4.2` is pinned within HTTPX's declared `click==8.*` range because Click 8.5 makes the unchanged download test fail under CPython too. A complete CPython 3.14 baseline with the same packages exits with **1,412 passed, 1 skipped, 5 failed** (`scratch/httpx-cpython-baseline-20260925.log`): multipart text-file expectation, two Trio timeout cases, and two logging assertions. Do not count those as XLang-only failures or claim an exit-zero HTTPX matrix. The final complete local FastAPI gate passes **212/212**, including real Uvicorn HTTP/HTTPS/WebSocket and zstd/Brotli contracts (`scratch/fastapi-local-final-20260925.log`). Full Release ALL_BUILD, complete core fixtures including the mmap/set/format/socket regressions, CTest **53/53**, and `git diff --check` pass (`scratch/fastapi-release-mmap-final-20260925.log`, `scratch/fastapi-fixtures-mmap-20260925.log`, `scratch/fastapi-ctest-mmap-20260925.log`). The demo is live at `http://127.0.0.1:8765/` on the final Release build: page 200, `/api/runtime` reports XLang3 3.14.7/FastAPI ready, and create/list/toggle/delete leaves zero tasks. AnyIO still cannot collect until a genuine `psutil._psutil_windows` backend exists; the overall untouched matrix remains **5/7 exit-zero**. Native zstd, Brotli, mmap and cryptography APIs are real but not complete upstream API implementations. Prior intermittent task-lifetime evidence remains open. No commit or push.

2026-09-25 `_sre`/mmap final checkpoint: General `_sre` lookbehind evaluation now uses a capture at the actual subject offset and retries later top-level alternatives after a rejected host-engine match. This fixes the Rich highlighter abort; the unchanged HTTPX CLI file passes **11/11** (`scratch/httpx-cli-sre-nosubs-20260925.log`). CTest initially exposed `re.split(r"(?<=:)", ":a:b::c")` regression for a pattern with no public groups: host `nosubs` had discarded the internal lookbehind marker. Disabling `nosubs` only when a marker exists fixes the CPython result and all CTest **53/53** pass (`scratch/fastapi-ctest-sre-nosubs-20260925.log`). The final Release ALL_BUILD passes (`scratch/fastapi-release-sre-nosubs-20260925.log`), and the full local FastAPI gate now exits zero **213/213**, including the real file-backed `mmap` TestClient contract and Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-sre-nosubs-20260925.log`). The production-style demo is running at `http://127.0.0.1:8765/` on this binary; page/runtime and create/toggle/delete flow pass, 1,000 requests at 16 workers and a 30-second 16-worker soak have zero errors (`scratch/fastapi-demo-load-sre-nosubs-20260925.log`, `scratch/fastapi-demo-soak-sre-nosubs-20260925.log`). `git diff --check` exits zero. A full untouched HTTPX rerun collects 1,418 and reaches 95%, showing three cryptography `_rust._openssl` setup errors and a multipart assertion shared with CPython, but times out during `tests/test_timeouts.py` (`scratch/fastapi-upstream-httpx-nosubs-20260925.log`); do not claim a final full-suite passing count. One isolated unchanged `test_timeouts.py` run finishes all ten tests but hangs in the session fixture's `thread.join()` while Uvicorn's worker awaits `_overlapped.GetQueuedCompletionStatus` (`scratch/httpx-timeouts-isolated-20260925.log`); a repeat and smaller subsets exit cleanly (`scratch/httpx-timeout-full-repeat-20260925.log`). The intermittent async/IOCP shutdown gap remains open and must not be skipped. AnyIO remains blocked at collection by absent genuine `psutil._psutil_windows`, so the full seven-project upstream matrix remains **5/7 exit-zero**. No CPython ABI/DLL dependency, test-only fake dependency, commit, or push.

2026-09-25 psutil/SSL/AnyIO checkpoint: Added a genuine Windows native `psutil._psutil_windows` package under `modules/psutil`, compiled against Windows system APIs with no CPython runtime, DLL, or ABI. It supplies real process IDs/existence/parent mapping, network interface addresses, disk I/O counters, and process heap data/trim needed by the official unchanged `psutil==7.2.2` Python layer. The public FastAPI/TestClient psutil route has identical CPython 3.14 and XLang3 output (`scratch/psutil-cpython-contract-20260925.log`, `scratch/psutil-xlang-contract-20260925.log`). This native package is partial; unsupported psutil APIs remain unsupported, not faked. `_ssl` now raises OSError-derived SSL exceptions with `errno`/`strerror` and certificate `verify_code`/`verify_message`, and MemoryBIO server wrapping accepts a hostname argument as CPython does; CPython-oracle TLS contracts and the focused unchanged AnyIO `test_receive_invalid_max_bytes` slice pass (`scratch/anyio-xlang-bio-hostname-all-20260925.log`). The full untouched AnyIO run now collects and reaches **202 passed, 70 skipped, 12 failed, 9 errors** before maxfail 21 (`scratch/fastapi-upstream-anyio-bio-hostname-20260925.log`). Its CPython 3.14 baseline with the same pinned dependencies has **205 passed, 70 skipped, 10 failed, 10 errors** before maxfail 20, with the TLS trustme certificate failures also reproducible on CPython (`scratch/anyio-cpython-baseline-20260925.log`). Three XLang-only `test_not_closed_warning` failures expose a general user-defined `__del__` finalization gap. A broad refcount-finalizer experiment made the simple warning pass but crashed the profiler/core fixture, so it was reverted; no AnyIO-specific marker or workaround was retained. Unchanged HTTPX timeout tests also exhibit an intermittent Uvicorn/IOCP session-shutdown hang; repeated CPython runs show a hang too, so root-cause work remains open. The final Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-allbuild-psutil-ssl-bio-final-20260925.log`, `scratch/fastapi-ctest-psutil-ssl-bio-final-20260925.log`). The complete local FastAPI gate exits zero **214/214** (`scratch/fastapi-local-psutil-ssl-bio-final-20260925.log`). The demo at `http://127.0.0.1:8765/` runs this final binary: page/runtime, create/toggle/delete, 1,000-request/16-worker load, and 30-second/16-worker soak all pass with zero errors (`scratch/fastapi-demo-load-psutil-ssl-bio-final-20260925.log`, `scratch/fastapi-demo-soak-psutil-ssl-bio-final-20260925.log`). The seven-project untouched matrix remains **5/7 exit-zero**. No commit or push.

2026-09-25 further general compatibility checkpoint: Native `_ssl` MemoryBIO EOF now tells OpenSSL about EOF and raises CPython-compatible `SSLEOFError` instead of repeatedly requesting reads; the unchanged AnyIO TLS handshake-failure test now passes. Enabling `SSLContext.check_hostname` with `verify_mode=CERT_NONE` promotes verification to `CERT_REQUIRED`, matching CPython. Runtime set subtraction now uses Python equality/hash semantics for user objects; Windows `os.path.exists` follows symlinks while `lexists` checks the link, and `DirEntry.is_file`, `is_dir`, and `stat` honor `follow_symlinks`. Native ctypes now accepts tuple `argtypes` and bytes for `c_char_p`. Direct CPython 3.14 probes and new public FastAPI contracts cover these behaviors (`trustme_tls_contract`, `set_difference_contract`, `path_symlink_contract`, `ctypes_dll_call_contract`). Full Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-allbuild-ssl-ctypes-final-20260925.log`, `scratch/fastapi-ctest-ssl-ctypes-final-20260925.log`); the complete local FastAPI gate passes **216/216** including real Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-ssl-ctypes-final-20260925.log`). A complete unchanged CPython 3.14 AnyIO baseline collected 4,236 tests and finished **2,551 passed, 1,621 skipped, 4 xfailed, 60 failed, 39 errors** (`scratch/anyio-full-cpython-20260925.log`); this includes shared dependency/certificate failures. The corresponding full XLang3 run is active (`scratch/anyio-full-ssl-ctypes-xlang-20260925.log`) and must not be treated as complete yet. Confirmed XLang-only gaps include user-defined `__del__`, `gc.get_objects`, async-generator awaitable `throw`/`close` and Coroutine ABC recognition, typed ctypes pointer/structure returns for Windows truststore, Trio scheduling checkpoints, and a cached class-method descriptor call. These are general runtime/native-boundary issues; no test-only workaround was added. The seven-project upstream matrix remains **5/7 exit-zero**. No commit or push.

2026-09-25 async delegation continuation: The previous full XLang3 AnyIO diagnostic run was deliberately stopped at 39% after it exposed distinct general gaps; it is a partial log, not a suite result (`scratch/anyio-full-ssl-ctypes-xlang-20260925.log`). Native async-generator awaitables now implement `throw` and `close` and expose those and `send` on their type, so the Python 3.14 `Coroutine` ABC and `asyncio.coroutines.iscoroutine()` recognize real `asend`/`anext`/`athrow`/`aclose` objects. A direct CPython/XLang3 protocol oracle matches, and unchanged AnyIO `test_wait_all_tasks_blocked_asend[asyncio]` passes. `itertools.combinations`, `combinations_with_replacement`, and `permutations` now raise the CPython `ValueError` class for negative `r`; 12 unchanged AnyIO cases pass with four winloop skips. The compiler now lowers `yield from` to a resumable `YieldFrom` operation, forwarding sent values and preserving delegated generator return, throw, and close paths; the direct CPython oracle `tests/fixtures/core/yield_from_protocol.py` matches XLang3. Because old cached modules retained the old lowering, XLang3's private bytecode magic was advanced to invalidate stale `.xlang3-314.pyc` files. With source recompiled, unchanged AnyIO `tests/test_futures.py` passes **51 passed, 17 winloop skipped**, including every Trio case (`scratch/anyio-futures-yieldfrom-magic-20260925.log`). Full Release ALL_BUILD and CTest **53/53** pass on this build (`scratch/fastapi-allbuild-yieldfrom-magic-20260925.log`, `scratch/fastapi-ctest-yieldfrom-magic-20260925.log`). The final local FastAPI gate and full unchanged AnyIO rerun are active at `scratch/fastapi-local-yieldfrom-magic-20260925.log` and `scratch/anyio-full-yieldfrom-magic-20260925.log`; do not claim their result before process exit. Remaining general gaps include async-generator yields after a suspended await raises `StopAsyncIteration` (minimal `scratch/asyncgen_suspended_except_yield_probe.py`), custom iterator `yield from` send/return behavior (`scratch/yield_from_custom_iterator_probe.py`), user-defined `__del__`, `gc.get_objects`, and typed ctypes pointers/structures. No fake dependency, FastAPI-specific workaround, commit, or push.

2026-09-25 general protocol and socket continuation: Native async-generator awaitables expose `send`/`throw`/`close` and satisfy Python 3.14's Coroutine ABC; real resumable `yield from` now forwards send/return values through generator and custom-iterator delegates. The private XLang bytecode magic advanced to invalidate old cached lowering. Unchanged AnyIO `tests/test_futures.py` passes **51 passed, 17 winloop skipped**; unchanged `tests/test_itertools.py` passes **297 passed, 99 winloop skipped** after native itertools negative-`r`, cached class-method dispatch, `operator.index`, and suspended async-generator exception fixes. Custom iterator `yield from` now also forwards `throw` and `close`; CPython-oracle fixtures `yield_from_protocol`, `yield_from_custom_throw_close`, and `async_generator_suspended_exception` pass. `OSError(errno, ...)` now selects CPython-compatible subclasses for common POSIX/Windows socket errors, and native `_overlapped.Overlapped.getresult()` raises the corresponding connection/timeout exception with numeric error attributes. A direct AnyIO connection-refused cause probe changed from plain OSError to ConnectionRefusedError; the exact unchanged upstream `TestTCPStream::test_connection_refused` slice passes **6 passed, 2 winloop skipped** (`scratch/anyio-socket-connection-refused-overlapped-20260925.log`). The public `runtime_protocol_contract` FastAPI/TestClient route exercises these general protocols and matches CPython 3.14 output exactly.

Full Release ALL_BUILD, the complete core fixture runner, CTest **53/53**, and `git diff --check` pass on the final source (`scratch/fastapi-allbuild-overlapped-remap-20260925.log`, `scratch/fastapi-fixtures-overlapped-remap-20260925.log`, `scratch/fastapi-ctest-overlapped-remap-20260925.log`). The complete local FastAPI gate passes **217/217**, including real Uvicorn HTTP and WebSocket (`scratch/fastapi-local-overlapped-remap-20260925.log`). The latest demo remains live at `http://127.0.0.1:8765/` on XLang3 3.14.7; page/runtime return 200, its 1,000-request/16-worker load and 30-second/16-worker soak both had zero errors (`scratch/fastapi-demo-load-overlapped-remap-20260925.log`, `scratch/fastapi-demo-soak-overlapped-remap-20260925.log`). A full unchanged AnyIO run collected 4,236 tests but was deliberately interrupted at **47%** (2,091 reported outcomes) to fix the native socket error path; it is a partial diagnostic log, not a completed suite result (`scratch/anyio-full-await-clear-final-20260925.log`). The complete CPython baseline remains 2,551 passed, 1,621 skipped, 4 xfailed, 60 failed, 39 errors. Before interruption, XLang-only categories included user-defined `__del__`, typed ctypes pointers/structures for Windows truststore, missing `gc.get_objects`, sourceless-install/pytest-plugin behavior, and additional socket exception/cause cases; 55 unique XLang-only failing test IDs were observed in the partial log, some likely related to the newly fixed socket error subclassing. These remain to isolate and verify. The seven-project untouched upstream matrix is still **5/7 exit-zero**, so full FastAPI ecosystem compatibility is not yet claimed. No CPython runtime/ABI/DLL dependency, fake test dependency, FastAPI-specific runtime workaround, commit, or push.

2026-09-25 socket follow-up after the preceding checkpoint: The exact unchanged AnyIO `test_send_exception_cause_preserved` and `test_receive_exception_cause_preserved` each initially stopped at missing `socket.SO_LINGER`. Exporting the genuine Windows `SO_LINGER` constant makes both slices pass (6 passed, 2 winloop skipped each). The unchanged `test_connect_tcp_with_local_port` then exposed `_overlapped.BindLocal` reporting WSAEINVAL/10022 without `OSError.winerror`; Python 3.14's unchanged `asyncio.windows_events` catches that specific winerror for an already-bound local socket before `ConnectEx`. Native BindLocal now preserves the actual WSA code via the general overlapped error construction path. The local-port slice passes 6 passed/2 winloop skipped. A combined final-source run of all four unchanged AnyIO socket slices (connection refused, local port, send cause, receive cause) passes **24 passed, 8 winloop skipped** (`scratch/anyio-socket-four-slices-final-20260925.log`). Full Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-allbuild-bindlocal-winerror-20260925.log`, `scratch/fastapi-ctest-bindlocal-winerror-20260925.log`). The final-source 217-case FastAPI gate is active at `scratch/fastapi-local-bindlocal-winerror-20260925.log` and must not be claimed green until exit. The demo is live again on this build at `http://127.0.0.1:8765/`; 1,000 requests/16 workers and 30-second/16-worker soak each had zero errors (`scratch/fastapi-demo-load-bindlocal-winerror-20260925.log`, `scratch/fastapi-demo-soak-bindlocal-winerror-20260925.log`). A focused unchanged AnyIO sourceless-install test fails because `xlang3 -m venv` invokes a child `ensurepip` that exits 103 (`scratch/anyio-sourceless-focused-20260925.log`); this is a real general stdlib/runtime gap, not patched with a test-specific route. No commit or push.

2026-09-25 compact `-m` continuation: The general XLang3 command-line parser now accepts CPython's joined module form (`-mfoo`) as well as `-m foo`. Both Python and Windows PowerShell CLI compatibility runners now assert module argv for this form. Full Release ALL_BUILD and CTest **53/53** pass on this source (`scratch/fastapi-allbuild-compact-m-final-20260925.log`, `scratch/fastapi-ctest-compact-m-final-20260925.log`), and `git diff --check` passes. The direct `_overlapped.BindLocal` IPv4/IPv6 oracle now exactly matches CPython's OSError class, errno 22, and winerror 10022 (`scratch/bindlocal-error-cpython-20260925.log`, `scratch/bindlocal-error-xlang-20260925.log`). The four unchanged AnyIO socket slices passed **24/24 available, 8 winloop skipped** on the immediately prior source; the final source only changes CLI parsing and the CLI tests, not those socket paths. The focused upstream `test_autouse_async_fixture` now progresses past its original `-mpytest` rejection but its spawned child hangs and the outer test times out (`scratch/anyio-pytest-autouse-compact-m-20260925.log`); the spawned orphan was stopped. A minimal private-name-mangling probe matches CPython, so the accompanying pytest snapshot teardown error is not yet attributed. The final-source complete local FastAPI gate is active in `scratch/fastapi-local-compact-m-final-20260925.log` and must not be claimed complete before exit. The demo is live on the final binary at `http://127.0.0.1:8765/`, page/runtime 200. No commit or push.

2026-09-25 final verification for this continuation: The source described above passed the complete local FastAPI gate **217/217** on the exact final Release executable, including actual Uvicorn HTTP and WebSocket (`scratch/fastapi-local-compact-m-final-20260925.log`). CTest **53/53**, full Release ALL_BUILD, direct CLI compatibility, and `git diff --check` pass (`scratch/fastapi-ctest-compact-m-final-20260925.log`, `scratch/fastapi-allbuild-compact-m-final-20260925.log`, `scratch/cli-compat-compact-m-20260925.log`). The live XLang3 demo at `http://127.0.0.1:8765/` runs this binary and returns page 200 plus XLang3 3.14.7/FastAPI-ready runtime; 1,000 requests at 16 workers and a 30-second 16-worker soak each completed with zero errors (`scratch/fastapi-demo-load-compact-m-final-20260925.log`, `scratch/fastapi-demo-soak-compact-m-final-20260925.log`). The complete unchanged CPython AnyIO baseline is recorded, but the XLang full run remains partial at 47%; focused final-source socket slices were 24 passed/8 winloop skipped. The seven-project upstream matrix remains **5/7 exit-zero**, so the objective remains active. Remaining verified gaps include general user-defined finalization, typed ctypes pointer/structure interoperability, `gc.get_objects`, `venv` child `ensurepip`, spawned pytest-plugin child hang, further socket behavior, and the unchanged HTTPX full suite. No commit or push.

2026-09-25 native socket and IOCP continuation: Native `socket` now preserves Windows nonblocking errors as `BlockingIOError` with errno/winerror 10035 across send/recv variants, and closed sockets retain `fileno() == -1` and raise OSError 10038 instead of silently creating a replacement socket. CPython 3.14 oracle fixtures `socket_nonblocking_errors` and `socket_closed_errors` were registered in the core runner; public FastAPI/TestClient route `socket_nonblocking_contract` checks both paths and matches CPython output. Unchanged AnyIO concurrent-send passes 6/6 available, and four closed/peer-closed socket slices pass 24/24 available (8 winloop skips). In `_overlapped`, `Overlapped.cancel()` no longer posts a synthetic IOCP completion after a real completion may already have been delivered. The unchanged AnyIO server-crash case passes 4/4 available (2 winloop skips), and the wider unmodified `TestTCPStream` diagnostic excluding known CPython-baseline TLS failures and XLang-only `gc.get_referrers` refcycle cases exits zero: **163 passed, 55 winloop skipped, 28 deselected** (`scratch/anyio-tcpstream-after-iocp-cancel-20260925.log`). The final Release ALL_BUILD, CTest **53/53**, and `git diff --check` pass (`scratch/fastapi-allbuild-iocp-cancel-20260925.log`, `scratch/fastapi-ctest-iocp-cancel-20260925.log`). The expanded **218-case** local FastAPI gate is running at `scratch/fastapi-local-iocp-cancel-20260925.log`; do not claim it passed until process exit. The full unchanged XLang AnyIO run remains partial at 47% and the upstream seven-project matrix last confirmed **5/7 exit-zero**. No CPython runtime/ABI/DLL dependency, FastAPI-specific runtime workaround, commit, or push.

2026-09-25 continuation after the IOCP gate: The exact IOCP-source local FastAPI gate exited zero **218/218** (`scratch/fastapi-local-iocp-cancel-20260925.log`), with real Uvicorn HTTP/WebSocket. Its demo passed 1,000 requests/16 workers and a separate 30-second/16-worker soak with zero errors (`scratch/fastapi-demo-load-iocp-cancel-20260925.log`, `scratch/fastapi-demo-soak-iocp-cancel-20260925.log`). Expanding unchanged AnyIO `tests/test_sockets.py` beyond TCP streams found a general Python descriptor issue at `bool(MagicMock())` in the listener-bind-failure test. Runtime truth testing now binds descriptor-valued `__bool__`/`__len__`; native `socket.getaddrinfo()` now accepts the standard `__index__` integer protocol, including int subclasses; binary `|` no longer treats an arbitrary instance's `__dict__` storage as a dict union, and the VM invokes descriptor-provided callable special methods; subscription resolves descriptor-valued `__getitem__`. CPython-oracle core fixtures `special_truth_descriptor` and `socket_index_args` cover these behaviors. The exact unchanged AnyIO `test_tcp_listener_total_bind_failure` now passes (`scratch/anyio-listener-bind-failure-getitem-20260925.log`). Final-source Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-allbuild-mock-getitem-20260925.log`, `scratch/fastapi-ctest-mock-getitem-20260925.log`). A broader unchanged AnyIO socket run on that source reaches ~55% but the pytest timeout plugin terminates a later socket test while asyncio waits on IOCP; this is a partial diagnostic, not a passing suite (`scratch/anyio-all-sockets-getitem-20260925.log`). The **final-source 218-case FastAPI gate** is running at `scratch/fastapi-local-mock-getitem-20260925.log` and must not be claimed green until exit. Latest-source demo is live again on port 8765. A focused unchanged HTTPX `test_config.py` run reached six passes then stopped at missing genuine `cryptography.hazmat.bindings._rust._openssl` in encrypted-key fixture setup (`scratch/httpx-config-after-iocp-20260925.log`); the isolated HTTPX text-mode multipart failure was reproduced on CPython 3.14 and is shared baseline (`scratch/httpx-multipart-text-cpython-20260925.log`). The upstream seven-project matrix remains last confirmed **5/7 exit-zero**. No commit or push.

2026-09-25 real Windows datagram continuation: The broader socket timeout was isolated with verbose unchanged AnyIO output to `TestUDPSocket::test_send_receive[asyncio-ipv4]`. `_overlapped.Overlapped.WSARecvFrom`, `WSARecvFromInto`, and `WSASendTo` had been taking a synthetic completion fallback; they now execute genuine Winsock overlapped operations, retaining socket buffers, flags, and sockaddr storage through completion and returning actual received data/address or transfer counts. The exact UDP send/receive case passes, and the unchanged `TestUDPSocket` class exits zero **52 passed, 60 platform/upstream skips** (`scratch/anyio-udp-class-real-overlapped-20260925.log`). The next broad socket run then exposed `_overlapped.WSAConnect` as explicitly unimplemented for connected UDP. That module-level function now performs the genuine Windows WSAConnect call and preserves native errors; the shared address parser also retains IPv6 flowinfo and scope ID. The exact unchanged connected-UDP extra-attributes case passes, and the whole unchanged `TestConnectedUDPSocket` class exits zero **56 passed, 64 skips** (`scratch/anyio-connected-udp-class-wsaconnect-20260925.log`). On this source, the broader unchanged AnyIO `tests/test_sockets.py` diagnostic, excluding only already identified CPython-baseline TLS cases and XLang-only `gc.get_referrers` refcycle cases, exits zero: **435 passed, 814 skipped, 28 deselected** (`scratch/anyio-all-sockets-wsaconnect-20260925.log`). Final Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-allbuild-wsaconnect-20260925.log`, `scratch/fastapi-ctest-wsaconnect-20260925.log`). The exact-source **218-case FastAPI gate** is running at `scratch/fastapi-local-wsaconnect-20260925.log`; do not claim it green until exit. The demo was restarted on final source at port 8765 and a fresh 1,000-request load is running. The full unchanged AnyIO suite and HTTPX suite remain unresolved; seven-project matrix last confirmed **5/7 exit-zero**. No CPython runtime/ABI/DLL dependency, fake protocol result, FastAPI-specific runtime workaround, commit, or push.

The intermediate `scratch/fastapi-local-mock-getitem-20260925.log` and `scratch/fastapi-local-real-udp-overlapped-20260925.log` runs were intentionally stopped after source changes and are not complete gate results.

Final-source verification for this continuation: The **218/218** local FastAPI gate exits zero on the exact WSAConnect/real-UDP Release binary, including actual Uvicorn HTTP and WebSocket (`scratch/fastapi-local-wsaconnect-20260925.log`). Release ALL_BUILD, CTest **53/53**, and `git diff --check` pass. The live demo at `http://127.0.0.1:8765/` runs this binary and returned XLang3 3.14.7/FastAPI-ready; its 1,000-request/16-worker load and 30-second/16-worker soak each had zero errors (`scratch/fastapi-demo-load-wsaconnect-20260925.log`, `scratch/fastapi-demo-soak-wsaconnect-20260925.log`). The unchanged FastAPI 0.141.1 upstream suite with normal assertion rewriting is running under `scratch/upstream-results-fastapi-wsaconnect-20260925` via `scratch/fastapi-upstream-wsaconnect-20260925.log`; do not claim its result until exit. The complete unchanged XLang AnyIO suite remains partial from an earlier build; the final-source full socket-module diagnostic is 435 passed/814 skipped/28 deliberately deselected. No commit or push.

2026-09-25 upstream FastAPI diagnostic and correction: The unchanged FastAPI 0.141.1 upstream suite on the prior WSAConnect build finished **3,317 passed, 17 skipped, 4 xfailed, 7 failed** (`scratch/upstream-results-fastapi-wsaconnect-20260925/fastapi.log`). Six header-param-model snapshot failures are reproduced under CPython 3.14 with the identical dependency environment: TestClient sends `Accept-Encoding: gzip, deflate, br, zstd`, while the snapshot does not admit `br`; they are shared baseline, not XLang-only. The seventh, `test_body_nested_models/test_tutorial008.py::test_post_invalid_list_item[tutorial008_py310]`, passed on CPython and failed on XLang because nested Pydantic validation yielded a raw internal `\x1findex:0` marker and a duplicated path. `modules/pydantic_core/pydantic_core_module.cpp` now converts indexed location segments to integer values both when creating and prefixing errors, and recognizes an existing integer prefix. The exact unchanged upstream case now passes (`scratch/fastapi-body-location-focused-20260925.log`). Final-source Release ALL_BUILD, CTest **53/53**, and `git diff --check` pass (`scratch/fastapi-allbuild-location-20260925.log`, `scratch/fastapi-ctest-location-20260925.log`). The exact-source local 218-case gate and full upstream rerun are active in `scratch/fastapi-local-location-20260925.log` and `scratch/upstream-results-fastapi-location-20260925/fastapi.log`; do not claim them green until exit. The demo is live on the rebuilt source at `http://127.0.0.1:8765/` and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. Goal remains active; no commit or push.

2026-09-26 gate update: The exact-source local FastAPI gate exited zero **218/218**, including real Uvicorn HTTP and WebSocket (`scratch/fastapi-local-location-20260925.log`); CTest remains **53/53** and Release ALL_BUILD passed. The demo home page returns HTTP 200 at `http://127.0.0.1:8765/`. The full unchanged FastAPI rerun is still active, so its final result must be read from `scratch/upstream-results-fastapi-location-20260925/fastapi.log` after process exit. No commit or push.

2026-09-26 final upstream comparison on this source: The unchanged FastAPI 0.141.1 suite with normal assertion rewriting finished **3,318 passed, 17 skipped, 4 xfailed, 6 failed** (`scratch/upstream-results-fastapi-location-20260925/fastapi.log`). This is one more pass and one fewer failure than the previous binary; the formerly XLang-only nested list/body error now passes in the full suite. All six remaining failure node IDs are under `test_header_param_models/test_tutorial001.py` and `test_tutorial003.py`, where the actual `Accept-Encoding` is `gzip, deflate, br, zstd` but the snapshot permits only gzip/deflate, optionally zstd. The exact same six node IDs fail under **CPython 3.14 with the same package directories** (`scratch/fastapi-header-cpython-20260926.log`); the CPython focused run also reports six inline-snapshot teardown errors, caused by those six snapshot mismatches. There is therefore no observed XLang-only failure in this unchanged FastAPI suite, but the suite does not exit zero in this dependency environment and broader ecosystem compatibility remains incomplete. Final Release ALL_BUILD, CTest **53/53**, local FastAPI **218/218**, focused nested-body upstream test, and `git diff --check` pass. Demo remains live at `http://127.0.0.1:8765/` on this binary. Goal remains active; no commit or push.

2026-09-26 AnyIO continuation: The unchanged AnyIO 4.15.1 suite with early-stop reaches **143 passed, 47 platform skips, 1 failed**; first failure is `tests/streams/test_memory.py::test_not_closed_warning[asyncio]`, where pure-Python `MemoryObjectSendStream.__del__` does not emit the expected `ResourceWarning` after `del` and `gc.collect()` (`scratch/upstream-results-anyio-current-20260926/anyio.log`). The same test passes under CPython 3.14 (`scratch/anyio-finalizer-cpython-20260926.log`). A provisional broad enablement of user-defined `__del__` in `release_last_reference` made a simple synchronous warning oracle pass but caused the unchanged async test and a reduced two-stream `asyncio.run` reproduction to exit abnormally with code 3 during event-loop cleanup; this is not safe production behavior. That provisional source edit was removed and the previously validated finalizer behavior restored, followed by a successful Release ALL_BUILD (`scratch/fastapi-allbuild-finalizer-rollback-20260926.log`). The remaining issue is a general instance-lifetime/finalizer defect, not an AnyIO-specific hook. No commit or push.

2026-09-26 native Pydantic-core continuation: The unchanged pydantic-core 2.46.5 suite advanced through successive early-stop runs from 242 passed to 413 passed to 449 passed to 514 passed on the preceding builds. Four general discrepancies were fixed on the current source. First, `repr(type)` now binds the metaclass special method for its own metaclass, matching CPython `<class 'type'>` and passing unchanged `serializers/test_any.py::test_any_model`; core fixture `type_self_metaclass_repr` and public FastAPI `type_repr_serialization_contract` have CPython-generated expected output. Second, native `SerializationInfo` no longer exposes the internal `polymorphic_serialization` option in `vars(info)` or `repr(info)`, matching pinned CPython pydantic-core 2.46.5 and passing unchanged `serializers/test_functions.py::test_function_args`; public `serialization_info_contract` is a CPython oracle. Third, JSON round-trip encoding now suppresses reapplication of the same schema's serializer throughout its own nested value traversal; unchanged `serializers/test_json.py::test_custom_serializer` and public `json_roundtrip_serializer_contract` pass against CPython output. Fourth, smart-union tuple matching now checks declared length and item schemas before choosing a branch; unchanged `serializers/test_list_tuple.py::test_tuple_wrong_size_union` and public `tuple_union_serialization_contract` pass against CPython output. Current final-source Release ALL_BUILD (`scratch/fastapi-allbuild-tuple-union-20260926.log`), CTest **53/53** including core fixtures (`scratch/fastapi-ctest-tuple-union-final-20260926.log`), and `git diff --check` pass. The final-source local FastAPI gate and next early-stop pydantic-core suite are still running (`scratch/fastapi-local-tuple-union-final-20260926.log`, `scratch/upstream-results-pydantic-core-tuple-union-final-20260926/pydantic-core.log`); do not claim their outcome until exit. Demo at `http://127.0.0.1:8765/` returns page 200 and XLang3 3.14.7/FastAPI-ready on this binary. The AnyIO finalizer gap and full ecosystem matrix remain active. No commit or push.

2026-09-26 exact final-source gate: The expanded local FastAPI gate exits zero **222/222** (`scratch/fastapi-local-tuple-union-final-20260926.log`), including the four new CPython-oracle TestClient routes and real Uvicorn HTTP/WebSocket. Release ALL_BUILD, CTest **53/53** including complete core fixtures, and `git diff --check` pass. On this same binary the demo at `http://127.0.0.1:8765/` returns page 200 and `/api/runtime` reports XLang3 3.14.7/FastAPI ready. Fresh 1,000-request/16-worker load exits zero with zero errors, and a separate 30-second/16-worker soak completed 337 requests with zero errors (`scratch/fastapi-demo-load-tuple-union-final-20260926.log`, `scratch/fastapi-demo-soak-tuple-union-final-20260926.log`). `dumpbin /dependents` on final `xlang3.exe` and `pydantic_core._pydantic_core.x3pkg.dll` lists system DLLs and no Python DLL. The unchanged pydantic-core early-stop suite advances to **578 passed, 4 skipped, 1 xfailed, 1 failed**; the next failure is `serializers/test_model.py::test_warn_on_missing_field`, where XLang3 omits a CPython `UserWarning` for a missing declared field (`scratch/upstream-results-pydantic-core-tuple-union-final-20260926/pydantic-core.log`). The exact case passes on CPython 3.14 (`scratch/pydantic-core-missing-field-cpython-20260926.log`). Full AnyIO/HTTPX/Pydantic-core coverage and user-defined finalization remain open, so the goal is active. No commit or push.

2026-09-26 tagged-union warning continuation: The missing-field warning is specific to a chosen tagged-union model branch; direct model serialization with a missing field emits no warning under either CPython or XLang3. The native tagged-union serializer now scopes its chosen branch as a union trial, allowing the existing model-field warning path to emit the CPython `UserWarning` without changing direct model behavior. The exact unchanged `serializers/test_model.py::test_warn_on_missing_field` passes (`scratch/pydantic-core-missing-field-xlang-20260926.log`), and CPython-generated public FastAPI/TestClient `tagged_union_missing_field_warning_contract` matches XLang3, including the warning category and missing-field message. Final-source Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-allbuild-tagged-warning-20260926.log`, `scratch/fastapi-ctest-tagged-warning-20260926.log`). The expanded local FastAPI gate is still running (`scratch/fastapi-local-tagged-warning-20260926.log`); do not claim its outcome until exit. The unchanged pydantic-core early-stop run advanced to **929 passed, 6 skipped, 1 xfailed**, then found an XLang-only nested-union warning aggregation mismatch in `serializers/test_union.py::test_union_of_unions_of_models_invalid_variant`; the exact test passes under CPython (`scratch/pydantic-core-nested-union-warnings-cpython-20260926.log`). That gap and the AnyIO finalizer/HTTPX suites remain open. Demo is live on current source at `http://127.0.0.1:8765/`. No commit or push.

2026-09-26 exact tagged-union final gate: The local FastAPI gate exited zero **223/223** (`scratch/fastapi-local-tagged-warning-20260926.log`), including the new warning route and real Uvicorn HTTP/WebSocket; Release ALL_BUILD, CTest **53/53** with full core fixtures, and `git diff --check` pass. The same binary serves the live demo at `http://127.0.0.1:8765/` with page 200 and XLang3 3.14.7/FastAPI-ready runtime. Fresh 1,000-request/16-worker load had zero errors (`scratch/fastapi-demo-load-tagged-warning-20260926.log`); a separate 30-second/16-worker soak completed 333 requests with zero errors (`scratch/fastapi-demo-soak-tagged-warning-20260926.log`). The unchanged pydantic-core early-stop result remains **929 passed/6 skipped/1 xfailed/1 XLang-only failure** at nested-union warning aggregation. Full ecosystem compatibility is not complete; user-defined finalization, AnyIO, HTTPX, and further upstream pydantic-core failures remain. No commit or push.

2026-09-26 complete Pydantic-core milestone: On the final Release ALL_BUILD (`scratch/fastapi-build-some-generic-20260926.log`), the complete unchanged pinned pydantic-core 2.46.5 suite with ordinary assertion rewriting exits zero: **5,794 passed, 130 skipped, 10 xfailed** out of 5,934 (`scratch/upstream-results-pydantic-core-some-generic-20260926/pydantic-core.log`, `matrix-results.json`). General native serializer fixes preserve an invalid union's original object in Python mode, raise the real unknown-type error in JSON mode, and aggregate all nested model-class warnings. A live-class registry makes cycle collection safe without an O(N^2) class-hierarchy rebuild; all six unchanged upstream GC tests pass in the full suite. Model-level before assignment callbacks see raw input, while their retained mapping reflects the validated field afterward, matching the CPython 3.14 oracle. Mixed int/IntEnum literals prefer an exact-type match regardless of schema order. Timedelta parse failures distinguish Python/JSON labels and provide structured error context. Native `pydantic_core.Some[T]` returns the `Some` class itself and supports the unchanged pattern-matching test. CPython-generated public contracts cover these behaviors (`nested_union_fallback_contract`, `model_root_assignment_before_contract`, expanded `literal_enum_identity_contract` and `timedelta_invalid_text_contract`, `some_generic_pattern_contract`). The final local FastAPI gate exits zero **226/226**, including real Uvicorn HTTP/WebSocket (`scratch/fastapi-local-some-generic-20260926.log`); CTest **53/53** (`scratch/fastapi-ctest-some-generic-20260926.log`) and `git diff --check` pass. Final binary demo is live at `http://127.0.0.1:8765/`: page 200 and runtime reports XLang3 3.14.7/FastAPI ready; 1,000 requests at 16 workers and a separate 30-second/16-worker soak (343 requests) each had zero errors (`scratch/fastapi-demo-load-some-generic-20260926.log`, `scratch/fastapi-demo-soak-some-generic-20260926.log`). This is a complete pydantic-core milestone, not full ecosystem completion: AnyIO user-defined finalization, HTTPX native cryptography/async shutdown, and the previously recorded CPython-shared six FastAPI snapshot failures remain to assess. No CPython runtime/ABI/DLL dependency, test fake, commit, or push.
2026-09-26 next-boundary audit on the same fully validated binary: A fresh unchanged AnyIO 4.15.1 early-stop run still reaches **143 passed, 47 platform skips, 1 failed**; first failure remains `tests/streams/test_memory.py::test_not_closed_warning[asyncio]` because a pure-Python memory stream's user-defined `__del__` does not emit its ResourceWarning after `del`/`gc.collect()` (`scratch/upstream-results-anyio-pydantic-core-final-20260926/anyio.log`). This is a general XLang3 finalization gap. The earlier broad finalizer experiment caused an asyncio cleanup crash and was rolled back; do not treat the Pydantic-core GC improvement as a fix for user-defined finalizers. Demo remains live on the pydantic-core-green binary. Goal remains active; no commit or push.
2026-09-26 finalization follow-up: A minimal CPython 3.14 comparison in `scratch/probe_general_finalizers.py` shows `__del__` on ordinary user instances and two AnyIO memory-stream ResourceWarnings; XLang3 currently emits neither. Enabling release finalization for every class with `__del__`, with a reference guard before binding the method, made the simple synchronous `__del__` check pass but still caused native exit code 3 when both AnyIO stream ends were released inside an asyncio task (one warning captured, rather than two). The unchanged AnyIO pytest case likewise exited 3 during collection. The experimental broad change was fully reverted and the stable runtime rebuilt. CTest passes **53/53** again (`scratch/fastapi-ctest-after-finalizer-restore-20260926.log`); the complete local FastAPI gate exits zero **226/226**, including real Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-gate-after-finalizer-restore-20260926.log`). The demo was restarted on port 8765 (PID 12284); `/api/runtime` returned 200 and XLang3 3.14.7/FastAPI ready. General finalization remains unresolved; no AnyIO-specific workaround was retained. No commit or push.
2026-09-26 general finalizer and handle milestone (supersedes the unresolved-finalization note immediately above): User-defined `__del__` now runs for ordinary instances on release, including inherited and masked methods, resurrection-once, and unraisable exceptions; CPython 3.14 comparisons in `scratch/probe_general_finalizers.py`, `scratch/probe_finalizer_semantics.py`, and `scratch/probe_unraisable_finalizer.py` match. ASAN stack traces isolated the previous native exit 3 to reentrant `sys._getframe()` during finalization: releasing a frame's retained locals while holding `live_frame_snapshots_mutex_` recursively reacquired that mutex. Frame refresh, retirement, and snapshot replacement now defer old reference releases until after unlocking. Returning Python calls force a registry-only frame prune so a temporary warnings frame does not keep an unrelated caller local alive; the exact two-warning async probe and unchanged AnyIO `test_not_closed_warning[asyncio]` pass. The public `finalizer_warning_contract` exercises both ordinary `__del__` and a real FastAPI/TestClient async route using unchanged AnyIO streams, exactly matching CPython 3.14. Prompt finalization exposed a prior `_winapi.DuplicateHandle` behavior that silently added `DUPLICATE_CLOSE_SOURCE` to inherited pipes; this has been removed along with obsolete tracking, restoring OS/Python handle ownership. The CPython-oracle `winapi_handle_contract` confirms the source pipe remains open after duplication and a real subprocess executes inside a FastAPI route. The final Release ALL_BUILD (`scratch/fastapi-build-general-finalizers-final-20260926.log`), CTest **53/53** (`scratch/fastapi-ctest-general-finalizers-final-20260926.log`), full local FastAPI gate **228/228** including Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-gate-general-finalizers-final-20260926.log`), and `git diff --check` pass. The unchanged pydantic-core 2.46.5 suite remains exit-zero **5,794 passed, 130 skipped, 10 xfailed** on the behaviorally equivalent pre-cleanup build (`scratch/upstream-results-pydantic-core-finalizer-fixed-20260926`). The unchanged AnyIO early-stop now reaches **196 passed, 64 Windows skips, 1 TLS failure/1 teardown error** (`scratch/upstream-results-anyio-finalizer-fixed-20260926`); the exact TLS certificate-verification failure is reproduced on CPython 3.14 for asyncio, eager asyncio, and trio. A diagnostic run excluding the TLS module reached **266 passed, 84 skips** before the next XLang-only failure, missing general `gc.get_objects` in `tests/test_debugging.py::test_main_task_name[asyncio]` (`scratch/anyio-next-failure-without-tls-module-20260926.log`). The production-style demo is live at `http://127.0.0.1:8765/` (PID 1664): runtime endpoint 200/XLang3 3.14.7/FastAPI ready, 1,000-request/16-worker load zero errors (`scratch/fastapi-demo-load-general-finalizers-final-20260926.log`), 30-second/16-worker soak 330 requests zero errors (`scratch/fastapi-demo-soak-general-finalizers-final-20260926.log`), and create/toggle/delete task flow pass. Full ecosystem compatibility remains open; no commit or push.

2026-09-26 GC and upstream continuation: Native `gc.get_objects()` now snapshots a non-owning live-object registry with guarded references and excludes atomic values; `gc.is_tracked` uses the same classification. A CPython-oracle FastAPI/TestClient contract (`tests/fastapi/gc_objects_contract.py`) checks live marker, list, dict, and atomic exclusions. Explicit generations and CPython's dynamic tuple untracking remain unsupported and are not claimed. Release ALL_BUILD, CTest **53/53**, and the expanded local FastAPI gate **229/229**, including real Uvicorn HTTP/HTTPS/WebSocket, passed on this build (`scratch/fastapi-build-gc-objects-20260926.log`, `scratch/fastapi-ctest-gc-objects-20260926.log`, `scratch/fastapi-gate-gc-objects-20260926.log`). The complete unchanged pydantic-core 2.46.5 suite exits zero again: **5,794 passed, 130 skipped, 10 xfailed** (`scratch/upstream-results-pydantic-core-gc-objects-20260926/pydantic-core.log`). The unchanged AnyIO `test_main_task_name[asyncio]` now passes. An exploratory unchanged AnyIO run excluding only the CPython-shared TLS test module reached **1,209 passed, 458 Windows skips, 1 xfailed** before `test_sourceless_install` failed in Windows `venv` (`scratch/anyio-next-failure-gc-objects-20260926.log`). The goal remains active; no commit or push.

2026-09-26 generic venv/archive diagnostic: Python 3.14's unmodified Windows `venv` expects a `python.exe` in the base interpreter directory. The repository's `xlang3_cli_compat` test expressly forbids that alias next to `xlang3.exe`, so no alias was retained. A temporary diagnostic copy isolated further general gaps. XLang3 startup now recognizes the Windows `__PYVENV_LAUNCHER__` path and `pyvenv.cfg`, reporting the correct virtual `sys.executable`/`sys.prefix` and base executable/prefix. The native archive loader now recognizes any valid ZIP file path, including `.whl`, preserves nested package archive/prefix metadata, and returns a real `zipimporter` from `importlib.util.find_spec`; CPython-oracle fixtures `zipimport_wheel` (including arbitrary `.data` extension) pass. Native `os.fsync` uses Windows `_commit` or POSIX `fsync` and preserves EBADF for closed descriptors; CPython-oracle `os_fsync` passes. With these changes, a fresh unchanged `ensurepip` reaches pip's wheel-install phase; the next XLang-only failure is the existing `_csv.writer` stub raising `NotImplementedError` (`scratch/venv-firstpip-ensurepip-20260926.log`). No pip, AnyIO, or FastAPI code/test was modified. The final-source Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-build-zip-vfs-final-20260926.log`, `scratch/fastapi-ctest-zip-vfs-final-20260926.log`); the final-source 229-case FastAPI gate is running at `scratch/fastapi-gate-zip-vfs-final-20260926.log` and must not be called green until exit. The unchanged AnyIO sourceless-install test is not passing; the no-alias Windows venv boundary and `_csv.writer` are open. No commit or push.

2026-09-26 live demo on the final-source binary: `http://127.0.0.1:8765/` serves from XLang3 PID 26320 (`scratch/fastapi-demo-zip-final-20260926.stdout.log`, `.stderr.log`). The page returned 200, `/api/runtime` reported `xlang3`/`3.14.7`/`ready`, and a real create/toggle/delete task API flow passed. Final-source `git diff --check` passed; the build directory has no `python.exe` alias. The full 229-case FastAPI rerun remains in progress, so its prior 229/229 result belongs to the earlier GC build, not this last archive/OS update.

2026-09-26 native CSV continuation: The prior archive/OS build's full FastAPI gate stopped at `sync_endpoint_task_lifetime` after 181 passes with one retained scheduled task and dangling thread (`scratch/fastapi-gate-zip-vfs-final-20260926.log`); the exact case subsequently passed 12/12 separate process repeats and a 100-TestClient iteration stress probe on the newer build. This nondeterministic lifetime failure is unresolved and must not be erased by a later green run. A fresh Windows `venv` diagnostic with a temporary external `python.exe` copy exposed the existing `_csv.writer` stub; native `_csv.writer`, `writerow`, and `writerows` now implement six Python 3.14 quoting modes, Unicode delimiter/quote handling, escaping, CSV errors, dynamic Python `write` attribute lookup, immutable dialect attributes, and forwarding of the sink's `write()` return. `io.StringIO.write()` now returns Unicode character count instead of UTF-8 byte count. CPython-oracle fixtures `csv_writer.py` and a real FastAPI/TestClient CSV export route `csv_writer_contract.py` match XLang3. A fresh unchanged `ensurepip` installs pip 26.2.1 inside a new XLang3 virtual environment under the temporary launcher diagnostic (`scratch/venv-csv-dynamic-ensurepip-20260926.log`); no alias remains in the actual build. Fifteen unchanged Python 3.14 `test.test_csv.Test_Csv` writer tests pass on XLang3 and CPython (`scratch/upstream-cpython-test-csv-writer-broad-immutable-20260926.log` and `-cpython-20260926.log`). The full unchanged CSV suite originally hung at `test_read_eof`; a native EOF/escape fix and CPython-oracle `csv_reader_eof.py` now make that exact test pass and the full suite terminate. The complete XLang3 suite currently has **24 failures, 27 errors, 6 skips** out of 134 (`scratch/upstream-cpython-test-csv-full-after-eof-20260926.log`), versus CPython **134 tests, 4 skips, exit zero**; broader CSV reader/dialect parity remains open. `io.StringIO` Unicode seek/tell is also still mismatched (XLang3 byte offsets), despite the corrected `write()` return count. Final-source Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-build-csv-eof-escape-20260926.log`, `scratch/fastapi-ctest-csv-eof-final-20260926.log`). The expanded local FastAPI gate, now 230 cases, is running at `scratch/fastapi-gate-csv-eof-final-20260926.log` and has no final result yet. The production demo was stopped for rebuilding and needs restart on the final build. No commit or push.

2026-09-26 CSV final-source gate and demo: The exact CSV EOF-source local FastAPI gate exited zero **230/230**, including real Uvicorn HTTP/HTTPS and WebSocket (`scratch/fastapi-gate-csv-eof-final-20260926.log`). The previously intermittent `sync_endpoint_task_lifetime` case passed in this run but remains an open reliability issue because the earlier exact failure is recorded. The demo is live again at `http://127.0.0.1:8765/` under XLang3 PID 3356 (`scratch/fastapi-demo-csv-eof-20260926.stdout.log`, `.stderr.log`): page 200, runtime endpoint reports XLang3 3.14.7/FastAPI ready, and real create/toggle/delete task API flow passed. CTest remains **53/53** on this source. Broader unchanged Python 3.14 CSV reader/dialect tests, full AnyIO/HTTPX suites, no-alias Windows venv, and repeat full upstream matrix remain open. No commit or push.

2026-09-26 complete upstream CSV milestone: Native `_csv.reader` now accepts `dialect=` and CPython 3.14 dialect overrides/errors; handles blank records, `QUOTE_NONNUMERIC`/`QUOTE_STRINGS`/`QUOTE_NOTNULL`, strict malformed quotes, escaped multiline unquoted fields, and safe reentrant iteration. Native `_csv.Dialect` rejects copy/pickle; native `array('w')` stores and returns Unicode code points; general `_sre` normalization prioritizes a start anchor over an equivalent deferred-lookbehind alternative, restoring unmodified `csv.Sniffer` doubled-quote detection. CPython-oracle core fixtures `csv_reader_dialect_keyword`, `array_w_unicode`, and `regex_lookbehind_anchor_alternative` are registered in both core runners. Real FastAPI/TestClient CSV reader/writer routes cover parsing, blank lines, Sniffer, dialect copy, and Unicode array export. The **unmodified Python 3.14 `test.test_csv` suite exits zero: 134 tests, 6 skips** (`scratch/upstream-cpython-test-csv-full-sre-anchor-20260926.log`) against a CPython 3.14 reference of 134 tests, 4 skips (`scratch/upstream-cpython-test-csv-full-cpython-20260926.log`); XLang3's two extra skips are CPython-specific implementation-detail tests, while four shared skips require `sys.gettotalrefcount`. Release ALL_BUILD (`scratch/fastapi-build-sre-anchor-alt-20260926.log`), CTest **53/53** (`scratch/fastapi-ctest-csv-full-green-20260926.log`), and `git diff --check` pass. The expanded **231-case** local FastAPI gate is running on this exact final build at `scratch/fastapi-gate-csv-full-green-20260926.log`; do not claim its result until exit. The demo remains stopped for rebuilding. Pinned full AnyIO/HTTPX suites, no-alias Windows venv, current-build pydantic-core rerun, and full upstream matrix remain open. No commit or push.

2026-09-26 timed TLS and package-install diagnostic: The preceding CSV build's full FastAPI gate subsequently exited zero **231/231** (`scratch/fastapi-gate-csv-full-green-20260926.log`), and the production demo passed page/runtime/task-flow checks, 1,000 requests with 16 workers (zero errors), and a separate 30-second soak with zero errors (`scratch/fastapi-demo-load-csv-full-20260926.log`, `scratch/fastapi-demo-soak-csv-full-20260926.log`). A local real TLS server exposed a general native `_ssl` gap: timed sockets surfaced `SSLWantReadError` instead of waiting for the handshake/read/write readiness as CPython does. `modules/net/ssl_module.cpp` now uses the socket timeout and OS readiness wait for OpenSSL WANT_READ/WRITE, retaining MemoryBIO nonblocking behavior. The CPython-oracle `ssl_timed_socket` core fixture and real FastAPI `ssl_timed_socket_contract` pass, as does the existing trustme TLS contract. External `urllib.request.urlopen('https://pypi.org/simple/setuptools/')` now returns HTTP 200 with a timed socket under XLang3 and under a diagnostic XLang3 virtual environment. The final SSL-source Release ALL_BUILD (`scratch/fastapi-build-ssl-timed-io-20260926.log`), CTest **53/53** (`scratch/fastapi-ctest-ssl-timed-20260926.log`), and `git diff --check` pass. The expanded **232-case** FastAPI gate is running (`scratch/fastapi-gate-ssl-timed-20260926.log`); the demo is stopped during this validation and needs restart. Unmodified pip install in the diagnostic venv still fails during package candidate resolution (`scratch/venv-pip-setuptools-direct-20260926.log`), and the temporary `python.exe` launcher alias remains outside the actual build; neither general pip install nor upstream AnyIO sourceless-install is claimed. Full AnyIO/HTTPX matrix and current-build pydantic-core rerun remain open. No commit or push.

2026-09-26 timed TLS final gate and live demo: The final-source expanded FastAPI gate exited zero **232/232**, including real Uvicorn HTTP, HTTPS, and WebSocket (`scratch/fastapi-gate-ssl-timed-20260926.log`); CTest remains **53/53**. The production-style demo is live again at `http://127.0.0.1:8765/` under XLang3 PID 27368: page 200, `/api/runtime` reports XLang3 3.14.7/FastAPI ready, and create/toggle/delete task flow returned 201/200/204. The unresolved venv pip install now has a smaller generic reproduction: unchanged pip-vendored Requests `Request('GET', 'http://example.com/').prepare()` sometimes causes a native Windows access violation, while the same code with line tracing enabled completes. This occurs before network I/O and is not a FastAPI-specific issue (`scratch/venv-requests-prepare-probe-20260926.log`, `scratch/venv-requests-trace-only-20260926.log`). The diagnostic scripts only instrument unchanged pip/Requests at runtime; no upstream source was edited. Full current-build Pydantic-core and AnyIO/HTTPX upstream suites, no-alias Windows venv, and resolution of the intermittent task-lifetime failure remain open. No commit or push.

2026-09-26 generic value-reference fix and next native boundary: `Value::operator=(const Value&)` now retains the source before releasing the previous destination value. The old order could free an object that the source borrowed when `str(url)` and `url.lstrip()` both returned the same string. An untraced seven-line pure-Python reproduction previously exited with Windows access violation; it now exits zero, as does unchanged pip-vendored `requests.Request('GET', 'http://example.com/').prepare()`. CPython-oracle `value_alias_assignment` core and public FastAPI/TestClient contracts are registered and match on HTTP, whitespace-prefixed HTTPS, and opaque URLs. Release ALL_BUILD (`scratch/fastapi-build-value-alias-full-20260926.log`), CTest **53/53** (`scratch/fastapi-ctest-value-alias-20260926.log`), and `git diff --check` pass. The expanded **233-case** FastAPI gate is running (`scratch/fastapi-gate-value-alias-20260926.log`); the demo was stopped for rebuilding and needs restart after this gate. Unmodified CPython 3.14 pip 26.2.1 resolves PyPI index versions in this environment, whereas XLang3 now advances to pip's unchanged Windows truststore and fails at `CertCreateCertificateContext.restype = POINTER(CERT_CONTEXT)`: generic native `ctypes` currently rejects typed pointer return values. A smaller CPython oracle with `GetCommandLineW.restype = POINTER(c_wchar)` confirms the same native boundary. No upstream Python source was modified or replaced. Current-build Pydantic-core, complete AnyIO/HTTPX matrix, no-alias Windows venv, and the intermittent task-lifetime failure remain open. No commit or push.

2026-09-26 value-reference final gate and demo: The Release source's complete local FastAPI gate exited zero **233/233**, including the new alias regression and real Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-gate-value-alias-20260926.log`). CTest remains **53/53** and `git diff --check` passes. The production demo is live at `http://127.0.0.1:8765/` under XLang3 PID 12848: page 200, runtime XLang3 3.14.7/FastAPI ready, and task create/toggle/delete 201/200/204. Under concurrent live traffic, 1,000 requests/16 workers returned zero errors (`scratch/fastapi-demo-load-value-alias-20260926.log`); a separate 30-second/16-worker soak returned 321 requests/zero errors (`scratch/fastapi-demo-soak-value-alias-20260926.log`). The unchanged full pydantic-core suite on this exact build is running at `scratch/upstream-results-pydantic-core-value-alias-20260926/pydantic-core.log`; do not claim completion before its exit. Native `ctypes` layout is also generally incomplete: a `Structure` with `c_char` then `c_int` has CPython size/alignment 8/4, while XLang3 reports 5/5 for list `_fields_` and 0/1 for tuple `_fields_` (`scratch/probe_ctypes_layout.py`). The Windows truststore needs both correct aligned layout and typed pointer returns/dereferencing, so pip install and AnyIO's sourceless-install remain open. No commit or push.

2026-09-26 pydantic-core assertion-setting correction: The first current-build full run used the matrix runner's default `--assert=plain` and ended **5,791 passed, 130 skipped, 10 xfailed, 3 failed** after 559 seconds. The three failures all expect pytest-generated assertion text from root validators. The exact focused test also fails under CPython 3.14 with `--assert=plain` (`scratch/pydantic-root-single-cpython-20260926.log`), so these are a test-runner configuration mismatch, not evidence of a new runtime regression. With `--assert=rewrite`, the three exact XLang3 cases pass (`scratch/pydantic-root-three-rewrite-xlang-20260926.log`). A complete rerun with `-RewriteAssertions` is in progress at `scratch/upstream-results-pydantic-core-value-alias-rewrite-20260926/pydantic-core.log`; await final exit and count. The upstream tests themselves remain unchanged. No commit or push.

2026-09-26 completed current-build pydantic-core: The unchanged pinned pydantic-core 2.46.5 suite exited zero on the value-reference build with the required assertion rewriting: **5,794 passed, 130 skipped, 10 xfailed** in 600.28 seconds (`scratch/upstream-results-pydantic-core-value-alias-rewrite-20260926/pydantic-core.log`, `scratch/upstream-pydantic-core-value-alias-rewrite-20260926.log`). This supersedes the plain-assertion configuration mismatch above. The same source has Release ALL_BUILD, CTest **53/53**, FastAPI **233/233**, real demo load/soak zero errors, and `git diff --check` green. The goal remains active for generic native `ctypes` pointer and structure layout support, upstream AnyIO/HTTPX completion, Windows venv packaging, and the historical intermittent lifetime failure. No commit or push.

2026-09-26 ongoing native `ctypes` continuation: Generic aligned structure/union layout, tuple `_fields_`, typed pointer return and dereference, pointer-to-pointer out-parameter readback, array initialization/indexing, `cast` to pointer/`c_void_p`, and C-function `errcheck` callbacks have been implemented in `modules/ctypes/ctypes_ffi.cpp` and `src/runtime/modules/system/ctypes_module.cpp`. New CPython-oracle core fixtures and public FastAPI contracts for structure layout and pointer returns are registered; focused fixtures pass. Latest Release `xlang3` target builds, `git diff --check` passes, and focused native cast/errcheck probes pass. Unchanged pip 26.2.1 now passes the former truststore pointer/cast failures and fetches PyPI index content, but fails while cleaning a package URL in `pip._internal.models.link._clean_url_path` with `AttributeError: object has no attribute 'upper'` (`scratch/pip-index-xlang-ctypes-cast-errcheck-20260926.log`). The CPython 3.14 baseline succeeds. Diagnostic source instrumentation is only in `scratch/`; no upstream Python source changed. **Do not claim secure/complete TLS compatibility yet:** `store_structure` still ignores nested structure, array, and pointer/string fields, including truststore's `SSL_EXTRA_CERT_CHAIN_POLICY_PARA.pwszServerName`; hostname verification may therefore be incomplete. Full ALL_BUILD, CTest, expanded FastAPI gate (now 235 cases), Pydantic-core, and live demo on this ctypes source still need reruns. The demo was stopped for rebuilding. No commit or push; goal remains active.

2026-09-26 Windows truststore and `zip_longest` milestone: Native `ctypes` marshalling now recursively writes nested structures and arrays, retains backing strings, decays an array used as a typed pointer field, reads wide-character output arrays, and supports `c_wchar_p` null and numeric values. The actual unchanged truststore path now accepts a locally generated CA-signed `localhost` certificate and rejects `wrong.test`, matching CPython 3.14; the public FastAPI/TestClient contract `ctypes_truststore_policy_contract` passes and is registered. `_SSLContext.get_ca_certs(binary_form=True)` now accepts the genuine keyword call needed for truststore fallback; an initial no-keyword regression was corrected and focused `xlang3_cli_ssl_module` CTest passes. The pip URL failure came from native `itertools.zip_longest` eagerly collecting each iterator separately, breaking pip's `pairwise(iterable)` with the same iterator supplied twice; it is now a lazy native iterator, and CPython-oracle core plus public FastAPI regressions pass. On the clean Release build, `pip index versions sniffio` passed **10/10** independent no-cache PyPI queries with normal TLS and the localhost/wrong-host truststore policy passed **8/8** independent runs. Release ALL_BUILD, the complete core fixture run, CTest **53/53**, and `git diff --check` pass. The expanded FastAPI gate is still running at `scratch/fastapi-gate-ctypes-zip-longest-20260926.log`; do not claim its result until exit. Unmodified pip `install --dry-run` now progresses through index retrieval but fails in generic `max(candidates, key=...)` because the built-in used a non-runtime-aware comparison for tuple keys containing `packaging.Version` objects. A general `runtime_value_compare` fix and CPython-oracle/public FastAPI regressions have been prepared in source but **are not yet linked or validated**; the active FastAPI gate holds the DLL open. No commit or push; goal remains active.

2026-09-26 pip wheel installation and current verification: The earlier FastAPI gate was intentionally stopped after **103 passing cases** because the source changed and its DLL was locked during linking; it is not a complete gate result. Built-in `min`/`max` now use runtime-aware rich comparison and truth testing for custom objects inside tuple keys; the core CPython oracle and public FastAPI route pass. Unchanged CPython 3.14 pip 26.2.1 under XLang3 completed a real `pip install --dry-run --no-deps sniffio==1.3.1` with trusted PyPI TLS, matching the CPython oracle, then performed a real `pip install --target scratch/pip-install-sniffio-20260926` of the unmodified wheel and imported `sniffio` from the installed target with metadata version 1.3.1 (`scratch/pip-dryrun-sniffio-minmax-20260926.log`, `scratch/pip-install-sniffio-xlang-20260926.log`). Release ALL_BUILD passes on this source (`scratch/fastapi-allbuild-minmax-runtime-compare-20260926.log`); complete core fixtures, CTest, and expanded FastAPI gate are running at `scratch/fastapi-core-fixtures-minmax-20260926.log`, `scratch/fastapi-ctest-minmax-20260926.log`, and `scratch/fastapi-gate-minmax-20260926.log`. Do not claim their results before exit. Full unchanged Pydantic-core and AnyIO/HTTPX matrix need final-source reruns, and the demo remains stopped. No commit or push; goal remains active.

2026-09-26 expanded installed-wheel and matrix probe: On the min/max Release source, complete core fixtures and CTest **53/53** exited zero. Unchanged pip then installed pinned AnyIO 4.15.1 with idna 3.20/typing_extensions 4.16.0 into an isolated target, and pinned HTTPX 0.28.1 with its unmodified pure-Python dependencies into a second target (`scratch/pip-install-anyio-xlang-20260926.log`, `scratch/pip-install-httpx-xlang-20260926.log`); XLang3 imported AnyIO from the new install and read its installed metadata. A direct installed-HTTPX HTTPS request to PyPI fails issuer verification under both XLang3 and CPython 3.14 in this host environment, so it is a shared baseline observation, not a green HTTPS assertion. The exact-source expanded local FastAPI gate is running (`scratch/fastapi-gate-minmax-20260926.log`), as are unchanged upstream Pydantic-core (`scratch/upstream-results-pydantic-core-minmax-20260926`) and HTTPX (`scratch/upstream-results-httpx-minmax-20260926`) suites. The HTTPX checkout was initially rejected as dirty due only to an untracked 54-byte OpenSSL TLS secrets log named `test`, generated by an older run; it was moved into `scratch/httpx-generated-tls-secrets-20260925.log`, leaving the pinned checkout clean before the new upstream run. No commit or push; goal active.

2026-09-26 complete HTTPX upstream diagnostic: The unchanged pinned HTTPX 0.28.1 suite on the min/max Release binary finished **1,411 passed, 1 skipped, 3 failed, 3 errors** (`scratch/upstream-results-httpx-minmax-20260926/httpx.log`, `matrix-results.json`). All three failures are shared CPython 3.14 baseline behavior in this pinned dependency environment: text-mode multipart upload fails to raise `TypeError`, and two caplog assertions see Uvicorn access records in addition to HTTPX records; exact CPython focused runs reproduce all three. The three XLang-only errors are one fixture reused by encrypted-key configuration tests, where unmodified cryptography `default_backend()` imports `cryptography.hazmat.bindings._rust._openssl` and XLang3's true native cryptography package does not yet export a functioning `_openssl` CFFI/OpenSSL binding. A focused CPython run of the first encrypted-key test passes. Do not skip or fake this native boundary; the full HTTPX matrix remains failed until real support is implemented. The current local FastAPI gate and Pydantic-core upstream suite remain running. No commit or push; goal active.

2026-09-26 final-source Pydantic-core and AnyIO baseline diagnostic: The unchanged pinned Pydantic-core 2.46.5 upstream suite completed on the min/max Release binary with required pytest assertion rewriting: **5,794 passed, 130 skipped, 10 xfailed**, exit zero in 613.97 seconds (`scratch/upstream-results-pydantic-core-minmax-20260926/pydantic-core.log`). The full unchanged AnyIO 4.15.1 run collected 4,236 cases and stopped at the configured 20-failure threshold: **205 passed, 70 skipped, 10 failed, 10 errors** (`scratch/upstream-results-anyio-minmax-20260926/anyio.log`). All first ten failures and their teardown errors occur in `tests/streams/test_tls.py::TestTLSStream` from local certificate issuer validation and server EOF. The first exact case reproduces on CPython 3.14, and a CPython run of the entire TLS class with a 30-failure cap produced **15 failed, 9 passed, 7 skipped, 15 errors** (`scratch/anyio-tls-class-cpython-minmax-20260926.log`). This is a shared host/dependency baseline issue, not evidence that all AnyIO tests pass. A separate diagnostic run deselecting that class is in progress at `scratch/upstream-results-anyio-baseline-skip-20260926/anyio.log`; upstream source remains unchanged. The expanded local FastAPI gate remains in progress. No commit or push; goal active.

2026-09-26 final-source local gate: The expanded real FastAPI/TestClient and Uvicorn HTTP/HTTPS/WebSocket gate exited zero **238/238** on the same min/max Release build (`scratch/fastapi-gate-minmax-20260926.log`). Together with the complete core fixture run, CTest **53/53**, and the completed Pydantic-core suite above, this establishes the current local regression baseline. The separate AnyIO diagnostic remains running. The production demo has not yet been restarted or load/soak tested on this exact build. No commit or push; goal active.

2026-09-26 production demo on final-source build: The production-style `tests/fastapi/demo_website.py` was restarted as XLang3 PID 21988 on `http://127.0.0.1:8765/`. Actual HTTP page and `/api/runtime` returned 200 with XLang3 3.14.7/FastAPI ready; create/toggle/delete task flow returned 201/200/204 and left no tasks. The 1,000-request/16-worker load completed with **zero errors** (`scratch/fastapi-demo-load-minmax-20260926.log`), followed by a 30-second/16-worker soak of 317 requests with **zero errors** (`scratch/fastapi-demo-soak-minmax-20260926.log`). These timings include contention with the full AnyIO diagnostic and are not standalone benchmarks. `git diff --check` passes. The AnyIO diagnostic has progressed beyond its baseline TLS class, but other TLS-module cases also fail and their baseline status remains to verify. No commit or push; goal active.

2026-09-26 Windows venv and importlib continuation: A focused unchanged AnyIO `test_sourceless_install` failed because the unmodified Python 3.14 Windows `venv` redirector seeks `python.exe` in the base executable directory and the build shipped only `xlang3.exe` (`scratch/anyio-sourceless-minmax-20260926.log`). The Windows Release build now copies the identical XLang3 executable as `python.exe` and the CLI compatibility CTest verifies that alias reports `sys.implementation.name == 'xlang3'`; SHA-256 hashes of both executables match. The unchanged AnyIO test then advanced through venv creation, `ensurepip`, and pip's source-build dependency install, failing on a genuine generic importlib gap: native `_frozen_importlib.spec_from_loader` rejected the standard `origin=` keyword (`scratch/anyio-sourceless-with-entry-minmax-20260926.log`). Native `spec_from_loader` now accepts `origin`/`is_package`, rejects unexpected/extra positional arguments with TypeError, and produces CPython-matching package metadata for an ordinary loader. New core oracle `importlib_spec_loader_keywords` and public FastAPI `importlib_spec_loader_contract` match CPython 3.14 directly. Final-source Release ALL_BUILD and CTest **53/53** pass (`scratch/fastapi-allbuild-spec-loader-final-20260926.log`, `scratch/fastapi-ctest-spec-loader-20260926.log`). A fresh focused unchanged AnyIO sourceless test, complete fixture runner, and expanded FastAPI gate are running on this final source; do not claim their results yet. The demo was stopped for linking and needs restart. The prior broad AnyIO diagnostic and four-project matrix were intentionally interrupted to rebuild after their source became stale; neither is a completed suite result. No commit or push; goal active.

2026-09-26 generic math and nested-exception continuation: With the Windows entry point and keyword importlib fix, unchanged pip's isolated build reaches latest unmodified setuptools 84.0.0 and its vendored `more_itertools`. Direct import against preserved genuine build dependencies exposed missing native math functions. `src/builtins/math_module.cpp` now implements `comb`, `perm`, `factorial`, `log1p`, `prod`, and integer `isqrt`; CPython-oracle `math_combinatorics` covers arbitrary-large results, `__index__`, product start, domain/type errors, and public FastAPI/TestClient `math_combinatorics_contract` matches CPython. Unchanged `import setuptools.build_meta` now succeeds on XLang3 against the preserved build environment. The next unchanged AnyIO sourceless-install attempt reached actual setuptools_scm metadata validation but wrongly rejected AnyIO's valid `dynamic = ["version"]` with a required-version error. A direct CPython comparison and a line trace of untouched generated setuptools validation code located a general XLang3 compiler bug: `break` inside a nested `try` left a stale exception handler, so a later exception bypassed the proper outer handler. `src/sema/lower.cpp` now unwinds the try handlers belonging to a loop on `break` and `continue`; the new `try_loop_exception_scope` core and public FastAPI regression match CPython. XLang3's private bytecode magic and IR codec version advanced to invalidate cached code compiled before this lowering correction. The untouched setuptools schema validator now returns `True` on XLang3, as on CPython. The exact-source Release ALL_BUILD passes (`scratch/fastapi-allbuild-try-loop-magic-20260926.log`); CTest, core fixtures, local FastAPI gate, and a fresh unchanged AnyIO sourceless-install test are running on this source. **Do not claim the AnyIO install passes before its exit.** Some advanced native math semantics, including custom-object multiplication in `math.prod`, remain unverified; the full FastAPI ecosystem matrix is still open. No commit or push.

2026-09-26 native module descriptor continuation: The unchanged AnyIO sourceless-install test progressed through venv creation, isolated build requirements, setuptools-scm metadata, and wheel file writing, then failed when vendored wheel called its class attribute `_default_algorithm = hashlib.sha256` through an instance: XLang3 bound the native module function and passed an extra `self`, unlike CPython (`scratch/anyio-sourceless-try-loop-magic-20260926.log`). `src/import/native_package_loader.cpp` now marks actual module-level native package functions non-binding, matching the existing runtime module builder and CPython built-in function behavior. CPython-oracle `native_module_function_binding` and a real FastAPI/TestClient route match on exact SHA-256 and attribute identity. Full Release ALL_BUILD passes (`scratch/fastapi-allbuild-native-function-binding-20260926.log`); current-source CTest, complete core fixture runner, local FastAPI gate, and focused unchanged AnyIO sourceless-install test are running. The old local FastAPI gate was intentionally stopped at a partial count for this rebuild and is not green evidence. The production demo remains stopped for linking. No commit or push; goal active.

2026-09-26 unchanged AnyIO sourceless-install success: A subsequent attempt reached wheel building but collided with an ignored `build/bdist.win-amd64/wheel/anyio-4.15.1.dist-info` directory left by prior failed builds. The pinned upstream checkout's tracked files were clean. After verifying both absolute paths under `D:\CantorAI\xlang3\scratch`, the generated `build` tree was moved to `scratch/anyio-build-artifacts-20260926` for preservation; upstream source was not edited. One explicit SourcelessFileLoader path still checked a much older XLang private bytecode magic, so `src/runtime/modules/system/importlib_module.cpp` now checks the same current magic as the rest of the loader. Release ALL_BUILD (`scratch/fastapi-allbuild-bytecode-loader-final-20260926.log`), complete core fixture runner (`scratch/fastapi-core-fixtures-bytecode-loader-final-20260926.log`), and CTest had passed 52/53 cases and its final debugpy launch smoke was still running (`scratch/fastapi-ctest-bytecode-loader-final-20260926.log`); do not claim complete CTest until exit. On this exact build, the **unchanged pinned AnyIO `tests/test_lazyimport.py::test_sourceless_install` passed** in 59.09 seconds (`scratch/anyio-sourceless-bytecode-loader-final-20260926.log`): real Windows venv/ensurepip, pip source build and install, source removal, and sourceless import. The expanded local FastAPI gate is running, and a diagnostic full AnyIO run deselecting the CPython-shared TLS module has started; neither full suite is yet green. The demo is stopped pending test completion. No commit or push; goal active.

2026-09-26 current-build gate and debugpy check: Release ALL_BUILD and complete core fixture runner exited zero on the bytecode-loader source. The expanded local FastAPI gate exited zero with 242/242 cases, including Uvicorn HTTP/WebSocket (`scratch/fastapi-gate-bytecode-loader-final-20260926.log`). CTest's first complete run passed 52/53; the Visual Studio debugpy launch smoke timed out waiting for `initialized` while an adapter server disconnected during concurrent heavy upstream runs (`scratch/fastapi-ctest-bytecode-loader-final-20260926.log`, `build/Testing/Temporary/LastTest.log`). The same final test passed isolated in 4.53s (`scratch/fastapi-ctest-debugpy-isolated-final-20260926.log`); rerun the complete CTest gate without concurrent heavy suites before claiming 53/53. The unchanged AnyIO suite is running as a diagnostic with the CPython-shared TLS module deselected, and a new five-project upstream matrix is running on this build. An attempted matrix launch with a relative outer logging path lost its launcher while an orphan pytest child kept running; that child was stopped and the matrix restarted with absolute log/result paths. That abandoned attempt is not suite evidence. No commit or push; goal active.

2026-09-26 compiler/process and matrix checkpoint: The unchanged AnyIO no-TLS diagnostic passed the actual sourceless-install test again, then stopped at 41% in tests/test_pytest_plugin.py. Focused test_plugin failed on XLang3 because a generator expression in a class method lost the class's private-name mangling (SysModulesSnapshot.__preserve); a CPython 3.14 focused run with unrelated plugins disabled passed. src/sema/lower.cpp now carries the active private class name into generator lowering. The next unchanged plugin test hung in a spawned child because os.chdir updated XLang3's virtual working directory but native _winapi.CreateProcess inherited the host process's original directory; the child scanned the entire repository. Native process creation now passes the virtual cwd when no explicit cwd is given. New core CPython-oracle fixtures private_generator_name_mangling and subprocess_inherits_chdir, public FastAPI/TestClient contracts, and two unchanged AnyIO plugin cases pass on the rebuilt binary. A separate discovered native math.prod gap for user-defined __mul__/__rmul__ and NotImplemented was repaired and covered by expanded CPython-oracle/FastAPI contracts. The XLang3 private pyc magic and IR codec version advanced again so existing cached generator bytecode cannot bypass the compiler fix. Exact-source Release ALL_BUILD and complete core fixture gate pass (scratch/fastapi-allbuild-genexpr-magic-20260926.log; scratch/fastapi-core-fixtures-genexpr-magic-final-20260926.log). The local FastAPI gate and unchanged Uvicorn matrix remain running.

The prior-source five-project matrix exposed: FastAPI process exit -1073740940 (Windows heap corruption) at ~11% during concurrent suites, while isolated unchanged test_frontend.py passed 93/93; this is unresolved and cannot be called a FastAPI suite pass. Starlette passed 1052, skipped 8, deselected 3 documented CPython-shared Windows cases, xfailed 2 (scratch/upstream-results-five-current2-20260926/starlette.log). Pydantic could not collect because that matrix invocation omitted preserved scratch/pydantic-test-deps; Uvicorn likewise omitted preserved upstream httptools wrapper. Pydantic-core completed 5791 passed, 130 skipped, 10 xfailed, 3 failed solely because the matrix forced --assert=plain: all three focused failures pass with pytest assertion rewriting (scratch/pydantic-core-three-rewrite-20260926.log). Pydantic JSON-schema under --assert=plain failed five cases identically on CPython 3.14 and XLang3 due pytest's missing assertion-rewrite state; with rewriting on the current XLang3 build it passed 534, skipped 1, xfailed 1 (scratch/pydantic-jsonschema-rewrite-final-20260926.log). The matrix manifest/runner now automatically supplies Pydantic's preserved test dependencies, Uvicorn's hash-matched unmodified httptools Python wrapper, and assertion rewriting for Pydantic/pydantic-core, recording these choices in results. Re-run every suite on the final source before claiming complete compatibility. No commit or push; goal active.

2026-09-26 status check: The first complete local FastAPI gate on the latest generator/magic source stopped after 196 contracts at an intermittent `sync_endpoint_task_lifetime` mismatch: one completed AnyIO portal task and one extra dangling thread were still retained (`scratch/fastapi-gate-genexpr-magic-final-20260926.log`). Repeated isolated runs usually pass, but a scratch pure-AnyIO portal probe also reproduced retention on XLang3, while its CPython comparison ran 100 cycles clean. This is an unresolved general lifetime/concurrency issue; do not count the latest local gate as green. The unchanged Uvicorn suite completed in an isolated Windows console: **1004 passed, 342 skipped, 5 failed, 1 setup error** (`scratch/upstream-uvicorn-isolated-final-20260926.log`). All six nonpassing cases are in `tests/test_ssl.py`: five client handshake failures report inability to get a local issuer certificate, and encrypted-key setup cannot import native `cryptography.hazmat.bindings._rust._openssl`. These need CPython 3.14 baseline comparison and general TLS/native-cryptography repair; no suite pass is claimed. Current estimate of the *entire* production FastAPI compatibility goal is about 65%, reflecting the green Release/core gates and substantial upstream coverage but open full-matrix, cleanup reliability, and final demo verification. No commit or push.

2026-09-27 baseline and gate continuation: The same untouched Uvicorn `tests/test_ssl.py` under CPython 3.14.7, the same local dependency paths, and the same pytest configuration produced **6 failed, 5 passed**; all six failures were `CERTIFICATE_VERIFY_FAILED: unable to get local issuer certificate` (`scratch/uvicorn-ssl-cpython-baseline-20260927.log`). Thus the five XLang3 handshake failures overlap with a CPython-shared local test environment failure; the sixth XLang3 case fails earlier during fixture setup because native cryptography `_rust._openssl` is absent. Do not call the Uvicorn suite green or assume the sixth case is XLang3-compatible. The exact latest Release build passed isolated complete CTest **53/53** (`scratch/fastapi-ctest-genexpr-magic-isolated-20260927.log`). A new complete local FastAPI gate is running alone at `scratch/fastapi-gate-genexpr-magic-isolated-20260927.log`; await actual exit. No commit or push.

2026-09-27 local gate completion: The entire current-source local FastAPI gate exited **0**, including the new math/private-generator/subprocess contracts, `sync_endpoint_task_lifetime`, real Uvicorn HTTP, and real Uvicorn WebSocket (`scratch/fastapi-gate-genexpr-magic-isolated-20260927.log`). This is one passing run; prior intermittent cleanup reproduction remains an open reliability issue. Together with current-source full Release ALL_BUILD, core fixtures, and isolated CTest 53/53, local gates are green on this build. Corrected unchanged upstream Pydantic-core suite is now running with assertion rewriting at `scratch/upstream-pydantic-core-final-20260927.log`; no result yet. No commit or push.

2026-09-27 pydantic-core result: The complete unchanged pinned Pydantic-core 2.46.5 suite now passes on the current XLang3 Release build with **5794 passed, 130 skipped, 10 xfailed, 0 failed** (5934 collected; `scratch/upstream-pydantic-core-final-20260927.log` and `scratch/upstream-results-pydantic-core-final-20260927/matrix-results.json`). The matrix used normal pytest assertion rewriting, as declared in its manifest. This resolves the prior three assertion-mode artifacts, not by deselection or source edits. Full upstream Pydantic remains to run. No commit or push.

2026-09-27 upstream Pydantic run: The corrected matrix started the unchanged pinned Pydantic 2.13.5 suite on the current Release XLang3 build with preserved `scratch/pydantic-test-deps` and assertion rewriting. It collected **6091** tests and was running at about 2% (`scratch/upstream-pydantic-final-20260927.log`, live exec session 19991 at this checkpoint). This is a verified live run, not suite pass evidence; await terminal exit and results JSON. No commit or push.

2026-09-27 Pydantic baseline checkpoint: The unchanged full Pydantic suite reported five failures in `tests/test_deprecated_fields.py` at 21%. A focused run of that exact file under CPython 3.14.7 with the same pinned local dependency paths and pytest configuration produced the identical five failing nodes and seven passes (`scratch/pydantic-deprecated-fields-cpython-20260927.log`). These are CPython-shared baseline failures caused by deprecation warnings not being emitted in this local stack, not yet evidence of XLang3-specific incompatibility. The full XLang3 suite remains live beyond 32%; await the complete summary and compare any further failures. No source change, commit, or push.

2026-09-27 SerializationInfo boundary: The first full unchanged Pydantic run reached ~97%, then hit its `--maxfail=20` threshold with **5674 passed, 261 skipped, 26 xfailed, 20 failed** (`scratch/upstream-pydantic-final-20260927.log`). Five are the CPython-shared deprecation-warning failures above. The other 15 are variants of `tests/types/test_dataclass.py::test_polymorphic_serialization_with_model_serializer` failing because native `SerializationInfo` lacked its `polymorphic_serialization` property. The same unchanged 36-test dataclass file passes on CPython 3.14 (`scratch/pydantic-dataclass-cpython-20260927.log`). Native Pydantic-core now preserves the existing call option on each info instance and exposes it as a property without adding a field to `vars(info)` or changing its repr; the existing public FastAPI `serialization_info_contract` now checks `None/True/False` against a CPython oracle. Current-source Release ALL_BUILD passes (`scratch/fastapi-allbuild-serialization-info-20260927.log`); the FastAPI contract matches CPython, the unchanged dataclass file passes **36/36**, and the rest of `tests/types` passes **143/143** (`scratch/pydantic-dataclass-xlang-after-info-20260927.log`, `scratch/pydantic-types-xlang-after-info-20260927.log`). A full unchanged Pydantic rerun on this source is active at `scratch/upstream-pydantic-info-final-20260927.log`, exec session 12099; await exit before claiming whole-suite status. No commit or push.

2026-09-27 pytest runner correction: The five `tests/test_deprecated_fields.py` failures on CPython and XLang3 were caused by using local pytest 9.1.1 with these untouched upstream tests. The same file passes **12/12 on CPython 3.14.7** and **12/12 on XLang3** with pytest 8.4.2 installed separately in `scratch/pytest8-test-runner` (`scratch/pydantic-deprecated-fields-cpython-pytest8-20260927.log`, `scratch/pydantic-deprecated-fields-xlang-pytest8-20260927.log`). The Pydantic matrix entry now prepends that test-runner path, declares exact `pytest_version: 8.4.2`, and the generic matrix runner validates/records the actual pytest version. The previously started full pytest-9 rerun was intentionally stopped after reproducing the same five failures at 21%, because its harness was superseded; it is **not** suite outcome evidence. A fresh complete unchanged Pydantic run with the pinned pytest-8 harness is active at `scratch/upstream-pydantic-pytest8-final-20260927.log`, exec session 41016; await exit. No upstream test source change, commit, or push.

2026-09-27 corrected full Pydantic progress: The live 6091-test upstream run confirms pytest **8.4.2** in its startup banner and passes all 12 `tests/test_deprecated_fields.py` cases in sequence; it has reached 22% with no reported failure. This is still partial, not final suite pass evidence. The next FastAPI matrix run is configured to use an isolated hidden Windows console, as Uvicorn did, to preserve the launcher when upstream signal/process tests execute; this does not alter upstream test source or XLang3 runtime behavior. No commit or push.

2026-09-27 complete Pydantic result: The full unchanged pinned Pydantic 2.13.5 suite on current-source Release XLang3 and pytest 8.4.2 exited **0**: **5804 passed, 261 skipped, 26 xfailed, 0 failed**, all 6091 collected tests accounted for (`scratch/upstream-pydantic-pytest8-final-20260927.log`, `scratch/upstream-results-pydantic-pytest8-final-20260927/matrix-results.json`). The matrix JSON records the exact pytest version and additional-package paths; no deselections or upstream source edits. Full unchanged FastAPI 0.141.1 now runs in its isolated Windows console at `scratch/upstream-fastapi-isolated-20260927.log`, exec session 89578; await terminal exit before counting it. No commit or push.

2026-09-27 FastAPI upstream progress: The isolated unchanged FastAPI suite collected **3335** tests (plus 10 collection-time skips) on current-source XLang3 with pytest 9.1.1 and reached 18% without a reported failure. It passed through the entire `tests/test_frontend.py` region where the previous concurrent matrix process had exited with Windows heap-corruption code; this narrows the earlier crash but does not prove full-suite stability. The live session is 89578 and result log is `scratch/upstream-fastapi-isolated-20260927.log`; await exit. No commit or push.

2026-09-27 complete FastAPI comparison: The full unchanged pinned FastAPI 0.141.1 suite completed in an isolated Windows console with **3318 passed, 17 skipped, 4 xfailed, 6 failed** (`scratch/upstream-fastapi-isolated-20260927.log`, `scratch/upstream-results-fastapi-isolated-20260927/matrix-results.json`). The only failed nodes are six `tests/test_tutorial/test_header_param_models` cases. A focused CPython 3.14.7 run of that exact directory with the same dependency paths and pytest 9.1.1 yields the same six failed nodes (26 passed, six teardown errors; `scratch/fastapi-header-params-cpython-20260927.log`). In both runtimes the local HTTPX test client advertises `accept-encoding: gzip, deflate, br, zstd`, while the upstream snapshot permits only no-Brotli variants. The FastAPI matrix has **zero XLang-only failures observed** on this full run, but its exit is not green and the six shared failures remain explicitly reported; no tests were deselected or source-modified. The earlier concurrent 11%-point process crash did not recur under isolated execution. No commit or push.

2026-09-27 AnyIO raw and diagnostic matrix: The full unchanged pinned AnyIO 4.15.1 run collected 4236 tests and stopped at its 50-failure limit within `tests/streams/test_tls.py`: **25 failed, 205 passed, 75 skipped, 25 teardown errors** at 6% (`scratch/upstream-anyio-isolated-20260927.log`). A complete CPython 3.14.7 run of that same unchanged 80-test TLS file under the same dependency paths produced **38 failed, 21 passed, 20 skipped, 33 teardown errors**, with certificate issuer verification failures (`scratch/anyio-tls-cpython-full-20260927.log`). This is a shared local TLS test environment problem, but the raw AnyIO full matrix is not green. The matrix runner now accepts explicit diagnostic deselections and records them separately from justified platform deselections. A diagnostic rerun excluding only `tests/streams/test_tls.py` is active at `scratch/upstream-anyio-no-shared-tls-20260927.log`, exec session 91713; no upstream source modification and no claim that this is a full suite pass. No commit or push.

2026-09-27 AnyIO diagnostic result: The explicit no-TLS diagnostic run finished at its 50-failure cap after 74%: **44 failed, 1755 passed, 1342 skipped, 80 deselected, 1 xfailed, 6 errors** (`scratch/upstream-anyio-no-shared-tls-20260927.log`, `scratch/upstream-results-anyio-no-shared-tls-20260927/matrix-results.json`). This is not a full-suite pass. Failure clusters are 15 pytest-plugin tests, 12 socket tests plus six TLS teardown errors, and 17 subprocess tests. First concrete XLang3 native gap: Trio's Windows subprocess wait constructs a ctypes handle array and assigns `handle_arr[i] = handles[i]`, but `_ctypes.Array` has `__getitem__` and no `__setitem__`; this produces `TypeError: object does not support item assignment` across the Trio process cases. The listener failure is `RuntimeError: imported module binding is not a module` inside patched `socket.socket`, another general import/patching issue to diagnose. The six socket TLS handshake errors appear to match the CPython local issuer-cert failure, but require focused comparison. The `gc.get_referrers` happy-eyeballs assertions and pytest-plugin failures also need CPython comparison/root-cause analysis. Current estimate of the overall production FastAPI-compatibility goal is ~70%, subjective rather than a test-derived metric. No commit or push.

2026-09-27 correction and focused fixes: The handle array above is actually `_cffi_backend.CData` from unchanged Trio's generated Windows FFI, **not** ctypes; the earlier diagnostic classification was wrong. Native CFFI now supports typed array item assignment with bounds checks. `_ctypes.Array.__setitem__` was also added as a separate general compatibility improvement. Public FastAPI/TestClient CPython oracles `cffi_array_assignment_contract` and `ctypes_array_assignment_contract` match XLang3. The unchanged AnyIO `tests/test_subprocesses.py` now passes **60 passed, 36 Windows skips**, identical counts to the CPython 3.14 baseline (`scratch/anyio-subprocess-final-20260927.log`, `scratch/anyio-subprocess-cpython-baseline-20260927.log`). This additionally required process text stdout CRLF translation on Windows and CPython-compatible WinError 232 -> `BrokenPipeError`/EPIPE mapping; public `windows_stdio_pipe_contract` checks text vs binary bytes, error class/errno/winerror, and a FastAPI route against CPython. Windows stdout translation is limited to actual process stdout, preserving LF in embedded output sinks. The three unchanged `TestTCPListener.test_tcp_listener_total_bind_failure` variants now pass (plus one Windows skip) after the optimized imported-module method call was taught to resolve a reassigned binding through normal `getattr` semantics; `module_binding_reassignment_contract` checks direct and public FastAPI calls against CPython (`scratch/anyio-listener-final2-20260927.log`). AnyIO's pytest-plugin failure `test_plugin` reproduces under CPython with unrelated globally autoloaded `pytest_benchmark`, but both CPython and XLang3 pass it with `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`; upstream AnyIO's own tox config sets this, so the matrix manifest/runner now records that choice. Current-source Release ALL_BUILD (`scratch/fastapi-allbuild-stdio-sink-20260927.log`), CTest **53/53** (`scratch/fastapi-ctest-stdio-sink-20260927.log`), four focused public contracts, and `git diff --check` pass. The complete final-source local FastAPI gate is still running (`scratch/fastapi-gate-stdio-sink-20260927.log`, session 76183); do not claim it green until exit. Full unchanged AnyIO and other upstream matrices need rerun on final source. No commit or push.

2026-09-27 final-source gate update: The CFFI array writer now additionally rejects unsigned overflow, including -1 and 2^32 for DWORD, and rejects None/non-pointer values for HANDLE arrays, matching the CPython 3.14 oracle (`tests/fastapi/cffi_array_assignment_contract.py/.out`). Release ALL_BUILD succeeds (`scratch/fastapi-allbuild-cffi-range-20260927.log`). The complete local FastAPI gate on that exact build exits **0 with 249/249** cases, including all four new CPython-oracle FastAPI routes, real Uvicorn HTTP/HTTPS and WebSocket (`scratch/fastapi-gate-cffi-range-final-20260927.log`). `git diff --check` passes. Final-source CTest is running at `scratch/fastapi-ctest-cffi-range-final-20260927.log`, session 56882; await exit. Unchanged upstream AnyIO still needs a matrix rerun under its declared disabled plugin autoload; full stack goal remains active. No commit or push.

2026-09-27 further AnyIO plugin diagnosis: CTest on the CFFI-range source completed **53/53** (`scratch/fastapi-ctest-cffi-range-final-20260927.log`). The unchanged AnyIO `tests/test_pytest_plugin.py` file with plugin autoload disabled per upstream tox passed **36/36 runnable tests on CPython** plus four Windows skips (`scratch/anyio-plugin-cpython-noautoload-full-20260927.log`), but XLang3 initially had five failures (`scratch/anyio-plugin-xlang-noautoload-full-20260927.log`). Native `dir(function)` omitted user-added function attributes, so Hypothesis lost an inner `pytest.mark.anyio`; native function enumeration now includes its actual attribute dictionary, and unchanged `test_hypothesis_function_mark` passes with a public CPython-oracle FastAPI `function_dir_attributes_contract`. SIGINT had erroneously defaulted to `SIG_DFL` instead of the shared `signal.default_int_handler` object; the general signal module now returns the correct default handler and raises `KeyboardInterrupt` from `signal.raise_signal(SIGINT)`, matching `sigint_default_contract` and passing unchanged `test_keyboard_interrupt_does_not_resume_test`. Current source Release ALL_BUILD succeeds (`scratch/fastapi-allbuild-sigint-default-20260927.log`). The complete unchanged AnyIO plugin file on that source now reports **33 passed, four Windows skips, three XLang-only failures** (`scratch/anyio-plugin-xlang-after-fixes-20260927.log`); all three are nested pytest subprocess tests with hard 3-second timeouts. A simple one-test pytest process takes ~6.8 seconds on XLang3 versus ~1.7 on CPython in this environment (`scratch/probe_pytest_startup_xlang_noplugin.log`), so runner startup/runtime performance is a remaining compatibility barrier; the async-fixture test also needs a longer-timeout diagnostic before declaring it merely slow. Full final-source CTest and FastAPI gates are still required after the function-dir/SIGINT changes; do not inherit the prior green gates as proof. Goal remains active; no commit or push.

2026-09-27 SIGINT fixture and final gates: CPython 3.14 confirms replacing the default SIGINT handler returns `signal.default_int_handler`, not `SIG_DFL`; one old expected line in `tests/fixtures/expected/compat_sections/standard_modules.out` was corrected to `False True True` while leaving the fixture source untouched. Complete exact-source CTest now passes **53/53** (`scratch/fastapi-ctest-function-dir-sigint-cp-oracle-20260927.log`). Release ALL_BUILD passed earlier on the same source (`scratch/fastapi-allbuild-sigint-default-20260927.log`). The final-source local FastAPI gate including the two new oracle routes is running at `scratch/fastapi-gate-function-dir-sigint-final-20260927.log`, session 80548; await exit before counting it. Remaining AnyIO plugin failures are the three 3-second nested subprocess tests, and full unchanged stack matrices still need final-source reruns. No commit or push.

2026-09-27 gate and demo follow-up: The exact-source local FastAPI gate completed with exit 0 and **251/251** cases, including real Uvicorn HTTP/HTTPS and WebSocket (`scratch/fastapi-gate-function-dir-sigint-final-20260927.log`). The exact-source CTest remains **53/53**. The demo site is running on XLang3 PID 31332 at `http://127.0.0.1:8765/`; real HTTP checks confirmed the page and runtime endpoint plus task create, toggle, delete, and empty-list lifecycle. Its 1000-request, 16-worker load test completed with **0 errors**, 23.3 requests/s, 653.2 ms median, 1245.5 ms p95 (`scratch/fastapi-demo-load-final-20260927.log`). A 30-second soak is in progress and should be reported only after exit. The goal is still estimated around 70%, not a measured compatibility percentage: complete unchanged upstream matrix, remaining AnyIO plugin subprocess issues and `gc.get_referrers` semantics, TLS baseline comparison, and native cryptography boundary still need work. No commit or push.

2026-09-27 demo soak completion: The 30-second, 16-worker HTTP soak against the real XLang3-hosted demo exited 0 with **328 requests and 0 errors** (31.654 seconds; 10.4 requests/s, 1527.1 ms median, 1749.1 ms p95; `scratch/fastapi-demo-soak-final-20260927.log`). Performance remains below the desired production confidence level and is not evidence that all upstream FastAPI dependencies are compatible. No commit or push.

2026-09-27 AnyIO nested-subprocess diagnosis: Both remaining categories were rerun as the unchanged generated child pytest files under the same pinned dependency paths, with a longer outer observation window. The dynamic async-fixture child exited in 7.77 seconds with the expected two `RuntimeError: Cannot schedule a coroutine ...` failures (`scratch/anyio-dynamic-fixture-child-long-probe.log`); the keyboard-interrupt child exited in 3.46 seconds with the expected `KeyboardInterrupt` failure (`scratch/anyio-keyboard-child-long-probe.log`). This confirms the three parent test failures are currently its hard 3-second child timeout, although no claim is made that reducing startup time alone will make the exact unchanged parent test pass. The general `gc.get_referrers` implementation still only checks current locals, and `frame.f_generator` returns `None` unconditionally; CPython 3.14's AnyIO `no_other_refs()` explicitly expects the containing generator, so that semantic gap remains. The pinned HTTPX checkout had an untracked OpenSSL TLS keylog `test` generated by earlier work; it was preserved outside the checkout as `scratch/httpx-generated-tls-keylog-20260926.txt`, restoring a clean upstream checkout. Exact-source unchanged HTTPX matrix is running at `scratch/upstream-results-httpx-final-20260927/httpx.log`; await its exit and results JSON. No commit or push.

2026-09-27 generator-referrer oracle: A minimal pure-Python probe (`scratch/probe_gc_generator_owner.py`) run unchanged under CPython 3.14.7 and XLang3 shows CPython reports `sys._getframe().f_generator is generator` as true and `gc.get_referrers(local_list)` as `['generator']` inside a generator. XLang3 reports false and `['dict']`. This confirms the gap is not limited to an AnyIO fixture and rules out treating the current locals snapshot as a correct referrer. A general frame-owner and GC traversal implementation is needed; no targeted fake should be added. No commit or push.

2026-09-27 exact-source HTTPX matrix: Unmodified pinned HTTPX 0.28.1 finished **1411 passed, 1 skipped, 3 failed, 3 errors** out of 1418 collected in 131.25 seconds (`scratch/upstream-results-httpx-final-20260927/httpx.log`, results JSON in the same directory). All three assertion failures—text-mode multipart upload and the two logging-record expectations—also fail unchanged under CPython 3.14.7 with the identical pinned paths (`scratch/httpx-cpython-focused-baseline-20260927.log`, 3 failed), so they are shared local baseline failures, not XLang3-only regressions. The three SSL-config setup errors are XLang3-only: native `cryptography.hazmat.bindings._rust` has modern `openssl` but no legacy `_openssl` CFFI bridge, which the unmodified cryptography 46.0.7 `default_backend()` import requires. The same three tests pass under CPython with the same paths (`scratch/httpx-cpython-cryptography-baseline-20260927.log`). This is a true native dependency boundary; implementation must stay in `modules/cryptography` and must not load the CPython binary extension. The checkout is clean after preserving generated TLS keylogs in scratch. No commit or push.

2026-09-27 exact-source Uvicorn matrix: Unmodified pinned Uvicorn 0.53.0 completed **1004 passed, 342 skipped, 5 failed, 1 setup error** of 1352 collected in 533.61 seconds (`scratch/upstream-results-uvicorn-final-20260927/uvicorn.log`, results JSON in the same directory). The six non-passes are again in unchanged `tests/test_ssl.py`: five TLS handshakes fail with `unable to get local issuer certificate`, already reproduced in the CPython 3.14 local baseline (`scratch/uvicorn-ssl-cpython-baseline-20260927.log`); the encrypted-key fixture errors because native cryptography lacks `_rust._openssl`, the same XLang3-only boundary exposed by HTTPX. The 342 skips are emitted by upstream's own Windows/unavailable-optional-protocol markers, not matrix deselections. This is a complete run, not a green suite; no commit or push.

2026-09-27 matrix inventory: A concise seven-suite evidence table and open-gap inventory is now maintained in `agent/python314_compat/tasks/fastapi_matrix_status.md`. The exact-source unchanged FastAPI suite is running in an isolated console at `scratch/upstream-results-fastapi-final-20260927/fastapi.log` (exec session 73602); it has not exited yet. Source inspection found that unmodified cryptography 46.0.7's `default_backend()` imports the legacy `_rust._openssl` binding for its FFI object, OpenSSL version/package check, and feature flags even though its serialization functions use the modern native `openssl.keys` module. This identifies the true native boundary more precisely; no implementation or workaround has been added. No commit or push.

2026-09-27 FastAPI exact-source process exit: The full unchanged isolated FastAPI rerun exited **-1 without a pytest summary** around 11%, during `tests/test_frontend.py` (`scratch/upstream-results-fastapi-final-20260927/fastapi.log`, results JSON). This resembles but does not prove the earlier process-level frontend failure and is not a complete suite result. The same unchanged frontend file alone passed **93/93** on the exact current binary with the same dependency paths and runner warning filters (`scratch/fastapi-frontend-isolated-final-20260927.log`). The first focused attempt without the matrix warning filters stopped at collection with the expected deprecation warning and is not a runtime test failure. The full-run exit is a real unresolved stability issue; do not count the prior-source complete FastAPI run as exact-source verification. A verbose isolated full rerun or smaller prefix reproduction is needed to locate the exit. No source change, commit, or push.

2026-09-27 heap-corruption localization: A verbose, unchanged full FastAPI rerun reproduced Windows exit **-1073740940** (heap corruption) at `tests/test_frontend.py::test_directory_index_and_redirect`, after the preceding frontend cases passed (`scratch/fastapi-upstream-verbose-final-20260927.log`). The previous matrix wrapper had surfaced the exit as -1, but the direct isolated launcher preserved the Windows code. Exact-source AddressSanitizer Release-with-debug binary now builds (`scratch/fastapi-asan-build-final-20260927.log`). A sanitizer run of `test_dependency_models.py` plus `test_frontend.py` selected 103 unchanged tests but was stopped by its 120-second pytest timeout inside `test_callable_classification_cache_supports_large_apps`, before reaching frontend; it yielded no memory report (`scratch/fastapi-asan-dependency-frontend-20260927.log`). Therefore that run does not disprove a heap bug. A smaller Release two-file reproduction is active at `scratch/fastapi-release-dependency-frontend-20260927.log`, exec session 88746; await its exit. No source fix yet, no commit or push.

2026-09-27 prefix isolation: The unchanged `test_dependency_models.py` plus complete `test_frontend.py` pair passes **103/103** on the current Release binary (`scratch/fastapi-release-dependency-frontend-20260927.log`), so that pair alone is insufficient. The exact 61 upstream test files that precede frontend in the failed full run, followed by only `test_frontend.py::test_directory_index_and_redirect`, pass **343 with 7 skips** (`scratch/fastapi-release-prefix-target-20260927.log`). Therefore the preceding files alone do not make the individual target crash; earlier frontend cases may interact with accumulated state. A new run with those exact 61 files plus the whole unchanged frontend file is active at `scratch/fastapi-release-prefix-frontend-20260927.log`, exec session 99774. This is a diagnostic selection, not an upstream suite pass. No source fix, commit, or push.

2026-09-27 full-collection heap reproduction: The explicit 61-file prefix plus complete frontend file passes **435 passed, 7 skipped** (`scratch/fastapi-release-prefix-frontend-20260927.log`). When pytest collects the entire unchanged FastAPI `tests` directory but `-k` selects only that same 61-file prefix plus frontend execution, the current Release process reproduces Windows heap corruption **-1073740940** during `test_apirouter_frontend_dependencies_protect_prefixed_frontend` (`scratch/fastapi-release-fullcollect-prefix-20260927.log`). This differs from the prior full-suite failure point (`test_directory_index_and_redirect`) but confirms that collecting later modules or the resulting larger object graph is necessary in this repro. The original single target preceded by 61 files passes, so it is not inherently failing. Exact-source ASAN binary is now running the full-collection/selected-prefix sequence with `--timeout=0` to allow the slow 3,000-callable upstream stress case (`scratch/fastapi-asan-fullcollect-prefix-20260927.log`, exec session 25671; process PID 15196 at checkpoint). It has not produced a sanitizer result yet; do not restart while live. No source fix, commit, or push.

2026-09-27 cryptography binding oracle: With the identical pinned local paths, CPython 3.14.7's cryptography 46.0.7 `_rust._openssl` exposes an FFI object and library object; its package-version C string is `b'46.0.7'`, and its `OpenSSL_version_num()` matches modern native `openssl.openssl_version()` at 810549344. XLang3's current native `openssl.openssl_version()` is 808452128 (`OpenSSL 3.3.2 3 Sep 2024`), so any genuine `_openssl` bridge must reflect the actual linked library and its feature flags, not copy CPython's compiled OpenSSL 3.5 constants. No implementation yet; current priority remains the heap-corruption diagnosis. No commit or push.

2026-09-27 ASAN observation checkpoint: The exact-source ASAN full-collection/selected-prefix process PID 15196 remained live after over 10 minutes, CPU-active with about 2.9 GB working set. Its log `scratch/fastapi-asan-fullcollect-prefix-20260927.log` had reached pytest session startup but had not yet emitted collection completion or a sanitizer report. Exec session 25671 remains the observation handle. This is a verified wait on that same process, not a completed test result. Continue polling the handle/process; do not restart solely because this turn ended. No source change, commit, or push.

2026-09-27 ASAN continued observation: The same PID 15196 remained live at 15+ minutes, CPU-active with working set roughly 2.9 GB and private bytes roughly 3.4 GB; its log still ended at pytest's plugin banner with no collection summary or sanitizer finding. The memory use had mostly leveled off. CPython oracle inspection also confirmed the pinned binary cryptography wheel's `_openssl` binding reports OpenSSL 3.5 capabilities while XLang3's native OpenSSL is 3.3.2; future native bridge must reflect the latter. No runtime code change or test conclusion from the live ASAN process. No commit or push.

2026-09-27 full-collection CPython baseline: The same full FastAPI collection with the same 62-file `-k` prefix/frontend selection, unchanged upstream tests, pinned dependency paths, and pytest warning filters exits **0 on CPython 3.14.7**: 439 passed, 17 skipped, 2889 deselected (`scratch/fastapi-cpython-fullcollect-prefix-20260927.log`). XLang3 Release with this selection exited Windows heap corruption, so the process failure is XLang3-specific. Counts differ because platform/backend marks select variants differently; the comparison is about the crash, not exact pass-count equality. ASAN PID 15196 still remained live during this baseline check; no sanitizer report yet. No source fix, commit, or push.

2026-09-27 collection/execution separation: Full-suite collection with only `test_frontend` selected executes **93 passed, 10 skipped, 3242 deselected** on the current Release XLang3 binary (`scratch/fastapi-release-fullcollect-frontendonly-20260927.log`). Thus collection alone is insufficient; some earlier selected test execution also contributes to the heap-corruption trigger. A half-prefix diagnostic selecting the first 30 of 61 preceding test files plus frontend while still collecting the full suite is active at `scratch/fastapi-release-fullcollect-half1-frontend-20260927.log`, exec session 21066. The exact-source ASAN full-collection/whole-prefix process PID 15196 remains live, CPU-active near 20 minutes without a test or memory report; preserve and poll it. No source change, commit, or push.

2026-09-27 half-prefix outcomes: With full-suite collection, the first 30 preceding file-name terms plus frontend selected pass **239 passed, 13 skipped, 3093 deselected** (`scratch/fastapi-release-fullcollect-half1-frontend-20260927.log`). The remaining 31 preceding file-name terms plus frontend selected pass **294 passed, 14 skipped, 3037 deselected** (`scratch/fastapi-release-fullcollect-half2-frontend-20260927.log`). The combined 61-term selection reproducibly caused Windows heap corruption, whereas each half and frontend alone passes. Pytest `-k` matches names by substring, so the halves include some similarly named tests from other files; these are diagnostic selections, not exact partitions or suite results. The evidence favors cumulative object pressure or interaction across groups over one necessary file, without proving a root cause. The same ASAN whole-prefix run PID 15196 remained live past 25 minutes, using about 3 GB RAM, still before collection completion. No source fix, commit, or push.

2026-09-27 three-quarter selection: Full-suite collection with the first 45 of the 61 preceding file-name terms plus frontend selected passes **393 passed, 13 skipped, 2939 deselected** on the exact Release source (`scratch/fastapi-release-fullcollect-threequarter-20260927.log`). This places the reproducible combined-prefix crash beyond that selection; it does not by itself prove a numerical memory threshold because `-k` substring matches and test interactions can differ. A 53-term plus frontend selection is running at `scratch/fastapi-release-fullcollect-fiftythree-20260927.log` (exec session 28380). The same exact-source ASAN full-prefix process PID 15196 remains active but has not yet produced collection completion or a sanitizer trace. No source fix, commit, or push.

2026-09-27 expanded selection: The first 53 of 61 file-name terms plus frontend, still with full-suite collection, also pass **409 passed, 17 skipped, 2919 deselected** (`scratch/fastapi-release-fullcollect-fiftythree-20260927.log`). This includes added response, schema, and exception-handler groups and narrows the crash trigger to the remaining eight groups or their combined effect. A 57-term selection is running at `scratch/fastapi-release-fullcollect-fiftyseven-20260927.log` (exec session 86688). ASAN full-prefix PID 15196 is still active without a report. No source fix, commit, or push.

2026-09-27 near-complete prefix: The first 57 file-name terms plus frontend pass **426 passed, 17 skipped, 2902 deselected** with full-suite collection (`scratch/fastapi-release-fullcollect-fiftyseven-20260927.log`). The last four terms are `test_form_default`, `test_forms_from_non_typing_sequences`, `test_forms_single_model`, and `test_forms_single_param`; their interaction with the earlier execution remains to be checked. A 59-term selection is active at `scratch/fastapi-release-fullcollect-fiftynine-20260927.log` (exec session 98059). No source fix, commit, or push.

2026-09-27 final-prefix narrowing: The first 59 file-name terms plus frontend pass **431 passed, 17 skipped, 2897 deselected** (`scratch/fastapi-release-fullcollect-fiftynine-20260927.log`). Only the `test_forms_single_model` and `test_forms_single_param` terms remain before the previously crashing 61-term selection. The 60-term run is active at `scratch/fastapi-release-fullcollect-sixty-20260927.log` (exec session 1458). The full-prefix ASAN process PID 15196 remains CPU-active, with no collection summary or sanitizer report yet. No source fix, commit, or push.

2026-09-27 60-term outcome: The first 60 file-name terms plus frontend pass **437 passed, 17 skipped, 2891 deselected** (`scratch/fastapi-release-fullcollect-sixty-20260927.log`). The only additional file-name term in the earlier crashing 61-term selection is `test_forms_single_param`, whose module has two tests. A fresh full 61-term verbose Release confirmation is active at `scratch/fastapi-release-fullcollect-allprefix-confirm-20260927.log` (exec session 26011), before attributing causality to either test or cumulative object state. The same ASAN process remains live. No source fix, commit, or push.

2026-09-27 nondeterministic crash correction: The fresh exact 61-term full-collection selection passed **439 passed, 17 skipped, 2889 deselected** (`scratch/fastapi-release-fullcollect-allprefix-confirm-20260927.log`, exit 0), whereas the earlier 61-term run exited Windows heap corruption around 81%. Thus the final form module is not a sufficient deterministic cause and the prior monotonic-prefix narrowing is only pressure/interaction evidence, not a bisect of one test. The full-suite source crash remains unresolved because it reproduced on two direct/full-matrix runs; a single passing selected rerun does not establish stability. Preserve and await the exact-source ASAN PID 15196, which has not yet finished collection or reported a sanitizer error. No source fix, commit, or push.

2026-09-27 sanitizer terminal state: The preserved ASAN run (PID 15196, exec session 25671) eventually completed full collection (3335 items/446 selected) after roughly 47 minutes and executed through 27% before terminating with **exit 3** mid-output at `test_default_response_class_router.py::test_router_a_b_override`. Its log `scratch/fastapi-asan-fullcollect-prefix-20260927.log` has no AddressSanitizer report, native stack, pytest summary, or explicit OOM diagnostic. Thus it neither diagnoses nor excludes the Release heap corruption. A fresh exact-source full Release suite with normal warning filters, `--assert=plain`, `-n 0`, and 120-second timeout is running at `scratch/fastapi-release-full-recheck-20260927.log` (exec session 83762). No source fix, commit, or push.

2026-09-27 full-suite reproduction: The fresh unchanged full FastAPI Release suite again exited **-1073740940 (0xC0000374 Windows heap corruption)** at 11%, this time during `test_frontend.py::test_index_fallback_for_navigation_request[/users/jane.doe-text/html]` (`scratch/fastapi-release-full-recheck-20260927.log`, exec session 83762). Its crash point differs from earlier full runs and the selected-prefix crash, while an exact selected-prefix rerun passed. This establishes an intermittent memory-safety fault sensitive to the full test execution/selection state, not a deterministic failure of one frontend assertion. The ASAN run's unexplained exit 3 yielded no usable trace; do not claim current-source upstream FastAPI stability. No source fix, commit, or push.

2026-09-27 Windows crash-dump diagnostic: A host-Python-only debugger launcher at `scratch/capture_fastapi_heap_crash.py` now runs the unchanged XLang3 full test command using Windows DebugActiveProcess-style process debug events and calls MiniDumpWriteDump at a heap-corruption exception. It is diagnostic tooling, not an XLang runtime or package dependency. The first validation run under the debugger terminated at its first test with `0xC0000008` because Windows raises invalid-handle exceptions during normal cleanup when debugged; the launcher now continues those first-chance events. The corrected one-test smoke run passed (`scratch/fastapi-heap-debugger-smoke2-20260927/pytest.log`, child exit 0), validating the monitor can let normal tests finish. The complete unchanged suite is now running under the corrected monitor at `scratch/fastapi-heap-debugger-full-20260927` (child PID 30948, exec session 37979). A debugged run may affect timing; it is for capturing a native fault, not as final upstream suite verification. No runtime source fix, commit, or push.

2026-09-27 debugger timeout adjustment: The first corrected full debugged run reached the upstream 3,000-callable dependency stress test at 5% but hit pytest's 120-second timeout under debugger overhead (`scratch/fastapi-heap-debugger-full-20260927/pytest.log`, child exit 1). This does not reproduce the Release heap crash and is not a product behavior result. The diagnostic launcher now passes `--timeout=0`, matching the earlier sanitizer diagnostic, and a new full debugged run is active at `scratch/fastapi-heap-debugger-full-no-timeout-20260927` (child PID 30068, exec session 51565). Normal uninstrumented suite validation retains its original timeout. No source fix, commit, or push.

2026-09-27 debugger live checkpoint: The timeout-free full diagnostic PID 30068 remained live after 15 minutes, CPU-active with about 1.08 GB working set. Full collection completed and the unchanged first ~5% of tests passed; it is still executing `test_dependency_models.py::test_callable_classification_cache_supports_large_apps`, whose 3,000-callable workload is much slower under the debugger. `scratch/fastapi-heap-debugger-full-no-timeout-20260927/debugger.jsonl` has no heap-corruption event and no dump yet. Preserve PID 30068 and exec session 51565 while they remain live; do not restart merely because this turn ends. This is only a diagnostic wait, not a source fix or complete upstream result. No commit or push.

2026-09-27 less intrusive dump capture: The start-under-debugger run stayed in the 3,000-callable stress test beyond 16 minutes with stable CPU/memory and no exception; the debugger itself heavily altered execution time. I deliberately stopped that diagnostic (PID 30068 and session 51565 are terminal) and added a `--attach-late` mode to `scratch/capture_fastapi_heap_crash.py`: XLang3 runs the full unchanged suite normally with the normal 120-second timeout, and the Windows debugger attaches at `test_forms_single_param.py` immediately before frontend. This preserves normal collection/stress timing while still observing the heap-crash region. The late-attach run is active at `scratch/fastapi-heap-debugger-late-20260927` (child PID 16932, exec session 19190); await actual attach/exit and check for a dump. The diagnostic monitor uses host CPython only and does not become an XLang3 runtime dependency. No runtime source fix, commit, or push.

2026-09-27 native crash captured: The late-attach full unchanged FastAPI run reached frontend and caught first-chance `0xC0000374` Windows heap corruption at 12%, during `test_unsupported_methods_to_fallback_only_routes_return_404[PATCH]` (`scratch/fastapi-heap-debugger-late-20260927/pytest.log`, child exit `0xC0000374`). The monitor wrote `scratch/fastapi-heap-debugger-late-20260927/xlang3-16932-c0000374-7ffa78837eb5.dmp` (530,230 bytes). I linked the same Release objects into a separate diagnostic DLL with a map at `scratch/fastapi-symbolized-release/xlang3_runtime.map`; the diagnostic DLL has byte-identical `.text` and `.pdata` sections and the same image size as the crashing `build/Release/xlang3_runtime.dll`, so its code RVAs can be trusted for this dump. The faulting thread ID 15196 has `ntdll` heap-failure frames, then `ucrtbase` free, then `xlang3_runtime.dll+0x51a773` in `InstanceFreeList::~InstanceFreeList`, immediately after an `InstanceObject` destructor and sized delete call; the lower stack includes the thread-local `dict_object_free_list` teardown and `__dyn_tls_dtor`. This localizes *detection* to thread-local cached-object destruction at worker-thread shutdown, not necessarily the earlier corrupting write or the exact duplicated/damaged object. The generated vcxproj was restored after the separate map link; the live Release DLL was not overwritten. A focused exact-source ASAN frontend-file run is active at `scratch/fastapi-asan-frontend-focused-20260927.log` (exec session 71323, XLang PID 23792) with the correct VS 14.51 sanitizer DLL on PATH. No runtime source fix, commit, or push.

2026-09-27 focused sanitizer result: The exact-source ASAN run of unchanged `tests/test_frontend.py` completed **93 passed / exit 0** (`scratch/fastapi-asan-frontend-focused-20260927.log`, exec session 71323), with no AddressSanitizer report. This does not disprove the full-suite heap fault, because the focused run excludes earlier test execution and most collection-time objects. The validated late-attach crash monitor now supports a full-memory dump and logs the exception parameters; a new normal-speed full-suite run is active at `scratch/fastapi-heap-debugger-fullmemory-20260927` (child PID 22632, exec session 72958). If it reproduces, inspect the cached-instance pointer and surrounding free-list memory before changing runtime ownership code. No runtime source fix, commit, or push.

2026-09-27 heap ownership invariant: The late-attached full-memory diagnostic run finished without a heap crash, but produced **3318 passed, 17 skipped, 4 xfailed, 6 failed** on unchanged FastAPI (`scratch/fastapi-heap-debugger-fullmemory-20260927/pytest.log`). The six failures are the already observed CPython-shared Accept-Encoding/Brotli snapshot differences. This one instrumented pass does not erase the intermittent clean-run crash. An environment-gated check was temporarily added to `InstanceFreeList::~InstanceFreeList` to verify unique cached pointers, expected object kind, and zero reference counts before deletion. The next unchanged full-suite run with that check stopped at frontend with Windows fail-fast `0xC0000409`; its captured dump (`scratch/fastapi-freelist-invariant-dump-20260927/xlang3-28060-c0000409-7ffa787cd769.dmp`) and byte-matched Release code map place the fault immediately after the diagnostic `abort()` in the **instance** invariant failure branch. Therefore a cached instance violated one of those three ownership invariants. A subsequent diagnostic build gives distinct exit codes for duplicate, wrong-kind, and live-reference cases; its full-suite run is in progress at `scratch/fastapi-freelist-reason-20260927.log`. This check is temporary diagnostic code, not a fix. No commit or push.

2026-09-27 live-reference cause narrowed: The subsequent unchanged full-suite run again stopped in frontend at 12% with `0xC0000409` (`scratch/fastapi-freelist-reason-20260927.log`). A late-attached debugger rerun also stopped at frontend 10% and captured `scratch/fastapi-freelist-reason-dump-20260927/xlang3-31928-c0000409-7ffa787cd769.dmp`. Direct disassembly of the exact current Release DLL shows its runtime return address `xlang3_runtime.dll+0x51aef4` is immediately after the call to the diagnostic failure handler with reason code **104, cached instance has a nonzero live reference count**; the handler itself reached its termination call at `+0x51cbfb`. Its stack contains the failing cached pointer `0x1e62c503b00`. This is stronger than the initial three-way invariant: an instance entered the thread-local free list with refcount zero but subsequently had a nonzero count before teardown. The crash dump did not include the object's heap page, so the new count and mutation site are still unknown. The diagnostic guard remains temporary; investigate the first refcount mutation of a cached instance and add a general regression before finalizing a fix. The demo was restarted and `/api/runtime` again returned XLang3 3.14.7 ready. No commit or push.

2026-09-27 first-transition diagnostics: An environment-gated check was added to the generic `Value` retain path to log a native stack if an instance with zero reference count is retained; it is diagnostic only. The unchanged full FastAPI suite on that Release build completed **3318 passed, 17 skipped, 4 xfailed, 6 failed in 883.09 seconds** (`scratch/fastapi-zero-ref-run-20260927.log`), with no zero-reference log. The six failures are the same CPython-shared Accept-Encoding header snapshots. This passing execution cannot negate the earlier intermittent native crash and does not prove no invalid retain occurred on a failed run. A second temporary diagnostic now tracks each thread-local cached instance at insertion and removal and aborts on the first duplicate push or untracked pop. That Release build succeeded (`scratch/fastapi-transition-diagnostic-build-20260927.log`), and its unchanged full FastAPI suite is running at `scratch/fastapi-transition-run-20260927.log` (exec session 82136); the dedicated diagnostic record is `scratch/fastapi-transition-invariant-20260927.txt` if triggered. The demo was restarted on the new Release binary and `/api/runtime` returned XLang3 3.14.7 ready. No commit or push.

2026-09-27 weak-reference lifetime root: The transition-diagnostic unchanged FastAPI run stopped at frontend 11% with exit 3. Its dedicated log `scratch/fastapi-transition-invariant-20260927.txt` captured **the first retain of an instance with refcount zero** and a 32-frame native stack. A separate link from the exact Release object files yielded `scratch/fastapi-zero-ref-symbolized/xlang3_runtime.map`; its `.text`, `.pdata`, and image size match the runtime DLL byte-for-byte. Symbolication identifies the call path `Value::operator=` from `weakref_get_target` via `value_key_equal` and set construction, rather than a duplicate free-list push. The shared weak-reference registry had no synchronization and returned a raw target pointer before promoting it to an owned `Value`, allowing target final release to race with that promotion. The general runtime fix uses a registry mutex and atomic increment-if-nonzero while the registry entry remains locked; registration, invalidation, other registry readers, and weakref-cycle traversal are synchronized. Invalidation now marks entries dead and retains callback references under the lock, then reads callback attributes outside it. A new CPython-oracle threaded weakref lifetime fixture `tests/fixtures/core/weakref_thread_lifetime.py` passes on CPython 3.14.7 and XLang3; the existing weakref fixture output remains identical. The fixture deliberately creates targets in a function scope, because XLang3's module-frame dead-register lifetime can delay deletion of a top-level temporary; this separate semantic gap remains. The fixed Release binary builds and its unchanged full FastAPI suite with zero-ref/free-list invariants enabled is running at `scratch/fastapi-weakref-fix-full-20260927.log` (exec session 86176). Its result is pending, so stability is not yet claimed. The demo is serving from the new binary. No commit or push.

2026-09-27 fixed-binary checkpoint: The unchanged full FastAPI run on the weakref fix (exec session 86176) passed the previously crash-prone frontend region and reached 20% at last observation. `scratch/fastapi-weakref-fix-invariants-20260927.txt` was absent, so neither the zero-ref retain nor duplicate cache-transition diagnostic fired in this run through that point. This is an active run, not a completed suite result; preserve its process/session and await the final exit. The running demo's `/api/runtime` endpoint returned XLang3 3.14.7 ready. No commit or push.

2026-09-27 FastAPI weakref concurrency coverage: The existing public `tests/fastapi/set_weakref_hash_contract.py` now makes 16 concurrent TestClient requests across four workers to the real FastAPI weak-set endpoint; it checks each response and prints a deterministic count. This contract passes unchanged under CPython 3.14.7 and XLang3 on the pinned local package paths. The expected output fixture was updated accordingly. The exact unchanged upstream FastAPI full suite on the fixed binary remains active in exec session 86176 and passed 44% without an ownership diagnostic at last observation; no final result yet. No commit or push.

2026-09-27 fixed-binary full FastAPI result: The exact unchanged full FastAPI suite completed **3318 passed, 17 skipped, 4 xfailed, 6 failed in 892.65 seconds** (`scratch/fastapi-weakref-fix-full-20260927.log`, exec session 86176); there was no process-level exit or zero-reference/free-list diagnostic. All six failures are the same Accept-Encoding/Brotli header snapshots already reproduced under CPython 3.14.7. This is a substantive improvement over multiple prior exact-source frontend crashes, but one complete run does not prove an intermittent race eliminated. The temporary diagnostics in `object_model.cpp`, `value.cpp`, and `value.h` have now been removed, leaving the general weakref registry lifetime fix. The new threaded oracle fixture is registered in both fixture runners, and the concurrent public FastAPI weak-set contract is registered in the existing local FastAPI runner. A clean Release ALL_BUILD is running at `scratch/fastapi-weakref-clean-allbuild-20260927.log` (exec session 24116); after it finishes, run CTest, complete fixtures, local FastAPI, and a clean-source upstream FastAPI rerun. The demo was stopped for the DLL rebuild and should be restarted afterward. No commit or push.

2026-09-27 clean-source gates: The Release ALL_BUILD succeeded (`scratch/fastapi-weakref-clean-allbuild-20260927.log`), then CTest completed **53/53 passed**, including the fixture runner with `weakref_thread_lifetime` (`scratch/fastapi-weakref-clean-ctest-20260927.log`). The local FastAPI runner completed **251/251 contracts**, including the concurrent weak-set endpoint and live Uvicorn HTTP/HTTPS and WebSocket integrations (`scratch/fastapi-weakref-clean-local-20260927.log`). The demo was restarted and `/api/runtime` returned XLang3 3.14.7 ready. A new full unchanged FastAPI run on the clean production binary is active at `scratch/fastapi-weakref-clean-full-20260927.log` (exec session 91037); await its result. No commit or push.

2026-09-27 remaining AnyIO semantic boundary: Source inspection confirms `gc.get_referrers` in `src/runtime/modules/system/gc_module.cpp` currently inspects only a synthesized current-locals dictionary, `FrameObject.f_generator` in `src/runtime/object_model.cpp` always returns `None`, and `generator_vm_frame_snapshot` in `src/executor/xlang_vm/xlang_vm_loop.cpp` constructs a fresh frame snapshot on access. Together they explain the earlier CPython-oracle mismatch for `sys._getframe().f_generator` and generator ownership, and show why a local AnyIO assertion patch would be incorrect. A general fix needs stable generator/frame ownership plus object-graph referrer traversal, while preserving frame and generator lifetimes without strong-reference cycles. No implementation claim yet. The clean-source full FastAPI run remains live and reached 30% at last observation. No commit or push.

2026-09-27 clean-source full FastAPI result: The exact unchanged pinned FastAPI suite completed on the production-source Release binary with **3318 passed, 17 skipped, 4 xfailed, 6 failed in 853.69 seconds** (`scratch/fastapi-weakref-clean-full-20260927.log`, exec session 91037). The six failed nodes are precisely the Accept-Encoding/Brotli header snapshots previously reproduced under CPython 3.14.7 with the same local package paths. There was no process-level heap exit. Together with the prior instrumented full run, 53/53 CTest, and 251/251 local FastAPI contracts, this supports the weakref lifetime fix beyond the formerly intermittent frontend crash. It is still not proof of a fully green upstream suite, and the goal remains active for AnyIO, native cryptography `_rust._openssl`, final-source matrix reruns, and final load/soak. The demo remains live. No commit or push.

2026-09-27 native cryptography progress: The pinned, unmodified cryptography 46.0.7 Python package now imports its legacy `_rust._openssl` boundary from `modules/cryptography/legacy_openssl_module.cpp`, with native version and feature data supplied by the linked OpenSSL rather than a CPython extension. The same native package now accepts loaded EC private/public keys through the existing EC key type and serializes EC private keys with `BestAvailableEncryption` using OpenSSL. The exact three previously blocked HTTPX encrypted-key upstream nodes pass (3 passed, 25 deselected). An EC encrypted PEM round trip and a FastAPI TestClient route in `tests/fastapi/cryptography_ec_contract.py` produce matching output on CPython 3.14 and XLang3. The focused Uvicorn `test_run_password` no longer fails at cryptography import, but its async call timed out at 45 seconds inside the Windows event loop, so that node remains unverified/failed and needs separate diagnosis. The broader local FastAPI gate is rerunning. No pure-Python package edits, CPython runtime/ABI dependency, commit, or push.

2026-09-27 `_ssl` encrypted-key boundary: The earlier Uvicorn focused timeout was caused by XLang3 `SSLContext.load_cert_chain` rejecting Uvicorn's normal password callable, leaving its server-start loop waiting. Native `_ssl` now calls a callable password and validates its str/bytes result; the same method now converts str, bytes, and `os.PathLike` certificate/key paths to stable owned strings before passing them to OpenSSL. A CPython-oracle FastAPI TestClient regression in `trustme_tls_contract.py` exercises a real encrypted EC key, `Path` arguments, one password callback, and a complete TLS 1.3 MemoryBIO handshake; CPython 3.14 and XLang3 produce identical output. Unmodified Uvicorn `test_run_password` now starts the server and fails only at the local TLS issuer verification error that also occurs under CPython (`scratch/uvicorn-ssl-cpython-baseline-20260927.log`). The full local FastAPI gate on the preceding native cryptography source exited zero, including live HTTP/WebSocket; the exact-source `_ssl` regression passed separately. The full unchanged HTTPX suite after the native cryptography fix completed 1,414 passed, 1 skipped, 3 CPython-shared failed, and zero setup errors in 129.32 seconds (`scratch/upstream-results-httpx-cryptography-20260927/httpx.log`). The full unchanged Uvicorn rerun is active in `scratch/upstream-results-uvicorn-cryptography-ssl-20260927/uvicorn.log`. Demo restarted from the current Release build at port 8765, PID 28844, with `/api/runtime` reporting xlang3 3.14.7 ready. No pure Python library edits, CPython ABI/runtime dependency, commit, or push.


2026-09-27 complete Uvicorn and lazy `_ssl` callback: The unchanged Uvicorn 0.53.0 full suite after cryptography and `_ssl` fixes completed **1,004 passed, 342 skipped, 6 failed, 0 setup errors** in 501.08 seconds (`scratch/upstream-results-uvicorn-cryptography-ssl-20260927/uvicorn.log`). All six are the TLS local-issuer verification failures also present in the CPython 3.14 baseline; the former `test_run_password` setup error and server-start hang are gone. A second CPython-oracle expansion showed that CPython invokes a password callable only when a key needs decryption. Native `_ssl` now registers a synchronous OpenSSL password callback for key loading, invokes Python lazily, propagates callback exceptions, clears callback state after load, and serializes concurrent load calls on one context. The public FastAPI encrypted-key route matches CPython for one encrypted-key callback, zero plain-key callbacks, exception propagation, Path inputs, and a real TLS 1.3 handshake. Exact-source focused Uvicorn `test_run_password` still fails solely at the CPython-shared TLS issuer verification error in 3.75 seconds. Current Release ALL_BUILD and CTest **53/53** pass. The XLang3 demo is running again at port 8765 (PID 30448); 1,000 real HTTP requests across 16 workers passed with zero errors in 41.729 seconds (`scratch/fastapi-demo-load-ssl-callback-20260927.log`), followed by a 30-second 16-worker soak of 332 requests with zero errors (`scratch/fastapi-demo-soak-ssl-callback-20260927.log`). The exact-source local FastAPI gate is running at `scratch/fastapi-local-ssl-callback-20260927.log`; do not claim it passed until exit. Full current-source Starlette/Pydantic/Pydantic-core/AnyIO reruns and other stated boundaries remain. No commit or push.

2026-09-27 AnyIO referrer design evidence: A CPython 3.14 probe confirms `gc.get_referrers(local_list)` returns no referrer for an ordinary live function frame (even when `sys._getframe()` is stored), but returns the owning generator for a generator local and the owning coroutine for a coroutine local. In both suspended-owner cases `sys._getframe().f_generator` identifies the actual generator/coroutine. XLang3's current implementation synthesizes a locals dict and returns it; it does not traverse tracked object edges, and its frame property is unconditionally None. `RuntimeFrameView` currently carries no generator owner and the VM frame snapshot is synthesized per access, so a correct fix must propagate owner identity into live frame materialization with safe lifetime handling, then enumerate real references in `gc.get_referrers` (including generator VM locals) without reporting a synthesized locals dict. This is an unresolved general runtime semantic gap, not a FastAPI test adaptation.

2026-09-27 live demo workflow on current Release build: `GET /api/runtime` reports `xlang3` 3.14.7 ready. A real HTTP task lifecycle created task id 1, toggled `completed` to true, deleted it, and confirmed zero remaining tasks. The demo remains running on port 8765, PID 30448 after the 1000-request load and 30-second soak.

2026-09-27 encrypted EC interoperability: XLang3 serialized a SECP256R1 private key with TraditionalOpenSSL PEM and BestAvailableEncryption; CPython 3.14 cryptography loaded it with the password and recovered private scalar 7. The reverse CPython-produced encrypted PEM loaded under XLang3 and recovered the same curve/scalar. Temporary PEMs are in `scratch/ec-xlang-encrypted.pem` and `scratch/ec-cpython-encrypted.pem`; neither runtime links to the other.

2026-09-27 upstream checkout hygiene: The full unchanged HTTPX test run generated an untracked OpenSSL keylog file named `test` inside its pinned checkout. It was moved intact to `scratch/httpx-generated-tls-keylog-cryptography-20260927.txt`; `git status --porcelain` is now empty for both HTTPX and Uvicorn checkouts. No upstream source was edited.

2026-09-27 exact-source local gate completed: `tests/fastapi/run_fastapi_tests.ps1` exited zero on the current native cryptography and lazy `_ssl` callback build with **251/251** `fastapi test ... ok` lines, including live Uvicorn HTTP and WebSocket (`scratch/fastapi-local-ssl-callback-20260927.log`). The current Release ALL_BUILD, CTest 53/53, demo 1000-request load, and 30-second soak also exited zero. Remaining work is the full upstream matrix on this final source, especially AnyIO generator/referrer semantics and three-second child pytest timeouts; the goal is not complete.

2026-09-27 current-source Starlette suite: Unchanged pinned Starlette 1.6.0 completed 1,052 passed, 8 upstream skips, 3 documented Windows deselections, 2 xfailed, zero failed in 147.54 seconds (`scratch/upstream-results-starlette-ssl-callback-20260927/starlette.log`). Its checkout remains clean. The exact-source Pydantic-core full suite is now running at `scratch/upstream-results-pydantic-core-ssl-callback-20260927/pydantic-core.log`; do not claim a result until exit. Pydantic, AnyIO, and final FastAPI full reruns remain open.

2026-09-27 generator ownership oracle expansion: `tests/fixtures/core/generator_frame_referrers.py` now records CPython 3.14 behavior for an ordinary function, a generator, and an asyncio coroutine. CPython emits normal owner None/referrers [], generator owner true/referrers ['generator'], coroutine owner true/referrers ['coroutine']. The current XLang3 binary emits a spurious ['dict'] for all three and reports false for generator/coroutine ownership. `tests/fastapi/gc_generator_referrers_contract.py` confirms a public async FastAPI route expects the current coroutine as the sole referrer. Source changes to frame ownership are in progress but not built or verified yet; the Pydantic-core run remains live on the preceding binary. Do not count these as fixed until the new runtime build and both oracles pass.

2026-09-27 generator/referrer current source: Pydantic-core 2.46.5 completed cleanly on the pre-generator-fix binary: 5,794 passed, 130 skipped, 10 xfailed in 565.81 seconds (`scratch/upstream-results-pydantic-core-ssl-callback-20260927/pydantic-core.log`). The later Release ALL_BUILD succeeded after a general runtime change that propagates live generator/coroutine ownership into `f_generator` and traverses tracked object references in `gc.get_referrers`. The new CPython 3.14 core oracle `generator_frame_referrers` and public FastAPI route `gc_generator_referrers_contract` match exactly; CTest 53/53 passes. Unchanged AnyIO `TestTCPStream.test_happy_eyeballs_refcycles` still has three failures: XLang3 returns the expected coroutine plus extra frame/list referrers, exposing a remaining general lifetime/object-graph difference. No upstream source was edited. The expanded 252-case FastAPI local gate is running; do not claim a result until it exits. Demo restarted on port 8765 (PID 26612) and `/api/runtime` reports XLang3 3.14.7 ready. Final-source full Pydantic-core and wider upstream matrix remain open. No commit or push.

2026-09-27 exception/GC refinement: CPython 3.14 oracle exposed that XLang3 did not clear `except ... as name` on raised/returned/broken/continued exits. The general IR lowering now executes cleanup on every exit; `except_target_cleanup` core fixture and FastAPI route match CPython. Retired traceback frames now release compiler-hidden locals, and comprehension aliases are cleared on normal completion; `comprehension_target_lifetime` core fixture and FastAPI route match CPython. Release ALL_BUILD and CTest 53/53 pass on this source. With a fresh `PYTHONPYCACHEPREFIX`, unchanged AnyIO `TestTCPStream.test_happy_eyeballs_refcycles` now passes Trio but fails asyncio and asyncio+eager solely because an `_OverlappedFuture` retains the socket exception. CPython 3.14 with its native `_asyncio` passes 3/3 applicable cases (`scratch/anyio-refcycles-cpython-native-asyncio-20260927.log`); the same unmodified tests under CPython with `_asyncio` disabled reproduce XLang3's exact 2-failed/1-passed/1-skipped pattern (`scratch/anyio-refcycles-cpython-no-asyncio-20260927.log`). Thus native `_asyncio` Future/Task parity is a true open native-boundary requirement, not a generic AnyIO or pure-Python patch. The current-source local FastAPI gate is running at `scratch/fastapi-local-gc-current-20260927.log`; do not claim full result until exit. Demo on port 8765 (PID 31540) reports XLang3 3.14.7 ready. No commit or push.

2026-09-27 comprehension closure follow-up: A CPython oracle found that clearing every hidden comprehension alias also erased a cell captured by an escaping lambda. The compiler now maps lambda free variables through active comprehension aliases and clears only aliases without a captured cell. `comprehension_target_lifetime.py` checks both that uncaptured targets are released and that escaping lambdas return `[2, 2]`; the public FastAPI route checks the same behavior. A temporary broad alias change broke `annotation_lambda_ast`; it was narrowed to comprehension aliases, and that fixture passes again. Latest Release ALL_BUILD and CTest 53/53 are green. The latest local FastAPI gate is running at `scratch/fastapi-local-final-gc-20260927.log` with a fresh XLang3 bytecode cache; do not claim it green before exit. The demo is running at port 8765 (PID 6652), `/api/runtime` ready. Native `_asyncio` Future/Task parity, final-source full upstream reruns, and load/soak remain open. No commit or push.

2026-09-27 binary-dependency audit: `dumpbin /dependents` over the current Release `xlang3.exe`, `xlang3_runtime.dll`, and all 23 built native package DLLs (25 binaries total) found zero `python*.dll` dependencies. This is direct binary evidence for the no-CPython-DLL invariant on the current Windows build; it does not by itself prove the full behavioral matrix.

2026-09-27 final local gates and live load: On the closure-corrected Release source, ALL_BUILD exited zero; CTest passed 53/53 including complete core fixtures; the expanded `tests/fastapi/run_fastapi_tests.ps1` gate exited zero with 254/254 `fastapi test ... ok` cases, including three new CPython-oracle routes and live Uvicorn HTTP/WebSocket (`scratch/fastapi-local-final-gc-20260927.log`). The running XLang3 demo (PID 6652, port 8765) passed 1,000 real HTTP requests with 16 workers and zero errors in 40.736 seconds (`scratch/fastapi-demo-load-final-gc-20260927.log`); the subsequent 30-second 16-worker soak completed 337 requests with zero errors in 31.528 seconds (`scratch/fastapi-demo-soak-final-gc-20260927.log`). `/api/runtime` still reports XLang3 3.14.7 ready. `git diff --check` is clean, and `dumpbin /dependents` showed no Python DLL dependency in 25 Release binaries. This is not goal completion: native `_asyncio` Future/Task parity and full final-source upstream matrix remain open, with the exact-source AnyIO refcycle result still 2 failed/1 passed/1 skipped against CPython's accelerated 3 passed/1 skipped. No commit or push.

2026-09-27 final-source upstream Starlette: Unmodified pinned Starlette 1.6.0 completed 1,052 passed, 8 upstream skips, 3 documented Windows deselections, 2 xfailed, zero failed in 146.71 seconds (`scratch/upstream-results-starlette-final-gc-20260927/starlette.log`). The runner exited zero and checkout remains clean. Full final-source reruns for other projects remain.

2026-09-27 HTTPX final-source full rerun: Unmodified HTTPX 0.28.1 collected 1,418 tests and reached the final `test_wsgi.py` file with three visible failures in the same multipart/util nodes as the prior CPython-shared baseline, but the session-scoped Uvicorn `server` fixture then timed out after 120 seconds in `thread.join()` (`scratch/upstream-results-httpx-final-gc-20260927/httpx.log`). This run cannot be counted as a complete result. Focused unchanged `test_wsgi.py` passed 12/12, and `test_api.py::test_get` plus all 12 WSGI tests passed 13/13 with the same server fixture (`scratch/httpx-wsgi-focused-final-20260927.log`, `scratch/httpx-server-teardown-final-20260927.log`); this points to a suite-scale/intermittent shutdown issue, not a deterministic WSGI test failure. Needs investigation and full final-source rerun. No upstream source edits.

2026-09-27 Pydantic-core final-source full rerun: Unmodified pinned Pydantic-core 2.46.5 completed **5,794 passed, 130 skipped, 10 xfailed, zero unexpected failures** in 568.82 seconds (`scratch/upstream-results-pydantic-core-final-gc-20260927/pydantic-core.log`). The matrix runner exited zero. Final-source Pydantic, FastAPI, Uvicorn, HTTPX, and AnyIO runs remain open; native `_asyncio` Future/Task parity remains a confirmed compatibility gap. No commit or push.

2026-09-27 `_asyncio` boundary isolation: The unchanged AnyIO `test_happy_eyeballs_refcycles` passed on CPython 3.14 when only `asyncio.Future` was switched to its pure-Python implementation while native `Task` remained (**3 passed, 1 Windows skip**, `scratch/anyio-refcycles-cpython-pyfuture-native-task-20260927.log`). Conversely, switching CPython to `_PyTask` while retaining native `Future` and switching the task registry functions to their pure-Python versions produced the extra `_OverlappedFuture` referrer in the ordinary asyncio case (`scratch/anyio-refcycles-cpython-native-future-pytask-registries-20260927.log`); the eager case additionally failed to find a current task because its factory was captured before the diagnostic monkeypatch. The first comparison establishes that a native `Task` is sufficient for this specific refcycle assertion even with pure-Python `Future`; it does not prove complete `_asyncio` parity or license a partial production shim. The full final-source Pydantic suite is running separately. No upstream source edits.

2026-09-27 Pydantic final-source full rerun: Unmodified pinned Pydantic 2.13.5 completed **5,804 passed, 261 skipped, 26 xfailed, zero unexpected failures** in 1151.59 seconds (`scratch/upstream-results-pydantic-final-gc-20260927/pydantic.log`). The matrix runner exited zero with assertion rewriting and pinned pytest 8.4.2; `matrix-results.json` records the pinned checkout and test environment. Final-source FastAPI, Uvicorn, HTTPX, and AnyIO full suites remain to resolve or complete. No commit or push.

2026-09-27 HTTPX final-source full rerun: After preserving the previous run's generated 54-byte OpenSSL TLS secrets log outside the pinned checkout, the unchanged HTTPX 0.28.1 suite completed **1,414 passed, 1 skipped, 3 failed, zero errors** in 130.04 seconds (`scratch/upstream-results-httpx-final-rerun-20260927/httpx.log`). The three nodes are the same text-mode multipart and two logging assertions reproduced by CPython 3.14 in this environment (`scratch/httpx-cpython-focused-baseline-20260927.log`). The earlier final-source session-fixture Uvicorn shutdown timeout did not recur, although its cause is unproven. The matrix runner exits nonzero due to the three shared upstream assertions; this complete run found no XLang3-only HTTPX failure. FastAPI, Uvicorn, and AnyIO final-source full suites remain. No commit or push.

2026-09-27 Uvicorn final-source full rerun: Unmodified pinned Uvicorn 0.53.0 completed **1,004 passed, 342 skipped, 6 failed, zero setup errors** in 494.21 seconds (`scratch/upstream-results-uvicorn-final-gc-20260927/uvicorn.log`). All six failures are local TLS issuer verification at the same nodes reproduced by CPython 3.14 (`scratch/uvicorn-ssl-cpython-baseline-20260927.log`). The matrix runner exits nonzero because of these shared certificate failures; no new XLang3-only failure appeared. Final-source FastAPI and AnyIO full suites and native `_asyncio` Task parity remain. No commit or push.

2026-09-27 FastAPI complete run and general generator completion oracle: Unmodified pinned FastAPI 0.141.1 finished **3,317 passed, 17 skipped, 4 xfailed, 7 failed** in 862.21 seconds on the pre-generator-result-fix binary (`scratch/upstream-results-fastapi-final-gc-20260927/fastapi.log`). Six failures are the known CPython-shared header snapshot cases. One new failure is `RuntimeError: invalid value is not hashable` from `_weakrefset.WeakSet.add` while creating a portal thread in `test_tuple_with_model_valid`; that exact node passes alone, as do standalone 5,000-thread and 5,000-TestClient-request stress probes. This suite-scale weakref issue remains unexplained and must not be described as solved. Independently, CPython 3.14 and XLang3 comparisons of `tests/fixtures/core/generator_result_release.py` and `tests/fastapi/generator_result_release_contract.py` show that XLang3 retains a completed coroutine result and returns completed generator/coroutine values again, unlike CPython. `src/runtime/generator.cpp` now stages a general completion cleanup and coroutine-reuse error, with CPython expected outputs and the public route added to the local gate. This edit is not yet built or verified; all prior upstream matrix results refer to the earlier binary. Demo PID 6652 still runs that earlier binary. No commit or push.

2026-09-27 completed-generator and delegated-await follow-up: The general completion cleanup now returns `None` for a second normal generator send, rejects reuse of an awaited coroutine, and releases the completed result as CPython 3.14 does (`tests/fixtures/core/generator_result_release.py`). That correction exposed an independent VM exception path: after a delegated coroutine handled an exception from an AnyIO worker or synchronous FastAPI endpoint and completed, `await_op` attempted to send into it again. `generator_throw` now marks its completed delegated result, and `await_op` consumes the already stored value once; `yield_from` clears the same state on its completion path. Minimal AnyIO worker-exception and FastAPI synchronous HTTPException probes now match CPython. Permanent core `delegated_exception_result.py` and public route `delegated_exception_contract.py` pass focused CPython/XLang3 comparisons, as do the generator-result route and the original cryptography HMAC route. Release ALL_BUILD and CTest 53/53 completed successfully (`scratch/fastapi-allbuild-delegated-await-result-20260927.log`, `scratch/fastapi-ctest-delegated-await-result-20260927.log`). The 256-case local FastAPI gate is running at `scratch/fastapi-local-delegated-await-result-20260927.log` and is not yet claimed green. The demo on the rebuilt binary is ready at port 8765 (PID 6776). Full upstream results above are pre-change and must be rerun. Native `_asyncio` Task parity and the suite-scale weakref failure remain unresolved. No commit or push.

2026-09-27 current-source gate and demo completion: The 256-case local FastAPI gate above completed with exit zero and **256/256** cases passed, including both new public routes and live Uvicorn HTTP/HTTPS/WebSocket (`scratch/fastapi-local-delegated-await-result-20260927.log`). The rebuilt demo passed 1,000 real requests with 16 workers and zero errors in 41.657 seconds (`scratch/fastapi-demo-load-delegated-await-20260927.log`), then a 30-second 16-worker soak of 329 requests with zero errors in 31.563 seconds (`scratch/fastapi-demo-soak-delegated-await-20260927.log`). `/api/runtime` still returns XLang3 3.14.7 ready on port 8765. `git diff --check` is clean. Full untouched upstream suites on this exact source, native `_asyncio` Task parity, and the intermittent suite-scale weakref hash failure remain open. No commit or push.

2026-09-27 upstream FastAPI rerun and general set hash fix: Unchanged pinned FastAPI 0.141.1 on the delegated-await-fixed binary completed **3,318 passed, 17 skipped, 4 xfailed, 6 failed** in 834.35 seconds (`scratch/upstream-results-fastapi-delegated-await-20260927/fastapi.log`). All six failures are the header snapshot assertions that also fail on CPython in the same environment. The prior `test_tuple_with_model_valid` WeakSet hash failure did not recur, so it must still be treated as intermittent or suite-state dependent. Separately, `scratch/set_rehash_probe.py` and the permanent `tests/fixtures/core/set_hash_caching.py` show a general mismatch: CPython hashes an object when inserting it into a set and retains that hash; XLang3 rehashed existing members during add and membership, permitting an existing member's `__hash__` to mutate the set. The runtime now stores a hash beside each set item and maintains that cache through add, membership, remove, copy, set operations, VM integer insertion, and marshal loading. The focused core fixture and public FastAPI route `set_hash_caching_contract.py` match CPython after the fix. The previous core fixtures `generator_result_release.py` and `delegated_exception_result.py` are now registered in both full fixture runners with this new set fixture. Release ALL_BUILD, CTest 53/53, and the full Python core fixture runner passed on the new source (`scratch/fastapi-allbuild-set-hash-cache-20260927.log`, `scratch/fastapi-ctest-set-hash-cache-20260927.log`, `scratch/fastapi-fixtures-set-hash-cache-20260927.log`). The expanded 257-case FastAPI integration gate is running at `scratch/fastapi-local-set-hash-cache-20260927.log`. Full upstream suites have not yet run on this new source, native `_asyncio` Task parity remains, and the suite-scale WeakSet root cause is not proven by the isolated set oracle. No commit or push.

2026-09-27 weakref hash cache and complete local gates: CPython 3.14 calls a live referent's `__hash__` only once across repeated `hash(weakref)` calls; XLang3 called it twice (`scratch/weakref_hash_cache_probe.py`). The native weakref boundary now checks its stored hash before consulting a live target, with CPython-oracle core `tests/fixtures/core/weakref_hash_cache.py` and public FastAPI route `tests/fastapi/weakref_hash_cache_contract.py` registered in the full gates. After an initial Windows linker temporary-file error, a build-local temporary directory allowed Release ALL_BUILD to finish; CTest passed 53/53, the full core fixture runner exited zero, and the expanded FastAPI integration gate exited zero with **258/258** cases (`scratch/fastapi-allbuild-set-weakref-cache-20260927.log`, `scratch/fastapi-ctest-set-weakref-cache-20260927.log`, `scratch/fastapi-fixtures-set-weakref-cache-20260927.log`, `scratch/fastapi-local-set-weakref-cache-20260927.log`). The rebuilt demo at port 8765 (PID 11768) passed 1,000 concurrent requests and a 30-second soak of 467 requests, both with zero errors, and `/api/runtime` remains ready (`scratch/fastapi-demo-load-set-weakref-cache-20260927.log`, `scratch/fastapi-demo-soak-set-weakref-cache-20260927.log`). `git diff --check` is clean. The unchanged pinned full FastAPI suite is running on this exact binary at `scratch/upstream-results-fastapi-set-weakref-cache-20260927/fastapi.log`; no outcome is claimed before exit. All other prior upstream suite results predate this source. Native `_asyncio` Task parity and the precise cause of the earlier one-off WeakSet failure remain open. No commit or push.

2026-09-27 full upstream FastAPI completion on set/weakref-cache source: The unchanged pinned FastAPI 0.141.1 suite completed **3,318 passed, 17 skipped, 4 xfailed, 6 failed** in 589.62 seconds (`scratch/upstream-results-fastapi-set-weakref-cache-20260927/fastapi.log`). The six failures are exactly the header snapshot assertions previously reproduced under CPython 3.14; no XLang3-only failure appeared. The prior one-off `test_tuple_with_model_valid` WeakSet error did not recur in this or the preceding complete run, but these runs do not establish its root cause or prove it cannot recur. The matrix runner exited nonzero because of the six shared assertions. All other upstream suite results predate this source. Native `_asyncio` Task parity and complete AnyIO coverage remain outstanding. No commit or push.

2026-09-27 current-source Pydantic-core full suite and AnyIO refcycle: Unchanged pinned Pydantic-core 2.46.5 completed **5,794 passed, 130 skipped, 10 xfailed, zero unexpected failures** in 341.67 seconds, with matrix runner exit zero (`scratch/upstream-results-pydantic-core-set-weakref-cache-20260927/pydantic-core.log`). A fresh-cache rerun of unchanged AnyIO `TestTCPStream.test_happy_eyeballs_refcycles` on the same binary remains 2 asyncio failures, 1 Trio pass, and 1 Windows skip (`scratch/anyio-refcycles-set-weakref-20260927.log`). The set and weakref caching fixes do not eliminate the extra `_OverlappedFuture` referrer; native `_asyncio.Task` behavior is still required. Starlette, Pydantic, Uvicorn, HTTPX, and complete AnyIO suites on this source remain to verify. No commit or push.

2026-09-27 current-source Starlette full suite: Unchanged pinned Starlette 1.6.0 completed **1,052 passed, 8 skipped, 3 documented Windows deselections, 2 xfailed, zero unexpected failures** in 125.23 seconds with runner exit zero (`scratch/upstream-results-starlette-set-weakref-cache-20260927/starlette.log`). The platform deselections are the same CPython-reproduced Windows cases in `tests/fastapi/upstream-platform-skips.json`. FastAPI, Starlette, and Pydantic-core now have complete current-source runs; Pydantic, Uvicorn, HTTPX, and complete AnyIO remain. No commit or push.

2026-09-27 current-source HTTPX full suite: Unchanged pinned HTTPX 0.28.1 finished **1,414 passed, 1 skipped, 3 failed, zero errors** in 118.08 seconds (`scratch/upstream-results-httpx-set-weakref-cache-20260927/httpx.log`). The failed nodes are the same text-mode multipart and two logging assertions that reproduce on CPython 3.14 (`scratch/httpx-cpython-focused-baseline-20260927.log`); no new XLang3-only failure appeared. The matrix runner exits nonzero due to these shared assertions. An OpenSSL-generated 54-byte TLS secrets file was moved from the upstream checkout to `scratch/httpx-generated-tls-secrets-set-weakref-post-20260927.log`, leaving the pinned checkout clean. Pydantic, Uvicorn, and complete AnyIO remain to rerun on this source. No commit or push.

2026-09-27 current-source Uvicorn full suite: Unchanged pinned Uvicorn 0.53.0 finished **1,004 passed, 342 skipped, 6 failed, zero setup errors** in 209.13 seconds (`scratch/upstream-results-uvicorn-set-weakref-cache-20260927/uvicorn.log`). The failed nodes are the same six local TLS issuer verification cases already reproduced under CPython 3.14 (`scratch/uvicorn-ssl-cpython-baseline-20260927.log`); no new XLang3-only failure appeared. The matrix runner exits nonzero due to those shared certificate failures. Pydantic and complete AnyIO current-source full suites remain. No commit or push.

2026-09-27 native `_asyncio` implementation boundary: The local unmodified Python 3.14.7 `Lib/asyncio/futures.py`, `tasks.py`, and `events.py` import `Future`, `Task`, awaited-by graph helpers, task registration/current-task helpers, and running-loop helpers from `_asyncio`. The [CPython 3.14.7 native source](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c) and [CPython asyncio internals](https://github.com/python/cpython/blob/main/InternalDocs/asyncio.md) show this is a substantial native Task/Future and per-thread task-state boundary, not simply a missing module alias. The focused unchanged AnyIO refcycle test still fails exactly in its two asyncio variants on XLang3 while accelerated CPython passes; earlier experiments show native CPython Task with pure-Python Future suffices for that one assertion, but that does not satisfy the full `_asyncio` import/API contract. No partial shim or FastAPI-specific change has been staged. The Pydantic full suite is running separately on the current binary.

2026-09-27 current-source Pydantic full suite: Unchanged pinned Pydantic 2.13.5 completed **5,804 passed, 261 skipped, 26 xfailed, zero unexpected failures** in 741.92 seconds with pinned pytest 8.4.2; runner exit zero (`scratch/upstream-results-pydantic-set-weakref-cache-20260927/pydantic.log`). FastAPI, Starlette, Pydantic, Pydantic-core, Uvicorn, and HTTPX now have complete current-source runs. AnyIO remains the incomplete suite with a confirmed native `_asyncio.Task` semantic gap and earlier nested pytest subprocess timeouts. No commit or push.
