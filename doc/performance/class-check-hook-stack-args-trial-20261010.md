# Class-check hook stack arguments — 2026-10-10

`call_class_check_hook` builds a vector of at most two arguments before it
executes a Python-defined `__instancecheck__`/`__subclasscheck__`. I replaced
that vector with a fixed stack array, preserving the Python hook call and all
existing dispatch rules. The complete fixture suite passed, but the focused
diagnostic did not improve: protocol positive, negative and nominal checks
were 1.5–3.4% slower in the candidate sample, while direct hook and `_abc`
costs were effectively flat. A fast official `typing_runtime_protocols` run
was 2.88 ms ± 0.20 ms; its saved parent was 2.78 ms ± 0.03 ms. The fast run
was unstable and the focused rows show no consistent signal, so the change was
removed. No benchmark win is claimed.

This path remains expensive because it looks up/binds the hook and runs a
separate `Interpreter::run_function_value` for every Python hook invocation.
Reducing that repeated VM entry is a larger target than removing this small
argument vector.

## Evidence

- [Candidate official pyperformance result](data/typecheck-hook-stack-args-candidate-typing-20261010.json)
- [Candidate run log](data/typecheck-hook-stack-args-candidate-typing-20261010.log)
- [Candidate dispatch diagnostic](data/typecheck-hook-stack-args-dispatch-xlang3-20261010.csv)
- [CPython 3.14.7 dispatch diagnostic](data/typecheck-hook-stack-args-dispatch-cpython3147-20261010.csv)
- [Prior exact-MRO dispatch diagnostic](data/typing-protocol-dispatch-layers-exact-mro-candidate-20261010.csv)
- [Fixture exit status](data/typecheck-hook-stack-args-fixtures-exit-20261010.txt)
