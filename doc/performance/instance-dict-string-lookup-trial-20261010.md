# Instance dictionary string lookup trial — 2026-10-10

## Hypothesis and result

`object_get_attr` constructed a temporary `Value::string(name)` before probing
an instance dictionary. The mapping layer already has `mapping_get_string_item`,
which accepts a string view and preserves generic equality fallback for mixed
key dictionaries. Replacing three instance-attribute reads with that helper
looked like it could avoid allocation in a common path.

The fixed Release build and full Python 3.14 fixture runner completed with the
candidate, including the custom mixed-key `__dict__` case. However, one
20,000-operation diagnostic sample did not show a performance gain:

| Primitive | Parent | Candidate | Candidate / parent |
|---|---:|---:|---:|
| `dict.get` | 22.048 ms | 21.944 ms | 0.995× |
| `inspect._check_instance` | 40.161 ms | 39.754 ms | 0.990× |
| `inspect.getattr_static` | 427.482 ms | 434.839 ms | 1.017× |
| `inspect._shadowed_dict` | 88.307 ms | 89.576 ms | 1.014× |

These unpaired changes are within noise and the full static lookup moved in the
wrong direction. The source change and redundant fixture were removed without
running the official benchmark. Do not retry this substitution without a new
reason; the faster helper does not remove the dominant cost in these paths.

## Evidence

- [Parent XLang3 diagnostic](data/instance-dict-string-lookup-parent-xlang3-20261010.json)
- [Candidate XLang3 diagnostic](data/instance-dict-string-lookup-candidate-xlang3-20261010.json)
- [CPython 3.14.7 parent diagnostic](data/instance-dict-string-lookup-parent-cpython3147-20261010.json)
- [CPython 3.14.7 candidate diagnostic](data/instance-dict-string-lookup-candidate-cpython3147-20261010.json)
- The full fixture runner exited successfully with the candidate; the PowerShell
  wrapper stopped earlier on an unrelated `ResourceWarning` in
  `io_module_streams`.
- [Candidate build log](data/build-instance-dict-string-lookup-Release-20261010.log)
