# XLang3 FastAPI compatibility account handoff

Last updated: 2026-09-23 (America/Los_Angeles)

This file transfers the active engineering goal to a new Codex account on the
same Windows PC. The detailed chronological ledger is
[`fastapi.md`](fastapi.md). Read both files before editing. The current working
tree is authoritative.

## Current checkpoint (2026-09-23)

Latest checkpoint for merge to `main`: general exception-group selection,
exception constructor arguments, and sized file reads have CPython 3.14
oracles and pass their corresponding unchanged Starlette cases. Native
`_zstd` is now under `modules/zstd`, built from Zstandard 1.5.7 C sources
with no CPython ABI dependency. Unmodified Python 3.14 `compression.zstd`
imports on XLang3; ordinary and 192 KB streaming round trips match CPython.
The unchanged Starlette asyncio request-header case passes, and the FastAPI
TestClient integration tests actual `Content-Encoding: zstd` decoding.
Release CTest passes **53/53**, and the full local FastAPI integration runner
passes. The native `_zstd` API is not yet complete (`get_frame_info`, parameter
bounds, dictionary training/finalization, and advanced stream options remain).
The untouched full upstream suites and Trio/CFFI boundary remain open; this is
a tested checkpoint, not completion of the FastAPI compatibility goal.

The user's requested checkpoint was committed as `58b939d`, fast-forwarded to
`main`, and pushed to `origin/main`. `fastapi-compatibility`, `main`, and
`origin/main` all pointed to that commit at the start of this continuation.
The current checkout is `fastapi-compatibility`. Ignore unrelated generated
and scratch files in `git status`.

The production FastAPI integration runner passed after the 8 MB Windows stack
reserve, `dict.pop` dispatch, and Pydantic mapping-input fixes. Release CTest
passed **53/53** again on 2026-09-23 (35.39 seconds). The untouched serial
FastAPI suite collected **3335 items / 10 skipped** and reached 5% before the
checkpoint pause; `test_upload_file[trio]` failed because the native CFFI
`CLibrary` has no `CreateIoCompletionPort` symbol. The untouched Starlette
suite collected **1065 items**; its first five failures are the same Trio
Windows CFFI boundary. Neither full upstream suite is green.

This continuation adds `ast.Expression` compilation in eval mode, operator
conversion, parser-backed `ast.parse(..., mode="eval")`, `ast.Interactive`
compilation, dynamic `eval()` locals mapping lookup, exception-class
constructor semantics for `raise`, `_contextvars.Context.run` keyword
forwarding, Python-sign modulo in the VM's constant path, and
`bytes.rjust`/`bytearray.rjust`. CPython 3.14 oracle fixtures cover these.
`-k` selection now works. Untouched Starlette asyncio session tests pass
10/10, WSGI tests 6/6, and body-limit tests 18/18. A real TestClient
session cookie persists across requests and verifies with itsdangerous.
The final Release build passed CTest 53/53 and all production FastAPI
integration runner cases, including Uvicorn end-to-end. The complete
upstream suites are still not green.

After `a742cec`, uncommitted work fixes `BaseExceptionGroup` constructor
selection for ordinary exceptions. The CPython 3.14 oracle and a public
FastAPI task-group route pass, as do Release CTest 53/53 and the full FastAPI
integration runner. Untouched Starlette `test__utils.py` asyncio cases pass
13/13; application/authentication/background/concurrency cases pass 42/42.
The next 77-case Starlette selection has 76 passes and one upstream Windows
failure: `test_missing_env_file_raises` interpolates `C:\Users` into a regex,
causing `re.PatternError` under both CPython 3.14 and XLang3. The full
upstream suite and the Trio/CFFI boundary remain open.

