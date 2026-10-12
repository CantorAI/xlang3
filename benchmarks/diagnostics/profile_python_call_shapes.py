"""Compare Python call shapes against loop-only and builtin-call controls."""

import argparse
import json
import statistics
import time


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--calls", type=int, default=50_000)
parser.add_argument("--repeats", type=int, default=9)
args = parser.parse_args()


def identity(value):
    return value


def no_args():
    return 1


def add_one(value):
    return value + 1


def identity_three(first, second, third):
    return first


def make_offsetter(offset):
    def add_offset(value):
        return value + offset

    return add_offset


closure_adder = make_offsetter(1)


class CallTarget:
    def method(self, value):
        return value


target = CallTarget()
local_identity = identity


def loop_only(count):
    value = 0
    for _ in range(count):
        value += 1
    return value


def global_one_arg(count):
    value = 0
    for _ in range(count):
        value = identity(value)
    return value


def local_one_arg(count):
    value = 0
    function = local_identity
    for _ in range(count):
        value = function(value)
    return value


def global_zero_arg(count):
    value = 0
    for _ in range(count):
        value = no_args()
    return value


def global_body_work(count):
    value = 0
    for _ in range(count):
        value = add_one(value)
    return value


def global_three_arg(count):
    value = 0
    for _ in range(count):
        value = identity_three(value, 1, 2)
    return value


def global_closure_call(count):
    value = 0
    for _ in range(count):
        value = closure_adder(value)
    return value


def bound_method(count):
    value = 0
    for _ in range(count):
        value = target.method(value)
    return value


def native_builtin(count):
    value = 0
    for _ in range(count):
        value = id(value)
    return value


cases = {
    "loop_only": loop_only,
    "global_one_arg": global_one_arg,
    "local_one_arg": local_one_arg,
    "global_zero_arg": global_zero_arg,
    "global_body_work": global_body_work,
    # The eval call-site guard is keyed by exactly three positional arguments.
    # Keep this light-body shape separate so paired runs expose its call cost.
    "global_three_arg": global_three_arg,
    # Captured free variables keep this tiny function out of the existing
    # trivial and argument-binary inliners, exposing ordinary CALL frame cost.
    "global_closure_call": global_closure_call,
    "bound_method": bound_method,
    "native_builtin": native_builtin,
}
results = {}
for name, operation in cases.items():
    assert operation(16) is not None
    samples = []
    for _ in range(args.repeats):
        start = time.perf_counter()
        result = operation(args.calls)
        samples.append(time.perf_counter() - start)
        assert result is not None
    results[name] = {
        "samples_seconds": samples,
        "median_seconds": statistics.median(samples),
        "calls": args.calls,
    }

print(json.dumps({
    "diagnostic_only": True,
    "python_version": __import__("sys").version,
    "calls": args.calls,
    "repeats": args.repeats,
    "results": results,
}, indent=2))
