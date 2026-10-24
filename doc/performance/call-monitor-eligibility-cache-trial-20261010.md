# Cached trivial-call monitoring eligibility — 2026-10-10

## Change and result

Cached trivial Python-function calls now cache the `sys.monitoring` function
event eligibility result at the call site, using the existing monitoring
configuration generation. Trace, profile, and debug hooks remain checked on
every call; function code-version checks and the ordinary fallback remain in
place. The comment beside the guard explains why the event query can be reused
and which live hooks are still checked.

Eight order-alternated process runs of the call-shape diagnostic used 200,000
calls per sample. Global one-argument, local one-argument, and zero-argument
trivial-call medians were respectively **5%, 3%, and 4% faster** than the
preserved parent. Bound-method and non-trivial-body calls did not improve.
The full 11-case local Release regression gate passed against that immediate
parent; its `function_calls` workload reported 0.964x candidate/parent time.

The official pyperformance measurements showed no material effect on the
tested call-heavy library cases: `typing_runtime_protocols` was 2.79 ms on
both builds, and `pickle_pure_python` was 3.51 ms on the parent and 3.52 ms on
the candidate. Fast-mode variation limits precision. Keep this as a narrow
call-shape improvement only; it does not explain or materially change the
full-suite gap.

## Evidence

- [Order-alternated call-shape samples](data/call-monitor-balanced-r1-20261010/)
- [Parent local gate](data/call-monitor-cache-parent-gate-r1-20261010.json)
- [Official `typing_runtime_protocols` parent](data/call-monitor-cache-typing-parent-r1-20261010.json)
  and [candidate](data/call-monitor-cache-typing-candidate-r1-20261010.json)
- [Official `pickle_pure_python` parent](data/call-monitor-cache-pickle-parent-r1-20261010.json),
  [candidate](data/call-monitor-cache-pickle-candidate-r1-20261010.json), and
  [CPython 3.14.7](data/call-monitor-cache-pickle-cpython3147-r1-20261010.json)
- Parent executable/runtime SHA-256: `FE99DAAF1100286C569FA49A7104D4D4E694F19FADF0B5AF8D677C3439086374` /
  `DB6E062B0692347E0B4C92E5A7389CEBB270684497D0BD720EB7FF9A7FDA895E`
- Candidate executable/runtime SHA-256: `E39AF2EE6E4A531D7758525A93E63A40DD678DB9BB6BE1C3AB4FF1B0ADCC7584` /
  `F06476F7BA02C36879FA52DB6248375B10F14D0A188E750F05F57EC321C76707`
