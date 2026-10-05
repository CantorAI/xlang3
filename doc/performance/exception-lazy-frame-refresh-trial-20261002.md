# Lazy traceback frame refresh trial (2026-10-02)

## Question

Does avoiding a live-frame snapshot refresh on every exception improve the
`subparsers` benchmark, which constructs many caught `KeyError` instances?

## Candidate

The candidate temporarily removed `Runtime::refresh_live_frame_snapshots()`
from VM exception dispatch and refreshed on `frame.clear()` instead. Existing
`f_locals`, `f_lineno`, `sys._current_frames()`, and frame-retirement paths
already refresh snapshots before exposing frame state.

## Correctness

The new `exception_traceback_lazy_frame_refresh` fixture passed, along with
`code_traceback_model`, `context_manager_exception_traceback`,
`debug_frame_source_edges`, `except_target_cleanup`, `exception_instance_dict`,
and `traceback_module`. For `traceback_module`, the expected-output runner must
receive Windows-style backslash source paths because the fixture prints its
source path in traceback output.

## Benchmark

The order-balanced paired comparison used 21 measured pairs and three warmups
for `benchmarks/cases/subparsers.py`, with the source-matched preserved Release
runtime as the control. The median candidate/control ratio was 0.9957 (about
0.4% faster), with a 95% paired-bootstrap interval of 0.9917–1.0130. This does
not establish a performance gain. The candidate change was reverted; the raw
samples are in [subparsers-exception-refresh-ab-20261002.json](data/subparsers-exception-refresh-ab-20261002.json).

## Decision

Do not retain lazy refresh on the basis of this result. Exception dispatch
keeps its eager frame refresh until a different optimization shows a repeatable
gain and preserves traceback/frame semantics.
