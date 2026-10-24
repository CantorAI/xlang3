# Early dynamic `LoadAttr` cache trial (2026-10-02)

## Result

Rejected. Moving a warmed ordinary-instance attribute cache hit ahead of the
descriptor and separate-dictionary checks did not improve either focused
workload:

| Workload | Control median | Candidate median | Candidate / control | 95% paired interval |
|---|---:|---:|---:|---:|
| Pure-Python Pickler-shaped dumps | 48.520 ms | 48.691 ms | 1.0053x | 0.9914–1.0144 |
| `subparsers` | 263.043 ms | 267.149 ms | 1.0156x | 1.0010–1.0211 |

Both comparisons used 21 order-balanced AB/BA pairs and three warmups. The
shortcut's added guards did not pay for themselves on these workloads, and
the `subparsers` slowdown was small but consistently above parity.

## Hypothesis and correctness coverage

The existing `InstanceAttr` cache hit came after descriptor and
instance-dictionary checks. The trial moved it earlier only for classes with
no descriptors, custom `__getattribute__`, or separate `__dict__`, while
retaining class-version and attribute-name guards. The optimization was
removed after measurement.

The fixture [`load_attr_cache_precedence.py`](../../tests/fixtures/core/load_attr_cache_precedence.py)
remains in the core fixture runner. It warms an instance-attribute site, then
adds a data descriptor and checks custom `__getattribute__` results. The
fixture passed along with the existing property, custom-attribute, and pickle
fixtures on the trial build.

Raw results: [Pickler-shaped A/B](data/load-attr-cache-pickle-ab-20261002.json)
and [`subparsers` A/B](data/load-attr-cache-subparsers-ab-20261002.json).
The focused Pickler case is [`pickle_pure_python_writer.py`](../../benchmarks/cases/pickle_pure_python_writer.py).
