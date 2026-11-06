# Embedded runtime import roots, November 6, 2026

A fresh Cantor task worker may invoke a deserialized Python function without
going through the command-line launcher or `EvalFile`. The preserved Release
runtime had no `sys.path` in that case, so even a task inspecting its configured
import roots failed before native Garnet execution.

Runtime initialization now publishes the existing default import roots before
any Python task runs. Host `AddImportRoot` appends a genuinely new root to the
existing list under the runtime execution guard. It preserves application
entries, aliases and replacement lists, and repeated root registration adds no
duplicate. This changes startup and explicit host registration only; it adds no
per-instruction or per-frame monitoring. No CPython bridge is involved.

The new C++ embedding regression fails against the preserved pre-change runtime
with `fresh embedded runtime has no populated sys.path`. It passes against the
candidate, including an actual import from a temporary host-added root. Both
native embedding and host-guard CTest executables passed. Six focused CLI
fixtures passed and all 90 public C exports were preserved. The unchanged
packaged `._pth` profile was checked separately from standard command-line
semantics. On Linux, only the platform-specific printed `sys._vpath` value in
the documentation fixture was normalized after confirming the preserved runtime
produced exactly the same value; the fixture's own platform predicate passed.

The complete default Release regression gate passed on XPS after all builds
finished, with native CPU task admission held and restored afterward. There
were 21 order-balanced paired measurements, five warmups and the unchanged 10%
threshold against the fixed accepted baseline. This establishes no new
performance baseline and does not resolve historical performance differences.

| Case | Candidate / baseline | Result |
| --- | ---: | --- |
| local_slots | 0.989 | pass |
| scalar_arithmetic | 0.998 | pass |
| range_for | 1.003 | pass |
| function_calls | 1.009 | pass |
| class_construct | 0.984 | pass |
| list_append | 0.999 | pass |
| property_access | 1.001 | pass |
| deepcopy_memo | 0.989 | pass |
| json_dumps | 0.969 | pass |
| gc_traversal | 1.010 | pass |
| subparsers | 0.981 | pass |

The full measured samples and executable/runtime hashes are in
[embedded-import-root-path-20261106.json](embedded-import-root-path-20261106.json).
Candidate runtime SHA-256:
`cfc86af2f4c5a8f2f51aa37fc3dacd1692c2b780d9c9e17cddffaceec5b5f081`.

This qualification covers Linux x86-64 Release. Production replacement and a
fresh cross-node Cantor task without the test's explicit-root workaround are
separate rollout checks. Other platform builds still require qualification.
