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
