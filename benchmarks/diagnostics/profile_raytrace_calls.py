"""Count Python call events in the unchanged pyperformance raytrace body.

This is a call-frequency diagnostic, not a timing harness. It executes the
benchmark's ``bench_raytrace`` function once at the official 100x100 size.
"""

import argparse
import json
import runpy
import sys
from collections import Counter


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("benchmark_script")
parser.add_argument("--width", type=int, default=100)
parser.add_argument("--height", type=int, default=100)
args = parser.parse_args()

namespace = runpy.run_path(args.benchmark_script, run_name="raytrace_profile_target")
benchmark = namespace["bench_raytrace"]
calls = Counter()


def profile(frame, event, _arg):
    if event == "call":
        code = frame.f_code
        filename = code.co_filename.replace("\\", "/").rsplit("/", 1)[-1]
        calls[(filename, code.co_name)] += 1


sys.setprofile(profile)
try:
    elapsed = benchmark(1, args.width, args.height, None)
finally:
    sys.setprofile(None)

print("diagnostic_elapsed_seconds", elapsed)
print("call_events_by_source_function")
print(json.dumps(calls.most_common(), separators=(",", ":")))
print("total_profiled_python_calls", sum(calls.values()))