Another untouched Starlette selection (form parser, requests, responses)
collected 168 asyncio cases: 164 passed, 3 failed, 1 upstream skip. CPython
passed all three failures. Two XLang3 failures are now fixed in uncommitted
work: custom exception subclasses retain constructor `args` without calling
`super().__init__`, and buffered `read(65536)` of a 14 KB regular file returns
the whole file. The exact upstream form-limit and FileResponse cases pass;
CPython oracles and public FastAPI TestClient file download pass. The third
failure is missing HTTPX2 `zstd` advertisement. HTTPX2 imports Python 3.14's
pure-Python `compression.zstd`, which requires native `_zstd`. Implement the
true `_zstd` boundary under `modules/` instead of special-casing HTTPX or
FastAPI; never load CPython's `_zstd.pyd`.
The final Release CTest rerun passes 53/53, and the full FastAPI integration
runner passes after the file-read fix. A preceding full CTest run had one
intermittent native-network large-response failure; the isolated test and
subsequent complete rerun passed.

The other major frontier is generated CFFI ABI support in
`modules/cffi/cffi_backend_module.cpp`: callable DLL symbols, owned C data,
arrays/structures, `ffi.new`, `ffi.from_buffer`, `ffi.sizeof`, and Win32 error
state. Trio's unchanged `_generated_windows_ffi.py` supplies signatures and
declarations. Do not substitute a Trio-specific function shim.

## First action in the new account

Open `D:\CantorAI\xlang3`, start a Codex task, and ask it to create this goal
before doing any work:

> Make XLang3 fully compatible with the pinned FastAPI production stack on the
> `fastapi-compatibility` branch. Establish and execute an automated
> compatibility matrix using unmodified upstream FastAPI, Starlette,
> Pydantic/pydantic-core, AnyIO, Uvicorn, and HTTPX tests; compare behavior with
> CPython 3.14; fix general XLang3 runtime semantics or true native dependency
> boundaries only; never depend on the CPython runtime, DLL, C ABI, or binary
> extensions; never replace pure Python standard-library or third-party code
> with C++; add minimal CPython-oracle regressions and public FastAPI
> integration coverage for every fix; validate real HTTP, HTTPS, WebSocket,
> streaming, multipart/file, middleware, authentication, lifecycle,
> cancellation, concurrency, malformed-input, load, and soak behavior;
> maintain an explicit report of passes, failures, justified platform skips,
> and unsupported behavior; finish with a clean Release build, complete
> fixture/FastAPI/CTest gates, and a running production-style demo. Do not
> commit or push unless the user asks.

Then instruct it to read this file and `fastapi.md`, inspect the current branch
and diff, and resume at **Current frontier** below. Do not let it reset, clean,
stash, commit, or discard the existing working tree.

## Non-negotiable design rules

- XLang3 replaces CPython for this stack. It must not load or depend on the
  CPython runtime, `python314.dll`, CPython's C ABI, `.pyd` modules, or other
  CPython binary extensions.
- Use the Python 3.14 pure-Python standard library sources with XLang3.
- Never reimplement a pure-Python standard-library or third-party library in
  C++ merely to make a test pass.
- Native code belongs in the runtime only when it is small and broadly common.
  Larger or dependency-specific native boundaries belong under `modules/`.
  A native implementation can still live in the runtime when that is the
  natural general runtime boundary; do not force artificial package splits.
- If FastAPI needs a genuine native dependency boundary, implement it generally
  under `D:\CantorAI\xlang3\modules` or, when naturally small/common, in the
  runtime. Do not add FastAPI-specific branches, fixtures, monkeypatches, copied
  tests, fake results, or workarounds.
- Run upstream projects and source libraries unchanged. Fix XLang3 semantics or
  the true native dependency boundary exposed by failures.
- Add a minimal CPython 3.14 oracle regression for general runtime fixes. Match
  CPython's real output; do not invent expected behavior.
- Do not repeat completed compatibility work from the earlier Complete XLang3
  effort. Inspect the current implementation and ledger first.
- Do not claim full compatibility from imports, smoke tests, or the FastAPI
  suite alone. The complete pinned upstream matrix and production protocol
  gates are required.
