# Native POSIX signals and graceful host shutdown

Linux x86-64 qualification, 2026-11-28. This repair delivers kernel signals to
registered Python handlers and wakes an asyncio selector. Previously the native
`signal.signal` registry did not install POSIX handlers, so SIGTERM terminated
an API before its ASGI cleanup could run.

The OS handler uses lock-free pending flags and a nonblocking wakeup descriptor,
preserves errno, and executes no Python, allocation or mutex operations. The
existing interpreter event poll dispatches callbacks on the main runtime thread.
Embedding ownership preserves/restores native host dispositions. `siginterrupt`
uses POSIX restart flags. Interrupted `select` restores descriptor sets and retries
against one monotonic deadline; raising callbacks escape instead of retrying.
No VM instruction path or public SDK change is introduced.

## Qualification

The accepted Release baseline is fixed at source
`75d6b8db81399f8476ecd4bd2dc5ab20fd8932a1`, library SHA-256
`cfc86af2f4c5a8f2f51aa37fc3dacd1692c2b780d9c9e17cddffaceec5b5f081`.
The older installed manifest naming e8a1b8d2 omitted later repairs; those source
repairs are retained. Sixteen SDK files and90 public C exports are unchanged.

Actual kernel/asyncio/wakeup/restoration fixtures, three embedded handler-lifetime
scenarios, retained embedded-package/native-host contracts and ordinary generic
cycle collection pass. The native differential runner executes all456 golden
cases against separately loaded baseline/candidate libraries:440 pass,16 fail
identically on both, zero new regressions. The complete golden suite does not
pass. Two focused existing failures are recorded separately in qualification.json.

The complete unchanged performance gate exits0: all11 default cases,21 paired
samples,5 warmups,10% per-case tolerance, same fixed baseline. Measurements run
after builds on an otherwise idle host, with equal CPU4 affinity for children.
The full [JSON report](posix-signals-20261128/performance.json) is preserved;
the qualification report and source/link provenance are in the same directory.

Earlier GCC-only compilation passed correctness but failed subparsers1.214x.
Two complete Clang-only gates were inconclusive on scalar arithmetic; neither
was accepted. A bounded local-GVN compiler experiment was intentionally stopped.
No threshold, workload, baseline or correctness feature was changed to pass.

## Qualified build profile

The passing artifact combines a source-identical GCC13.3 VM object with normal
Clang19.1.1 non-VM Release objects, linked with LLD19 and libstdc++13. All objects
use C++17/O3/NDEBUG/PIC. Only the GCC VM translation unit uses `-fno-tree-pta` to
bound compiler analysis. VM source and headers are unchanged. No unproven cached
object was used. Cross-compiler source/header byte equality was verified before
linking; original link input hashes were verified again afterward.

The original Clang runtime link command is retained verbatim except for the
source-identical GCC VM object, private output and `$ORIGIN` runtime search path.
Embedded test executables are relinked against that exact shared candidate.
The CLI is the hash-pinned Clang CLI. Exact observed commands, source hashes,
all link-input hashes and build profile are preserved under
`posix-signals-20261128/`. Paths in those records identify the original private
builds; they are provenance, not a requirement to reuse stale artifacts.

Candidate CLI SHA-256:
`97ec927d7c40f4f8e01d770a1b16b3babc6b15a140accead48f56be0f2be429c`.
Candidate library SHA-256:
`677cf3af25e5eecb75a3ab6d27a0adc50d70af2d3cc4b8707e81b3c2b26888e8`.

## Actual assembled host

Azure kernel/asyncio smoke passes with those exact binaries. An isolated actual
Core/database/uvicorn host processes960 HTTP checks, including300 rejected bodies,
real periodic GC/retention and zero-work settlement. On shutdown, the API joins
GC, closes its bridge and sends ASGI shutdown.complete. Uvicorn then restores and
re-raises SIGTERM; exit-15 is accepted only with those observed cleanup events.

Core initially crashed during final teardown with both the new and installed
runtimes. Debugger stacks identified script futures releasing Galaxy objects
after Galaxy teardown. Separate Cantor commit
`436a89cb43c36c7fe071eba14ac02bc41004e302` closes script admission, notifies and
joins/releases scripts before dependent services stop. With that paired repair,
the actual Core exits0. Temporary database/role/credentials are removed; both
process groups are empty; shared services and release pointers stay unchanged.
Original failed receipts remain retained. See
[host evidence](posix-signals-20261128/assembled-host.json).

This qualification is Linux x86-64 only. It does not establish Windows/macOS
release acceptance, complete Python compatibility, finalizable-cycle support,
production memory/load behavior, real model worker compatibility or public
search acceptance. Nothing was activated in shared production by these tests.
