# Empty asyncio current-task map fast-path trial (2026-10-06)

## Result

Rejected. In a rigorous pyperformance 1.14.0 comparison, both the fixed
Release control and the candidate measured `async_tree_eager` at **1.31 s ±
0.05 s**. `pyperf compare_to` hid the case as not statistically significant.
The candidate change was removed and the normal `build-repro/Release` pair was
restored to the fixed control hashes below.

## Trial

`asyncio_native::current_for_loop()` checks the cached thread-local Task first.
When that slot is empty, it falls back to the Python-visible
`asyncio.tasks._current_tasks` mapping. The candidate additionally returned
`None` without calling `dict.get()` when the cached mapping was completely
empty. This kept the fallback for any nonempty mapping, including mappings
that may hold a Task running on another loop or thread.

The hypothesis was that this generic lookup was a material cost on each
scheduled Task entry. The official benchmark did not show a repeatable gain,
so the shortcut is not retained. The profile still points to coroutine resume
and VM work as the larger target.

## Measurement and validation

Both runs used CPython 3.14.7 as the pyperformance 1.14.0 harness, the same
dependency site, `--mode rigorous`, and the repository's
`run_pyperformance_xlang3_shimmed.py` runner.

| Binary | `async_tree_eager` |
| --- | ---: |
| Fixed Release control | 1.31 s ± 0.05 s |
| Candidate | 1.31 s ± 0.05 s |

The complete fixture runner and `xlang3_interpreter_tests.exe` passed with the
candidate. The candidate and control runtime hashes were
`3150764B55CA1F41D7297A41ADE1FA75C9E0CD484427C4FA30416499C905A9CD` and
`B29EE2944F3916F7D901EB2A178F58CD9EA1B36DC1D1DFB2033338012D61CC2D`,
respectively. The executable hashes were
`93CC8A526E4DEA2419E3361EF97B260AA14A82506B4F83893F3A2C3BDD09C64C` and
`E8EFEE922E95093437E0FEF3F6754ED2730A3CEC2BBA9A4C067F2C660C7B202C`.

Raw data and logs:

- [Control pyperf JSON](data/asyncio-empty-current-map-control-r1-20261006.json)
  and [log](data/asyncio-empty-current-map-control-r1-20261006.log)
- [Candidate pyperf JSON](data/asyncio-empty-current-map-candidate-r1-20261006.json)
  and [log](data/asyncio-empty-current-map-candidate-r1-20261006.log)
- [pyperf comparison](data/asyncio-empty-current-map-compare-20261006.txt)
