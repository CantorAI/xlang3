<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# Compatibility Loop Lessons

Keep this file short and practical. Add lessons only when a batch exposes a
mistake pattern, a recurring compatibility trap, or a workflow rule that should
shape the next iteration.

- Use `rg -F` for literal searches in PowerShell. Audit checkboxes, backticks,
  brackets, quotes, C++ punctuation, and Python syntax are literals unless a
  regex is explicitly needed.
- Do not update expected fixture output blindly. First compare with Python 3.14
  or the intended XLang3 runtime contract, then decide whether the runtime or
  the golden output is wrong.
- Every claimed compatibility feature needs a concrete fixture assertion. Add
  assertions to the combined section fixture when possible; add a focused
  fixture when the behavior deserves isolation.
- Do not start a second loop while one is running. Use the loop lock and the
  stop-request file so a human stop exits after the current iteration.
- Codex CLI output is UTF-8. When the loop captures child output on Windows, do
  not use the console default code page; decode as UTF-8 with replacement so
  warnings or Unicode diagnostics cannot crash the runner.
- Git command output captured for prompt/commit generation can also contain
  UTF-8 diff text. Decode helper output as UTF-8 with replacement instead of
  using the Windows console default codec.
- In continuous mode, one empty Codex batch should not stop the goal. Treat it
  as no-progress and retry a small fixed number of times before exiting.
- Compatibility fixtures must run headless. Windows crash/report dialogs from
  child `xlang3.exe` processes block the loop, so the CLI and Python fixture
  runners both disable popup error UI and report failures in console output.
- A passing direct fixture run is not enough for threaded/runtime-sensitive
  code. Also test it through the fixture runner with captured stdout/stderr,
  because pipe output and process teardown can expose different lifetime bugs.
- Do not “fix” thread crashes by turning on a coarse global VM lock. It can
  deadlock fixtures that intentionally wait across threads; fix ownership,
  teardown, and lock boundaries instead.
- Keep Python and native thread target execution-lock boundaries symmetric.
  Inherited trace/profile hooks exercise frame snapshots from worker threads and
  can expose crashes when only the native target path is locked.
- Descriptor primitive changes often need both `object_get_attr` and the VM
  fast-path attribute helper updated; otherwise explicit descriptor calls and
  compiled attribute access can diverge.
- Native module functions that should raise catchable Python exceptions must
  call `runtime.raise_class_error`; returning `false` with only an error string
  can surface as an uncaught runtime failure instead.
- The selected section runner executes a section fixture but does not compare
  it with the golden output. After adding or reordering fixture assertions, run
  the full fixture script or do an explicit line comparison before trusting the
  expected-output file.
- A failed build or fixture run should become a repair prompt in the same Codex
  session. Preserve the changed batch, feed back the captured validation log,
  fix the regression, and only commit after the next validation pass.
- Native functions that should reject keyword arguments with a catchable Python
  `TypeError` need an explicit keyword callback; otherwise the generic native
  dispatcher can surface the rejection as an uncaught runtime failure.
- Container subclass construction may depend on inherited native `__init__`
  methods even when exact builtin construction is handled by VM constructor
  fast paths. Add the initializer on the builtin type and cover both subclass
  construction and direct `dict.__init__`-style calls when exposing it.
- Shared structseq method implementations need the same catchable
  `TypeError` and explicit keyword-callback handling as direct native methods;
  one bare registration can leak raw runtime errors across every structseq type.
- Check whether a native-backed public stdlib API is originally a Python
  function before rejecting keywords. Public helpers may need keyword binding
  even when adjacent private C helper functions reject all keywords.
- Deprecated descriptor wrappers can expose compatibility state on the wrapper
  itself rather than mutating wrapped callables. Check CPython-visible descriptor
  attributes before assuming accessor marker propagation.
- `property.__doc__` needs an internal "inherited from getter" distinction:
  replacement getters recompute inherited docs, while explicit docs remain stable.
- `property.__name__` follows the same inherited-versus-explicit cloning shape
  as property docs in Python 3.14, with deletion resetting to the getter name.
- PowerShell does not accept Bash-style `<<'EOF'` heredocs. Use `python -c`
  or an existing fixture/script path for CPython probes on Windows.
- Class and callable metadata checks should pin visible doc text when an audit
  row claims CPython-shaped docs. A non-`None` doc assertion can hide
  compatibility-visible one-line summaries where CPython exposes structured
  multi-line documentation.
- When fixture checks pin CPython multi-line docs, choose substrings that do not
  cross line-wrapping boundaries unless the newline itself is intentional.
  CPython doc text can split phrases such as `profiler\nchapter`, so prose
  substring checks must be based on the probed `repr`.
