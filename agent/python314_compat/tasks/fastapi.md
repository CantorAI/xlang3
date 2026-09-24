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
