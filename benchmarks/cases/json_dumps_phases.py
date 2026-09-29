"""Split the pyperformance json_dumps inputs to localize encoder overhead.

This is a diagnostic, not a replacement for pyperf. It preserves the four
payload shapes and repetition counts from pyperformance 1.14's bm_json_dumps.
"""
import json
import time


EMPTY = ({}, 2000)
SIMPLE_DATA = {
    "key1": 0,
    "key2": True,
    "key3": "value",
    "key4": "foo",
    "key5": "string",
}
SIMPLE = (SIMPLE_DATA, 1000)
NESTED_DATA = {
    "key1": 0,
    "key2": SIMPLE_DATA,
    "key3": "value",
    "key4": SIMPLE_DATA,
    "key5": SIMPLE_DATA,
    "key": "ąćż",
}
NESTED = (NESTED_DATA, 1000)
HUGE = ([NESTED_DATA] * 1000, 1)


def main():
    for name, (obj, count) in (
        ("EMPTY", EMPTY),
        ("SIMPLE", SIMPLE),
        ("NESTED", NESTED),
        ("HUGE", HUGE),
    ):
        start = time.perf_counter()
        for _ in range(count):
            result = json.dumps(obj)
        elapsed = time.perf_counter() - start
        print(name, count, len(result), f"{elapsed:.6f}")


if __name__ == "__main__":
    main()
