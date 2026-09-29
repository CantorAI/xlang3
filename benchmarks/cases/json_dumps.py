"""Fixed-baseline workload matching pyperformance's default json_dumps cases."""
import json


EMPTY = {}
SIMPLE = {
    "key1": 0,
    "key2": True,
    "key3": "value",
    "key4": "foo",
    "key5": "string",
}
NESTED = {
    "key1": 0,
    "key2": SIMPLE,
    "key3": "value",
    "key4": SIMPLE,
    "key5": SIMPLE,
    "key": "ąćż",
}
HUGE = [NESTED] * 1000


def main():
    # Keep the same shape and repetition counts as pyperformance 1.14. This
    # measures the public Python json.dumps path as well as the native _json
    # encoder, so regressions in wrapper dispatch cannot hide behind an
    # encoder-only microbenchmark.
    empty_result = None
    for _ in range(2000):
        empty_result = json.dumps(EMPTY)
    simple_result = None
    for _ in range(1000):
        simple_result = json.dumps(SIMPLE)
    nested_result = None
    for _ in range(1000):
        nested_result = json.dumps(NESTED)
    huge_result = json.dumps(HUGE)
    # Emit deterministic output so the order-balanced gate can check equality.
    print(len(empty_result), len(simple_result), len(nested_result), len(huge_result))


main()
