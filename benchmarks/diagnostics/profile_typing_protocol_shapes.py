"""Measure runtime-checkable protocol checks by protocol/instance shape.

Diagnostic only: imports the unchanged pyperformance workload definitions,
then times small groups independently. Use matching runtimes and loop counts;
these values are not substitutes for the official pyperf result.
"""
import runpy
import sys
import time
import types

if len(sys.argv) != 2:
    raise SystemExit("usage: profile_typing_protocol_shapes.py BENCHMARK.py")

class _ImportOnlyRunner:
    def __init__(self):
        self.metadata = {}
    def bench_time_func(self, *args, **kwargs):
        pass

stub = types.ModuleType("pyperf")
stub.Runner = _ImportOnlyRunner
sys.modules["pyperf"] = stub
ns = runpy.run_path(sys.argv[1], run_name="typing_benchmark")

protocols = [ns[name] for name in (
    "HasX", "HasManyAttributes", "SupportsInt", "SupportsManyMethods",
    "SupportsIntAndX")]
groups = {
    "empty": [ns["Empty"]()],
    "property": [ns["PropertyX"](), ns["PropertyXWithInt"]()],
    "class_attrs": [ns["ClassVarX"](), ns["ClassVarXWithInt"]()],
    "instance_attrs": [ns["InstanceVarX"](), ns["ManyInstanceVars"](), ns["InstanceVarXWithInt"]()],
    "methods": [ns["HasIntMethod"](), ns["HasManyMethods"]()],
    "nominal": [ns["NominalX"](), ns["NominalSupportsInt"](), ns["NominalXWithInt"]()],
}
loops = 2000
print("protocol,shape,instances,loops,median_seconds")
for protocol in protocols:
    for shape, instances in groups.items():
        def check():
            for _ in range(loops):
                for instance in instances:
                    isinstance(instance, protocol)
        check()  # warm caches before timing
        samples = []
        for _ in range(5):
            start = time.perf_counter()
            check()
            samples.append(time.perf_counter() - start)
        samples.sort()
        print(f"{protocol.__name__},{shape},{len(instances)},{loops},{samples[2]:.9f}")