- Do not commit or push unless the user explicitly requests it.

## Repository and build state

- Repository: `D:\CantorAI\xlang3`
- Branch: `fastapi-compatibility`
- Release executable: `D:\CantorAI\xlang3\build\Release\xlang3.exe`
- CMake executable:
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- Inspect `git status --short` before work. Preserve unrelated generated
  files; do not run broad cleanup commands.
- The first published checkpoint is `58b939d` on `origin/main`; inspect the
  current branch and remote for subsequent checkpoints.

Set Windows build temp directories before compiling:

```powershell
$env:TEMP='C:\Users\shaw9\AppData\Local\Temp'
$env:TMP=$env:TEMP
```

Build the runtime:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build build --config Release --target xlang3 -- /m:1
```

Build native pydantic-core:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build build --config Release `
  --target xlang_pydantic_core_native_package -- /m:1
```

## Test environment

For the currently installed isolated targets:

```powershell
$env:PYTHONPATH='D:\CantorAI\xlang3\scratch\fastapi-test-deps;D:\CantorAI\xlang3\scratch\fastapi-deps;D:\CantorAI\xlang3\scratch\upstream-compat\pydantic\pydantic-core'
$env:PYTEST_DISABLE_PLUGIN_AUTOLOAD='1'
$env:PYTHONPYCACHEPREFIX='D:\CantorAI\xlang3\scratch\pycache-choose-a-new-unique-name'
```

Use pytest with:

```text
--assert=plain -c D:\CantorAI\xlang3\scratch\empty_pytest.ini
```

Prefer exact node IDs or complete files for targeted diagnostics. `-k` now
works after implementing eval AST compilation and dynamic custom locals
mapping lookup.

Pinned production dependencies are in `tests/fastapi/requirements.txt`:
FastAPI 0.141.1, Starlette 1.6.0, Pydantic 2.13.5, pydantic-core 2.46.5,
AnyIO 4.15.1, Uvicorn 0.53.0, HTTPX 0.28.1, plus the fully pinned production
graph. Test tooling is independently pinned in
`tests/fastapi/requirements-test.txt`. Exact upstream commits and source/test
directories are in `tests/fastapi/upstream-matrix.json`.

## Verified major gates

These results were obtained with the production Release XLang3 executable and
unmodified upstream/source libraries unless explicitly described otherwise:

- Untouched FastAPI suite collected all 3,345 tests: **3,315 passed, 36
  upstream skips, 4 expected failures**, plus one teardown-only shared-directory
  race after all behavioral assertions passed. CPython 3.14.7 reproduces that
  same race under two file workers; XLang3 passes the two files serially 7/7.
  Logs: `scratch/fastapi-full-loadfile-8.log`,
  `scratch/fastapi-static-race-cpython.log`, and
  `scratch/fastapi-static-serial-xlang.log`.
- Latest rebuilt Release CTest baseline: **53/53 passed** in 33.55 seconds.
  The previous SQLite/coroutine representation assertions and PowerShell
  negative-fixture wording are reconciled with CPython output.
- Unmodified Visual Studio debugpy launch lifecycle: **20/20 consecutive
  sessions passed**.
- Full untouched pydantic-core serializer directory: **797 passed, 6 upstream
  skips, 1 expected failure** (**804 collected**). The union serializer is
  independently **106/106 passed**.
- The current 13-file untouched validator regression batch is **840 passed
  with 2 expected failures**. It includes bool, bytes, float, string, tuple,
  complex, custom-error, chain, lax-or-strict, json-or-python, with-default,
  callable, and recursive definitions. Recursive definitions are now **36/36**;
  tuple is **89/89**; with-default is **130 passed with 2 expected failures**.
  Separate gates pass arguments **201 with 50 upstream skips**, partial
  validation **14/14**, and call/definitions/dict/enum/frozenset **168 with 1
  upstream skip**.
- The sequential untouched validator sweep has also closed function **58/58**,
  generator **38/38**, integer **239/239**, is-instance **33/33**,
  is-subclass **14/14**, JSON **33/33**, list **96/96**, literal **46/46**,
  model-fields **308 passed with 1 upstream skip**, model-init **14/14**, and
  model-root **9/9**. The model file is **41 passed, 3 failed, and 1 upstream
  skip** across 45 collected, improved from 14 passed and 30 failed. All
  implementation cases pass; the three remaining comparisons require pytest
  assertion rewriting, which currently collects zero tests under XLang3.
- None, nullable, pickling, and set validators now pass **85/85** untouched.
  This includes exact JSON `None` errors, aggregated unhashable set items,
  smart list/set union selection, native timezone pickle round trips, and
  collection of callback cycles crossing a native schema validator. The
  runtime fix publishes native-owned Python references to cycle GC and drops
  dead call-result temporaries inside loops; `function_attribute_gc.py`
  provides the CPython 3.14 oracle.
- The model-fields file is closed with **0 failures** across **309 collected**,
  improved from 114 passes in the first untouched run. Model-init is also
  closed after implementing extras propagation, custom-init revalidation,
  custom-init union selection, and correct `__slots__`/`__dict__` separation.
- Date, datetime, time, and Decimal validator files: **696/696 passed**.
- Timedelta validator and serializer files: **165 passed, 2 skips**.
- UUID validator file: **97/97 passed**.
- Real SQLModel request: `GET /heroes/` returned `200 []` through unchanged
  SQLAlchemy/SQLModel and native SQLite DB-API support.
- The checked-in FastAPI integration runner validates the XLang3 interpreter
  before gates. It includes real Uvicorn HTTP and HTTPS coverage.
- The interactive production-style demo exists at
  `tests/fastapi/demo_website.py` and has run under real Uvicorn. It must be
  restarted and reverified as part of the final gate after the account switch.

The complete implementation and regression history, including `_ssl`, hashing,
Argon2, CFFI boundaries, Python semantics, debugpy, Coverage, Typer, Rich,
SQLAlchemy, and fixture names, is recorded in `fastapi.md`. Do not infer that a
missing item here is unfinished without checking that ledger.

## Earlier frontier: remaining untouched pydantic-core validator sweep

Active source file:

```text
D:\CantorAI\xlang3\scratch\upstream-compat\pydantic\pydantic-core\tests\validators
```

Implementation:

```text
D:\CantorAI\xlang3\modules\pydantic_core\pydantic_core_module.cpp
```

`test_model_fields.py`, `test_model_init.py`, and `test_model_root.py` are
closed. The recursive-definition file is also closed at **36/36**, including
definition-aware reprs, cyclic mapping and attribute input, recursive
assignment, wrap/before/after validators, and complex JSON-like unions.
Preserve those results and continue the remaining validator files exactly as
supplied upstream.

Untouched `test_model.py` now has **41 passed, 3 failed, and 1 upstream skip**.
All implementation cases pass. The three validator error-payload comparisons
depend on pytest assertion rewriting; the required `--assert=plain` runner
gives both XLang3 and CPython an empty `AssertionError` message, while enabling
rewriting currently makes XLang3 collect zero tests.

Latest authoritative progress:

- The complete untouched URL validator file passes **378 tests with 6 expected
  failures** in 7.72 seconds using the Release XLang3 executable.
- The complete untouched URL serializer file passes **10/10**. URL and
  multi-host URL schemas now warn on unexpected values, `any_schema()` performs
  recursive JSON-mode URL inference (including dictionary keys), and both
  native URL classes implement constructor-based pickle reduction.
- The combined untouched URL, UUID, Decimal, datetime/date/time, and timedelta
  serializer batch passes **138 tests with 1 upstream skip** in one process.
  This includes temporal ISO/seconds/milliseconds modes, UTC `Z`, inferred and
  typed JSON keys, warning fallbacks, and precision-sensitive timestamps.
- The complete untouched bytes serializer file passes **16/16**. It covers
  strict UTF-8 error types/messages, bytes subclasses and bytes Enum members,
  UTF-8/base64/hex modes, inferred values and keys, model-level configuration,
  and the public recursive `to_json()` helper. The CPython-oracle fixture
  `bytes_subclass_protocol.py` matches CPython for inherited methods, join
  iterator exception propagation, and ordinary decoder diagnostics.
- The untouched complex serializer file passes **13/13**.
- The combined untouched none, nullable, literal, simple scalar, and string
  serializer batch passes **98 tests with 1 upstream skip**. This closes empty
  literal schema rejection, enum-literal tagged-union serialization, arbitrary
  precision integers, numeric and string subclasses, `IntEnum`, JSON-mode
  numeric normalization, recursive warning fallbacks, all configured non-finite
  float encodings, `ensure_ascii` (including surrogate pairs), warning
  truncation and warn/none/error policy, and string Enum normalization.
  `numeric_subclass_conversion.py` is the CPython oracle for inherited
  `int.__float__` and NaN passthrough identity.
- The untouched enum and format serializer files pass **20/20**.
- The complete untouched any serializer file passes **99 tests with 2
  upstream Windows skips**. It covers inferred models/dataclasses (including
  slots), sets, iterators, regex, pathlib, IP address types, temporal and
  arbitrary-precision numeric values, recursive include/exclude, fallback and
  cycle handling, non-finite float policies, dictionary keys, and public
  `to_json()` keyword modes. Native submodule imports now bind the child on an
  already-loaded parent package, matching CPython import semantics.
- The untouched dataclass serializer file passes **15 tests with 1 expected
  failure**. Schema-level exclusion predicates, computed fields, init-only
  fields, aliases, and `serialize_by_alias` config/runtime precedence are now
  implemented.
- The untouched definitions serializer file passes **7/7**. Schema creation
  rejects duplicate refs across the complete schema graph, and definitions
  remain available to later sibling serializers.
- The untouched recursive-definitions serializer file passes **5/5**,
  including typed recursive cycles, custom serializers, and repeated-reference
  detection in Python and JSON modes.
- The untouched dictionary serializer file passes **28/28**, including pretty
  JSON indentation, arbitrary keys, schema/runtime filters, nested selectors,
  `__all__`, and selector precedence. The untouched function serializer file
  passes **30 tests with 2 upstream platform skips**, including plain/wrap
  callables, `SerializationInfo`, return schemas, exception chaining,
  selector-aware handlers, model fields, and unexpected-value fallback.
- The full untouched serializer-directory probe collected **804 tests** and
  now passes completely: **797 passed, 6 upstream skips, 1 expected failure**
  across all **804 collected tests**. Generator **6/6**,
  infer **3/3**, JSON **4/4**, JSON-or-Python **2/2**, list/tuple **63/63**,
  and literal **6/6** pass untouched. This added unsized and sized selector
  merging, tuple fallback/length warnings, collected `warnings='error'`, JSON
  round-trip serialization, enum-value inference, and correct JSON/Python
  branch selection. The complete untouched model serializer file now passes
  **67/67**, covering model/dataclass field serializers, computed fields,
  aliases, extras, nested selectors, missing-field warnings, exception
  wrapping, round-trip mode, and `exclude_computed_fields`. Model-root passes
  **8/8**, other passes **7/7**, and `serialize_as_any` passes **12/12**.
  Runtime-type dispatch now covers model/dataclass subclasses, unrelated and
  nested models, typed dictionaries, root models, field serializers, and
  recursive models. Root-model validation stores its validated payload in
  `.root`, and unrelated model schemas no longer consume another model's
  Pydantic extras. Set/frozenset fallback passes **7/7** without false cycle
  detection. Simple scalar serialization passes **39 tests with 1 upstream
  skip**; the Any and typed-float non-finite policies are deliberately
  distinct and their combined exact regression passes **13/13**. Resume by
  continuing with the remaining untouched validator matrix.
  The untouched TypedDict serializer file passes **37/37**, including input
  order, aliases, allowed extras, custom extra schemas, selectors, and correct
  owner objects for nested field serializers whose values are `None`.
- The untouched union serializer file passes **106/106**. Union selection now
  handles literals, structural TypedDict alternatives, exact and fallback
  branches, serializer rejection, nested unions, model subclasses, tagged
  unions, discriminator alias paths, JSON keys, and combined per-branch
  diagnostics matching CPython/pydantic-core formatting.
- WHATWG preprocessing now removes ASCII TAB, LF, and CR anywhere in URL input.
- Authority-bearing non-`file` URLs now reject an empty authority as
  `empty host`.
- The native pydantic-core target rebuilt successfully after both changes.
- Exact untouched node
  `test_url_cases[http://-expected5-SCHEMA_VALIDATOR]` passes **1/1**.
- General IDNA normalization now validates ASCII `xn--` labels through
  XLang3's existing `idna` codec, keeps the Unicode host form, and translates
  codec failures into structured `url_parsing` errors. The exact malformed
  punycode nodes for schema validation and direct `Url` construction pass 2/2.
- URL parsing, validation, strict syntax, conversions, schema constraints,
  construction, security, serialization, subclasses, and pickle round trips
  are closed. Resume with untouched enum serialization, then continue the
  remaining pydantic-core validator and serializer matrix.

Completed validator command:

```powershell
& 'D:\CantorAI\xlang3\build\Release\xlang3.exe' -m pytest `
  'D:\CantorAI\xlang3\scratch\upstream-compat\pydantic\pydantic-core\tests\validators\test_url.py' `
  --assert=plain -c 'D:\CantorAI\xlang3\scratch\empty_pytest.ini' -x -q
```

Two non-verbose URL serializer runs intermittently reported unrelated pytest
hook-binding errors at the second subclass parameter; the exact node passed and
a fresh verbose full-file run passed 10/10. Keep watching this symptom in larger
serial runs. A serializer configuration scope currently uses thread-local RAII
for nested synchronous calls; later suites may show that lazy serialization
iterators must retain configuration themselves.

## Remaining work required before completion

1. Finish untouched pydantic-core scalar, URL, validator, and serializer test
   matrices and keep general regressions green.
2. Validate the independently pinned clean test dependency target. PyYAML's
   unchanged pure-Python implementation already passes its tutorial tests 4/4;
   Trio's `_cffi_backend` boundary and complete clean-environment preflight
   remain open.
3. Execute the complete seven-project matrix from
   `tests/fastapi/run_upstream_matrix.ps1` against unchanged FastAPI, Starlette,
   Pydantic, pydantic-core, AnyIO, Uvicorn, and HTTPX checkouts. Preserve logs
   and `matrix-results.json`; classify every skip/failure honestly.
4. Validate real HTTP, HTTPS, WebSocket, streaming, multipart/file, middleware,
   authentication, lifecycle, cancellation, concurrency, malformed input,
   load, and soak behavior. Existing integration files provide part of this
   coverage; audit them against the full goal rather than assuming completion.
5. Run a clean Release build and all final fixtures, FastAPI integration gates,
   upstream suites, and CTest. Re-run tests after any fixes that affect covered
   behavior.
6. Start `tests/fastapi/demo_website.py` with the Release XLang3 executable and
   real Uvicorn, verify its feature routes in a browser/client, and leave the
   production-style demo running for user inspection.
7. Update `fastapi.md` with exact pass/fail/skip evidence and unsupported
   behavior. Mark the goal complete only after every requirement above has
   current authoritative evidence.

## Account and task continuity

The original Codex account's task history and internal goal do not transfer to
another account. The local branch, files, build outputs, scratch checkouts, and
uncommitted edits remain on this PC. The new account must create the goal quoted
at the top of this document. Treat this Markdown file plus `fastapi.md` and the
current working tree as the handoff record.
