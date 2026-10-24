# The async opcode probe did not reach timed workers (2026-10-06)

The previous async-tree opcode timing log measured setup processes rather than
the timed benchmark body. The runner's `--inherit-environ` list contained only
`PYTHONPATH`, `PYTHONPYCACHEPREFIX`, and `XLANG3_PYTHON_LIB`. Pyperf's
`Manager.spawn_worker` constructs each worker environment with
`pyperf._utils.create_environ`; its default allowlist does not include
`XLANG3_VM_OPCODE_TIMING`. Setting that flag in the outer shell therefore did
not enable timing scopes in the timed worker.

The [original log](data/async-tree-none-opcode-timing-20261006.log) contains one
timer block before its `[1/1] async_tree...` benchmark marker. The absent
Task-step timer rows do not rule out a native Task hotspot. Likewise, the
reported 2.8 ms `LoadLocalAttr` cost does not measure the async-tree body. The
[profile report](async-tree-none-native-profile-20261006.md) has been corrected.
The independent callback counts and ordinary pyperf result remain evidence;
the setup-process timer rows provide no benchmark-body attribution.

The shimmed runner now supports repeatable `--inherit-worker-env NAME`
arguments. A separately built instrumented binary needs both the environment
flag and explicit worker forwarding:

```powershell
$env:XLANG3_VM_OPCODE_TIMING = '1'
# Add this option to the diagnostic runner invocation:
# --inherit-worker-env XLANG3_VM_OPCODE_TIMING
```

Normal benchmark runs retain the existing worker environment. No diagnostic
flags are added implicitly. Timers perturb execution, so instrumented runs
remain attribution experiments rather than candidate/control speed results.

## Validation

Under CPython 3.14.7, a direct check of the installed pyperf environment
builder confirmed that an outer-shell flag is removed without explicit
inheritance and preserved with `['XLANG3_VM_OPCODE_TIMING']`. The runner's
`--help` accepts the new option, and invalid variable names are rejected during
argument parsing. No benchmark was launched for these checks. A new
worker-instrumented async-tree run remains pending; the full 97-case ordinary
run continues with its original environment and fixed executable.
