# Interpreter event-poll countdown trial (2026-10-03)

## Result

Rejected. Encoding the immediate event hint in the existing 64-instruction
poll countdown removed a thread-local hint branch from the VM's per-opcode
event check, while keeping signal and weakref handling on the same dispatcher.
It passed the weakref and signal fixtures, but it did not improve representative
workloads and made the function-call case measurably slower. The change was
removed from `interpreter_events.cpp`.

The same-source Release A/B used 21 order-balanced pairs and three warmups per
case. Output was identical on every run. Candidate time divided by control:

| Case | Control median | Candidate median | Candidate / control | 95% interval |
|---|---:|---:|---:|---:|
| `scalar_arithmetic` | 8.913 ms | 9.000 ms | 1.0133× | 0.9868–1.0301× |
| `function_calls` | 1.006 ms | 1.042 ms | 1.0321× | 1.0146–1.0647× |

The `function_calls` regression rules out retaining this version of the change.
Raw paired samples are in
[`scalar-arithmetic-ab.json`](../../scratch/performance-trials/interpreter-event-poll-countdown-20261003/scalar-arithmetic-ab.json)
and
[`function-calls-ab.json`](../../scratch/performance-trials/interpreter-event-poll-countdown-20261003/function-calls-ab.json).
The candidate/control executables and runtime DLLs remain under the same trial
directory for reproduction. The fixed Release path was unchanged.
