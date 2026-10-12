# Dask import failure: dataclass weakref field

During the full XLang3 versus CPython 3.14.7 pyperformance run
`pyperformance-xlang3-current-candidate-full-fast-20261010-r1`, case 33
(`dask`) died before timing. `distributed/worker_state_machine.py` decorates
`TaskState` with `@dataclass(repr=False, eq=False, slots=True)` and declares
`__weakref__: Any = field(init=False)` after the defaulted `done` field. The
CPython 3.14 `dataclasses._get_field()` implementation reads class attributes
with `getattr(cls, name, MISSING)` and recognizes the value as a `Field` only
when that explicit class attribute is returned. In XLang's class attribute
lookup, the implicit `__weakref__` descriptor was returned before the explicit
class namespace value. The dataclass code therefore treated `__weakref__` as
an init parameter and raised:

```text
TypeError: non-default argument '__weakref__' follows default argument 'done'
```

The runtime lookup now gives an explicit class namespace value precedence
over the implicit descriptor. A focused fixture,
`dataclass_slots_weakref_field.py`, covers the Dask pattern, including the
generated constructor fields and weak-reference support.

Validation is pending: the current full 97-case baseline run is still using
the pre-change fixed Release executable. Do not use its results as evidence
for this source change. After that run exits, build with the recorded VS
command and run the focused fixture before rerunning the full comparison.
