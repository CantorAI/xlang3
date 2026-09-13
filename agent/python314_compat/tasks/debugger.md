# Debugger Compatibility Tasks

- [x] Python CLI compatibility for IDE launch
  Completed: XLang3 accepts the CPython 3.14 launch forms and startup controls used
  by IDE tooling, including `-c`, `-m`, script execution, help/version flags,
  `-E`, `-I`, `-P`, `-S`, `PYTHONPATH`, `sys.argv`, `sys.orig_argv`, and safe-path
  behavior. Coverage: `tests/cli/run_cli_compat.py`,
  `tests/fixtures/core/sys_command_path.py`, and
  `tests/fixtures/core/sys_startup_config.py`.

- [x] sys.settrace and threading.settrace
  Completed: trace hooks cover call, line, exception, return, generator yield/resume,
  coroutine suspension/resume, per-frame line/opcode flags, local trace replacement,
  `f_lineno` trace-only writes, and hooks inherited by new threads. Coverage:
  `tests/fixtures/core/trace_hooks.py`, `tests/fixtures/core/trace_events.py`,
  `tests/fixtures/core/trace_local_and_exception.py`, and
  `tests/fixtures/core/debug_trace_profile_edges.py`.

- [x] sys.setprofile and monitoring
  Completed: profile hooks cover Python call/return, generator and coroutine
  suspension/resume, native C call/return/exception events, callback reentrancy
  suppression, and hooks inherited by new threads. PEP 669 covers global/local
  events, instruction and branch events, raise/handled/reraise/unwind paths,
  generator throw, and `STOP_ITERATION`. Coverage:
  `tests/fixtures/core/debug_trace_profile_edges.py`,
  `tests/fixtures/core/sys_monitoring_all_events.py`, and
  `tests/fixtures/compat_sections/standard_modules.py`.

- [x] frame/code/source APIs for debugpy
  Completed: live frames expose locals, globals, back links, code metadata,
  instruction and line positions, trace flags, and trace-restricted line mutation;
  CPython `inspect`, `linecache`, and traceback source lookup operate on those
  objects. Coverage: `tests/fixtures/core/debug_frame_metadata.py`,
  `tests/fixtures/core/debug_frame_source_edges.py`, and
  `tests/fixtures/core/inspect_currentframe.py`.

- [x] debugpy adapter execution
  Completed: Visual Studio's installed, unmodified `debugpy.adapter` imports and
  runs naturally through XLang3. The automated DAP stdio test exchanges framed
  `initialize` and `disconnect` requests, validates responses/events, and requires
  a clean adapter exit. Native dependencies remain private runtime modules;
  CPython `ctypes`, `platform`, `collections`, `json`, and the rest of debugpy's
  pure-Python dependency graph remain source-backed. Coverage:
  `tests/fixtures/core/debugpy_bootstrap_compat.py` and
  `tests/cli/run_debugpy_adapter_smoke.py` (`xlang3_cli_debugpy_adapter_smoke`).

- [~] Visual Studio 2026 debugpy launch and inspection
  Functional integration completed; startup and inspection performance remains
  in progress. Visual Studio's unmodified debugpy adapter, launcher, debug
  server, and user program all run with `xlang3.exe` directly. The launch
  profile and regression use no `python.exe` alias, CPython host, or custom DAP
  bridge. The full DAP regression follows the normal protocol and verifies the
  XLang3 user breakpoint, local value `41`, continued output `42`, and
  termination. The repository profile is selectable as `XLang3 Python 3.14
  (debugpy)`, and the PEP 514 environment points directly to `xlang3.exe`.
  Runtime coverage includes
  stable main-thread identity, cross-thread `sys._current_frames()`, live
  builtins dictionaries on frames, thread-owned live-frame refresh, and
  monitoring callback lifetime across a released execution lock. The DAP
  regression now also sends an asynchronous Pause request, inspects its stack,
  resumes, and completes the program. It also requires debugpy to advertise
  hover evaluation and verifies that Visual Studio's DAP hover expression for
  `value` returns `41`. Pure CPython
  and debugpy libraries remain source-backed. The VM now retains safe owned
  cross-thread frame metadata, publishes frame-stack changes before suspension,
  gives every compiled function a stable monitoring code object, caches the
  effective event mask per code object, and refreshes live-frame locals only
  when `f_locals` is read. The runtime accepts every normal Python callable as
  a thread target, which makes debugpy's asynchronous Pause path work without
  debugger-specific behavior. General runtime work now includes cached import
  directory snapshots, ordered import-root deduplication, vectorcall adapters,
  cached nested-frame state, and fused local-load,
  local-store, comparison, identity, and short-circuit branch instructions.
  XLang's IR cache now serializes type-parameter constants, allowing modules
  such as `typing` to use their independent `.xlang3-314.pyc` cache instead of
  reparsing on every process start. Cached `typing` import time fell from about
  38-41 ms to 17 ms, and cached `debugpy.adapter` import time fell from about
  108 ms to 78-83 ms. Entry-only monitoring events also bypass the per-opcode
  observability path after the entry callback, and ordinary cached Python calls
  avoid repeated monitoring-map queries unless the call site has an inline
  specialization. Direct constant/local returns and paired local/global loads
  now avoid temporary-register transfers and interpreter dispatches while
  preserving normal source-line monitoring. Dynamically exposed member
  descriptors now use the runtime's stack-backed native-call path, and VM
  frames allocate memoryview bookkeeping only after producing a memoryview.
  Live Python frame objects are indexed by activation and retired directly;
  the runtime no longer rescans every tracked frame on every Python call and
  return. In the full launch regression this reduced live-frame refreshes from
  57,837 to 1,703 and scanned frame items from 545,072 to 21,135 while retaining
  live locals, traceback state, cross-thread inspection, and asynchronous
  pause behavior. A warm-cache audit covering 256
  source modules imported by the adapter, launcher, and server found an XLang
  IR cache for every ordinary Python module; only the natively supplied frozen
  import bootstrap aliases and `zipimport` intentionally lacked cache files.
  Class construction now writes prepared namespaces through the VM's mapping
  store operation instead of allocating and calling a temporary
  `dict.__setitem__` wrapper for every class member. This reduced native calls
  in the launch regression from about 57,600 to 48,850. Built-in method tables
  now retain one native descriptor per method, matching CPython type-descriptor
  lifetime; native-function allocations fell from 13,560 to 525. Class values
  receive closure cells only when the generated Python annotation function
  captures them, reducing cell allocations from 9,311 to 2,289 while preserving
  deferred class annotations. Class members that are never read by the class
  body or its generated annotation function no longer receive hidden local
  aliases; this removed about 6,180 local-value copies from the launch path.
  Ordinary methods now also follow CPython scope lookup and skip the class
  namespace when resolving free variables. Pure Python library and debugpy code
  remains source-backed throughout these changes.
  In the latest three interleaved warm runs, XLang reached the initialized event
  in 1.411-1.429 seconds and inspected the local value in 1.653-1.678 seconds
  (1.668 seconds median). The equivalent CPython 3.14 runs reached the local
  value in 0.858-0.902 seconds (0.902 seconds median). Host load varies between
  runs, and the remaining median gap is about 0.77 seconds, so the performance target
  remains open. Opt-in runtime profiling measured roughly 54-66 ms for all
  18,000 execution-lock release/reacquire transitions; expected socket, lock,
  and sleep waits dominated native callback time. The remaining non-waiting
  cost is general source-backed Python execution through `exec` and importlib,
  rather than line mapping, breakpoint lookup, cache misses, or monitoring
  callback dispatch alone. Coverage:
  `tests/cli/run_debugpy_launch_smoke.py`, `tests/ide/vs2026_launch.json`,
  `tests/fixtures/core/threading_runtime_edges.py`,
  `tests/fixtures/core/debug_trace_profile_edges.py`, and
  `tests/fixtures/core/sys_monitoring_all_events.py`.

