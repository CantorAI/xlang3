"""Break down pyperformance's protocol checks without a profiler.

Loads the official benchmark definitions and times representative
nominal/structural checks independently. This is diagnostic only; the official
pyperf result remains the comparison used to validate changes.
"""
import json
import runpy
import sys
import time
from pathlib import Path


def main():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    benchmark = Path(sys.argv[2])
    namespace = runpy.run_path(str(benchmark), run_name="typing_protocol_profile")
    names = (
        ("nominal_single_data", "NominalX", "HasX"),
        ("structural_class_data", "ClassVarX", "HasX"),
        ("structural_instance_data", "InstanceVarX", "HasX"),
        ("structural_property", "PropertyX", "HasX"),
        ("structural_missing", "Empty", "HasX"),
        ("structural_single_method", "HasIntMethod", "SupportsInt"),
        ("structural_many_methods", "HasManyMethods", "SupportsManyMethods"),
        ("structural_many_data", "ManyInstanceVars", "HasManyAttributes"),
        ("structural_mixed", "PropertyXWithInt", "SupportsIntAndX"),
    )
    rows = []
    for name, instance_name, protocol_name in names:
        instance = namespace[instance_name]()
        protocol = namespace[protocol_name]
        expected = isinstance(instance, protocol)
        for _ in range(100):
            assert isinstance(instance, protocol) is expected
        start = time.perf_counter()
        for _ in range(loops):
            result = isinstance(instance, protocol)
        elapsed = time.perf_counter() - start
        assert result is expected
        rows.append({"case": name, "loops": loops, "seconds": elapsed,
                     "result": expected})
    print(json.dumps({"purpose": "diagnostic only; one warmed representative pair per path",
                      "rows": rows}))


if __name__ == "__main__":
    main()