- Intern-table size probes can mutate the value they are measuring because
  keyword names, metadata strings, or fixture temporaries may be interned during
  the probe. Assert monotonic relationships or relative bounds instead of exact
  equality across successive intern-count calls.
- Structseq type metadata belongs in the shared builder. CPython-visible
  `__module__`/`__qualname__` gaps can otherwise affect every generated
  `sys.*_info` type even when instances, descriptors, and reprs look correct.
- Type reprs need to derive from visible class `__module__`/`__qualname__`
  metadata while eliding `builtins`; setting those attrs alone can still leave
  CPython-visible `<class 'module.name'>` mismatches if the shared repr path
  uses only the internal class name.
- Float display precision is a shared value-model primitive. Low default C++
  stream precision can leak into structseq reprs such as `sys.float_info`, so
  fix the central float formatter and cover both the structseq repr and direct
  `repr()`/`str()` cases.
- When startup metadata depends on bootstrap modules registered after `sys`,
  publish the already-created runtime objects from the later registration step
  instead of inventing placeholder objects or forcing a broader registration
  reorder.
- Exception detection cannot rely only on names ending in `Error` or
  `Exception`; CPython-visible builtins such as `SystemExit`, `GeneratorExit`,
  and `StopIteration` still need normal `BaseException` string/args behavior.
- Lambda lowering reuses `clone_expr`; when adding expression forms, cover both
  plain literals and comprehensions. Missing `DictExpr`/`SetExpr` clones can
  silently turn lambda bodies into `None` even when normal function returns work.
- Builtin constructor fast paths must mirror native `__new__` metadata side
  effects. `type(name, bases, namespace)` can bypass `type.__new__`, so class
  `__module__`/`__qualname__` defaults need coverage for both direct `type()`
  construction and class statements.
- Shared time tuple helpers have different contracts depending on caller:
  `time.struct_time` construction accepts 10/11-field extra metadata, while
  `mktime`/`strftime`/`asctime` consume exactly 9-field time tuples.
- `sys.exit(status)` does not behave like direct `SystemExit(status)` for tuple
  statuses: CPython unpacks tuple status through exception normalization before
  computing `SystemExit.args` and `code`.
- Direct `abc.abstractmethod()` marker writes are stricter than deprecated ABC
  descriptor wrapper construction: direct staticmethod/classmethod/native
  callable targets should raise `AttributeError`, while `abstractstaticmethod`
  and `abstractclassmethod` still create marked wrappers.
- Direct `abc.abstractmethod()` on runtime builtin classes fails with immutable
  type `TypeError`, not marker-write `AttributeError`; identify builtin class
  objects by runtime registry identity rather than visible `__module__` alone.
- Time tuple consumers and `time.struct_time` construction have different
  accepted input types as well as lengths: `mktime`/`strftime`/`asctime`
  reject lists, while `struct_time` construction accepts sequence inputs.
- Attribute-compatible objects are not always acceptable to CPython C-style
  APIs. `mktime`/`strftime`/`asctime` require a tuple or exact `time.struct_time`
  instance, not an arbitrary object with `tm_*` fields.
- `sys.getsizeof(obj, default)` falls back for missing/non-callable `__sizeof__`
  or a `TypeError` raised while calling it, but a successful `__sizeof__` call
  that returns a non-int still raises the normal `TypeError` diagnostic.
- When a native compatibility fallback is keyed to a Python exception type,
  match subclasses with `class_is_subclass` instead of checking the exact class
  name; `sys.getsizeof(..., default)` treats `TypeError` subclasses like
  `TypeError`.
- For protocol helpers with optional default fallback, cover the no-default
  diagnostic separately. `sys.getsizeof()` falls back for a non-callable
  `__sizeof__` only when a default is supplied; otherwise it exposes the normal
  non-callable TypeError text.
- Public Python aliases for native helper functions can still expose native
  module names in CPython diagnostics. `abc.get_cache_token(x=...)` reports
  `_abc.get_cache_token() takes no keyword arguments`, so keyword callbacks may
  need explicit function-qualified user data rather than deriving names from the
  public registration site.
- CPython `_abc` helper arity diagnostics are not uniform across helpers:
  one-argument helpers use fully qualified `takes exactly one argument (N given)`
  text, but `_abc_register`/`_abc_subclasscheck`/`_abc_instancecheck` use their
  short C helper names in `expected 2 arguments, got N` messages. Probe each
  helper family before sharing one arity formatter.
- Native wrappers that delegate to an equivalent implementation still need
  their own CPython-visible name in arity diagnostics. `sys._clear_type_cache`
  can share behavior with `_clear_internal_caches`, but its positional error
  must report `sys._clear_type_cache()`, not the callee helper.