## Continuation prompt

Continue this open Visual Studio 2026/debugpy goal in `D:\CantorAI\xlang3`
until XLang3's repeatable warm end-to-end debugging performance equals or
exceeds CPython 3.14 on the same host.

Keep these constraints throughout the work:

- `xlang3.exe` must be the only executable used by the actual adapter,
  launcher, debug server, and debuggee. Do not use a `python.exe` alias,
  CPython host, or hidden CPython process. CPython is only an external
  compatibility and performance reference.
- Preserve CPython's implementation boundary: the interpreter/runtime,
  built-in types, and CPython-native modules may be C++; pure Python standard
  library modules and debugpy must remain Python source. Do not reimplement a
  pure Python library in C++.
- Use general runtime improvements. Do not special-case debugpy, debugger file
  names, the benchmark, or Visual Studio behavior in the runtime.
- Build and run relevant regressions after each code change. Before declaring
  completion, run the full Release CTest suite and require all 50 tests to
  pass. Do not commit until the user asks.
- Preserve the DAP hover regression: debugpy must advertise
  `supportsEvaluateForHovers`, and `value` must evaluate to `41` across
  repeated requests at one stop, a later breakpoint, and an asynchronous
  pause.
- Do not extend adaptive-cache lifetime using unowned raw object pointers.
  The earlier naive persistent-cache experiment caused stale-pointer crashes;
  any future design needs explicit ownership and invalidation.
- At completion, remove temporary measurements and logs created by this work,
  while leaving `.vs` untouched.

Investigate the reported Visual Studio DataTip behavior first. The direct DAP
regression already proves repeated hover evaluation works. In a real IDE
session, compare Watch and the editor DataTip and inspect DebugAdapterHost logs
to determine whether Visual Studio sends each later `evaluate` request. If no
request is sent, document the IDE/editor cause and do not add a runtime
workaround. If a request reaches XLang3 and fails, capture that exact sequence
in the regression before fixing the general runtime behavior.

Continue profiling the remaining performance gap with interleaved warm runs;
the current local-inspection medians are about 1.668 seconds for XLang and
0.902 seconds for CPython, a gap of about 0.77 seconds. Prefer changes supported
by counters and repeatable timings. Promising general directions include
classifying native callbacks so CPython-equivalent nonblocking built-ins can
retain the VM execution lock, reducing measured value/materialization,
reference-count, and frame overhead, and adding code-lifetime adaptive caches
only after their ownership and invalidation are safe. Avoid broad speculative
fast adapters; the previous broad batch regressed performance.

The goal is complete only when all of these are true:

1. Process inspection confirms the complete live debug chain uses only
   `xlang3.exe`.
2. Visual Studio supports breakpoints, stepping, asynchronous pause/resume,
   stacks, locals, Watch/evaluate, and repeated DataTips across stops.
3. At least five interleaved warm runs show XLang's median time to inspect the
   first local is no slower than equivalent CPython 3.14.
4. The full Release suite passes 50/50, the measurements and counters above are
   updated, and temporary work files are removed.

Useful verification commands from the repository root:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build --config Release --target xlang3 -j 8
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build -C Release --output-on-failure
& 'C:\Python\Python314\python.exe' tests\cli\run_debugpy_launch_smoke.py .\build\Release\xlang3.exe
```

The last command uses CPython only as the external test driver; every process
inside the debug session must still be `xlang3.exe`.
