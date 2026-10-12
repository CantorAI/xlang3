# Original raytrace method IR observation

The unchanged pyperformance raytrace source was executed once at its official
100 by 100 dimensions using the restored accepted Release executable. The
CLI dumped the entry module's compiled IR before execution. The worker completed
with one value, one loop and zero warmups; all 178 Release files and selected
source/dependency inputs remained unchanged. The elapsed value is unscored.

`Vector.dot(self, other)` has 20 registers and 15 IR instructions. Its reachable
body contains a `CallLocalMethod` for the argument validation method, `Pop`,
six `LoadLocalAttr` instructions, three multiplications, two additions and
`Return`. The validation method's IR is a side-effect-free `ReturnLocal(0)`;
its following `ReturnConst` is unreachable.

The existing small-self-method execution path requires no explicit arguments,
one parameter and at most 16 registers/instructions. This actual two-parameter,
20-register method cannot enter that path. Existing primitive argument binary
shortcuts do not execute its six instance-field reads. This identifies an
uncovered IR family; it does not establish CPU share or a speed improvement.

A candidate generic plan should recognize method IR rather than Python names
or this library's algorithm. It must guard argument binding, custom attribute
access, descriptors, instance method shadows, resolved method/code versions,
primitive operand types, and inactive tracing/profiling/monitoring. It must
fall back to the original frame before any observable side effect, preserving
overrides, exceptions and traceback behavior. Pure-Python library code remains
Python.

The dump is compile IR, not a dump of adaptive call caches after warm-up. This
observation therefore does not prove the exact cache kind or frame count at
every call. Before accepting an optimization, run semantic/eligibility fixtures,
the full correctness checks, the unchanged fixed regression gate, and a paired
comparison of the unchanged original raytrace benchmark. The historical full
comparison reports 0.104925× CPython/XLang3 speed (9.53064× elapsed), not a current
paired optimization result.

Evidence: [original dot IR](data/raytrace-original-worker-ir-20261009-Vector.dot.ir.txt),
[terminal receipt](data/raytrace-original-worker-ir-20261009-receipt.json), and
[publication map](data/raytrace-original-worker-ir-publication-20261010.json).