- Tuple-backed native types may need an explicit `__str__` even when `__repr__`
  is correct. CPython displays `time.struct_time` with the named-field repr for
  both `repr()` and `str()`, so cover both display entry points.
- User-facing output hooks must use descriptor-aware display conversion, not the
  low-level value formatter. `sys.displayhook(obj)` is a `repr(obj)` surface and
  must honor native instance `__repr__` methods such as `time.struct_time`.
- Protocol-return diagnostics often include the returned object's visible type.
  `sys.displayhook()` reports `__repr__ returned non-string (type int)`, so cover
  the error text, not just that a `TypeError` was raised.
- Avoid hand-rolled display escaping when a shared `repr` primitive exists.
  `sys.displayhook("don't")` must use CPython's quote-selection rules, so route
  string output through `value_to_repr` instead of duplicating partial escaping.
- `sys.displayhook` clears `builtins._` before invoking `repr(obj)`, not after.
  Protocol callbacks can observe hook state, so fixture both the callback-visible
  state and the final `_` value when changing displayhook ordering.
- Native function metadata often has multiple CPython-visible channels. Do not
  overload keyword-diagnostic `user_data` with `__text_signature__`; store text
  signatures explicitly in the native attrs dict and keep CPython-normal `None`
  for helpers that do not expose one.
- Native callable docs can contain CPython quirks that do not follow from the
  visible function name. For example, Python 3.14 reports
  `process_time() -> int` for `time.process_time_ns.__doc__`, so probe docs
  directly and fixture stable substrings or exact short docs.
- The Standard Modules section runs close to the fixed 60-second full-suite
  case timeout. If full validation times out there after the selected section
  passes, stop new feature work, rerun the fixed selected comparison to isolate
  marginal duration versus hang, and only rerun full validation after that check.
- When a section fixture is already near the fixed case timeout, do not add
  metadata-only assertions to that monolithic section as an isolated batch.
  Prefer a focused already-run fixture or defer the metadata expansion until the
  section runtime has headroom.
- Directive aliases can be platform-specific in CPython `strptime`. Probe the
  active Python 3.14 build before accepting POSIX aliases such as `%h` on
  Windows, and let partially valid parsers report leftover suffixes through the
  shared trailing-data path instead of converting them to generic mismatch
  errors.
- Native-backed objects exposed as `_io.TextIOWrapper` still need to honor the
  text-layer return contract. Keep byte-oriented plumbing below the public
  stdio methods; `sys.stdin.read()` and `readline()` return `str`, not `bytes`.
- Metadata added to native functions must survive descriptor binding when
  CPython exposes it on bound built-in methods. Forward `__text_signature__`
  through the bound-method attribute path instead of only storing it on the raw
  native callable.
- `sys.std*.buffer` is a separate binary stream object, not an alias for the
  text wrapper. Keep the shared underlying standard stream ownership unchanged,
  but expose bytes at the buffered layer and strings at the TextIOWrapper layer.
- Shared native method callbacks can still need receiver-specific diagnostics.
  When one implementation backs multiple CPython-visible classes, derive the
  displayed class name from the bound receiver instead of hardcoding the first
  wrapper type.
- Module-defined exception classes still need normal `BaseException` display
  behavior. Do not key exception `str()` rendering only on class names ending in
  `Error`/`Exception`; CPython classes such as `io.UnsupportedOperation` inherit
  the message behavior without matching those suffixes.
- Import compatibility includes visible cache side effects. When the runtime
  bypasses CPython's path-hook dispatch with a native fast path, mirror
  `sys.path_importer_cache` entries for the selected path item so stdlib code
  observing import state sees the expected importer object.
- Before adding a native helper body, search with the same recursive file walk
  used for edits, not a shell glob that may silently miss files. Existing
  unregistered or under-fixtured helpers should be covered or registered
  without duplicating their implementation.
- When strengthening a native facade toward CPython behavior for valid inputs,
  preserve existing compatibility semantics for deliberately accepted legacy
  inputs unless the batch explicitly retires them with fixture coverage.
- Archive-backed zipimporter execution should reuse the normal parse/lower/run
  module path and assert `sys.modules`, `__loader__`, `__spec__`, and `__file__`
  metadata; keep `load_module()` registration semantics separate from
  `exec_module()`'s supplied-module execution, and keep non-zip legacy facade
  behavior separate when fixtures already depend on it.
- Visible startup containers can intentionally differ from internal runtime
  search roots. For `-c`, publish CPython's empty-string `sys.path[0]` sentinel
  while preserving the resolved cwd import root for actual module lookup.
- Import loader compatibility includes protocol no-ops such as
  `create_module()` and `invalidate_caches()`. Add them to the real loader
  object when `find_spec()` exposes that object through `ModuleSpec.loader`.
