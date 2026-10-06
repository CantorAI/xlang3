# Pickle BytesIO conditional-forwarder trial (2026-10-06)

## Result

Rejected. A guarded VM shortcut recognized the conditional two-argument
Python forwarding shape used by `_Framer.write` and directly called native
`_io.BytesIO.write` when the frame was an exact `BytesIO` and the payload was
exact `bytes`. Other streams, overrides, observable execution, and errors used
the ordinary Python call path. The shortcut did not produce a significant
official Pickler benchmark gain, so it was removed.

| Official pyperformance 1.14.0 case | XLang3 control | Candidate | Candidate / control |
| --- | ---: | ---: | ---: |
| `pickle_pure_python` | 5.24 ms ± 0.33 ms | 5.30 ms ± 0.30 ms | 1.01× (not significant) |

Both runs used the same Python 3.14.7 dependency site, compatibility hooks,
Release control executable, and rigorous pyperformance mode. `pyperf
compare_to` hid the result as statistically insignificant. The means do not
support retaining a shape-specific shortcut with additional dispatch guards.

## Semantic checks

Before removing the candidate, a direct XLang3 probe passed on both the
candidate and preserved control. It checked the exact BytesIO branch, a
BytesIO subclass override, the no-frame `file_write` branch, and a closed
BytesIO error whose traceback retained the Python `write` frame. The complete
fixture suite also passed after rebuilding the reverted source. The probe was
temporary because its sole purpose was validating this rejected shortcut.

## Evidence

- [Control pyperf JSON](data/pickle-bytesio-writer-control-rigorous-20261006.json)
- [Candidate pyperf JSON](data/pickle-bytesio-writer-candidate-rigorous-20261006.json)
- Preserved control executable: `scratch/performance-trials/conditional-bytesio-writer/control/python.exe`
- Candidate executable SHA-256 during measurement: `411E7F122535EBDFEE8107734D0E0F0185CE34B22609E605492CCE5547BF708B`

This experiment did not modify `pickle.py` or add a native replacement for a
pure-Python standard-library module. The Pickler gap remains open; future work
should reduce costs shared by many Python calls or show a clearly material
gain before adding a narrow call shortcut.
