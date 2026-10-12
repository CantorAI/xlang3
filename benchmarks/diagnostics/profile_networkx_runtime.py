"""Run unchanged official NetworkX bodies for diagnosis, outside pyperf.

Setup and calls are recorded separately. Profiled times are perturbed and
must not be substituted for official pyperformance samples.
"""

import argparse
import json
import runpy
import sys
import time
from collections import Counter


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("benchmark_script")
parser.add_argument("--benchmark", choices=("shortest_path", "connected_components", "k_core"),
                    default="shortest_path")
parser.add_argument("--repeats", type=int, default=1)
parser.add_argument("--call-profile", action="store_true")
parser.add_argument("--ready-file", help="create a fresh marker after setup for native sampling")
args = parser.parse_args()
if args.repeats < 1:
    parser.error("repeats must be positive")

start = time.perf_counter()
namespace = runpy.run_path(args.benchmark_script, run_name="networkx_diagnostic_target")
setup_seconds = time.perf_counter() - start
benchmark = namespace["BENCHMARKS"][args.benchmark]
calls = Counter()


def profile(frame, event, arg):
    if event == "call":
        code = frame.f_code
        filename = code.co_filename.replace("\\", "/").rsplit("/", 1)[-1]
        calls[(filename, code.co_name)] += 1


print("official body loaded; setup_seconds", setup_seconds, flush=True)
if args.ready_file:
    # A sampler waiting for this marker excludes graph loading/imports. The
    # ordinary call-profile flag stays off for native sampling, preserving VM
    # optimization eligibility; all diagnostic times remain perturbed evidence.
    with open(args.ready_file, "x") as marker:
        marker.write("official benchmark body ready\n")
samples = []
if args.call_profile:
    sys.setprofile(profile)
try:
    for repeat in range(args.repeats):
        start = time.perf_counter()
        benchmark()
        elapsed = time.perf_counter() - start
        samples.append(elapsed)
        print("diagnostic_call_seconds", repeat, elapsed, flush=True)
finally:
    if args.call_profile:
        sys.setprofile(None)

print(json.dumps({
    "diagnostic_only": True,
    "profiled": args.call_profile,
    "benchmark": args.benchmark,
    "setup_seconds": setup_seconds,
    "call_seconds": samples,
    "call_events": [{"source": source, "function": function, "count": count}
                    for (source, function), count in calls.most_common()],
}, indent=2))
