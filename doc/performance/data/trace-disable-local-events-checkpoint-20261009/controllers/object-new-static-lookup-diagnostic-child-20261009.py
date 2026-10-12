# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""Artificial lookup-route diagnostic; no benchmark score or workload share."""

import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

CP = Path("C:/Python/Python314/python.exe")
CONTROL = Path(
    "D:/CantorAI/xlang3/build-repro/controls/"
    "lambda-eager-comprehension-capture-accepted-r5-20261009/"
    "Release/xlang3.exe"
)
CURRENT = Path(
    "D:/CantorAI/xlang3/build-repro/main-verify-20261006/"
    "Release/xlang3.exe"
)
OPERATIONS = 10000
STATE = b"\x07\xbc\x05\x07"
CASES = ("original_lookup", "saved_native_lookup")


class EntryOnly:
    __slots__ = ()

    def __new__(cls, state):
        return object.__new__(cls)


# Deliberately changed lookup policy, captured outside all timers.
saved_object_new = object.__new__


def saved_native_wrapper(cls, state):
    return saved_object_new(cls)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def function_metadata(function):
    code = function.__code__
    return {
        "name": function.__name__,
        "filename": code.co_filename,
        "first_line": code.co_firstlineno,
        "argcount": code.co_argcount,
        "kwonlyargcount": code.co_kwonlyargcount,
        "freevars": list(code.co_freevars),
        "cellvars": list(code.co_cellvars),
    }


def optional_native_metadata(function):
    # A legitimate native wrapper need not expose these fields, or represent
    # __self__ as None. Record primitive observations without admission tests.
    fields = {}
    for name in ("__name__", "__qualname__", "__self__", "__code__"):
        try:
            value = getattr(function, name)
        except Exception as error:
            fields[name] = {"supported": False, "error_type": type(error).__name__}
        else:
            observation = {
                "supported": True,
                "type_name": type(value).__name__,
                "is_none": value is None,
            }
            if isinstance(value, str):
                observation["text"] = value
            if name == "__self__":
                observation["is_object_class"] = value is object
                observation["is_allocated_class"] = value is EntryOnly
            fields[name] = observation
    return fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--selection",
        choices=("cpython-python", "xlang3-control", "xlang3-current"),
        required=True,
    )
    parser.add_argument("--case-order", required=True)
    parser.add_argument("--runtime-executable", type=Path, required=True)
    parser.add_argument("--exe-sha256", required=True)
    parser.add_argument("--dll-sha256", required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()

    cpython = args.selection == "cpython-python"
    executable = {
        "cpython-python": CP,
        "xlang3-control": CONTROL,
        "xlang3-current": CURRENT,
    }[args.selection]
    dll = executable.with_name(
        "python314.dll" if cpython else "xlang3_runtime.dll"
    )
    source = Path(__file__).resolve(strict=True)

    if sys.flags.optimize:
        raise RuntimeError("Assertions must remain enabled")
    assert sys.version_info[:3] == (3, 14, 7)
    assert sys.implementation.name == ("cpython" if cpython else "xlang3")
    assert args.runtime_executable.resolve(strict=True) == executable.resolve()
    assert Path(sys.executable).resolve() == executable.resolve()
    if cpython:
        assert sys.flags.isolated
    assert sys.gettrace() is None and sys.getprofile() is None

    pins = {
        str(executable): args.exe_sha256,
        str(dll): args.dll_sha256,
        str(source): args.source_sha256,
    }
    assert all(sha(path) == expected for path, expected in pins.items())
    order = args.case_order.split(",")
    assert len(order) == 2 and set(order) == set(CASES)

    original_wrapper = EntryOnly.__new__
    original_code = original_wrapper.__code__
    altered_code = saved_native_wrapper.__code__
    original_object = object
    assert callable(saved_object_new)
    assert saved_object_new is object.__new__
    native_metadata = optional_native_metadata(saved_object_new)

    for function in (original_wrapper, saved_native_wrapper):
        code = function.__code__
        assert code.co_argcount == 2 and code.co_kwonlyargcount == 0
        assert code.co_freevars == () and code.co_cellvars == ()
        assert function.__defaults__ is None
        assert function.__kwdefaults__ is None
        assert Path(code.co_filename).resolve() == source

    assert EntryOnly.__slots__ == ()
    assert "__init__" not in EntryOnly.__dict__
    assert EntryOnly.__init__ is object.__init__
    functions = {
        "original_lookup": original_wrapper,
        "saved_native_lookup": saved_native_wrapper,
    }
    iterations = range(OPERATIONS)
    seconds = {}

    def check(value):
        assert type(value) is EntryOnly
        assert not hasattr(value, "__dict__")

    # Both routes use this same loop and ordinary Python-function callsite.
    # One untimed validation/warm call precedes each timed loop.
    for name in order:
        function = functions[name]
        assert sys.gettrace() is None and sys.getprofile() is None
        value = function(EntryOnly, STATE)
        check(value)

        if not args.verify_only:
            start = time.perf_counter()
            for _ in iterations:
                value = function(EntryOnly, STATE)
            elapsed = time.perf_counter() - start
            check(value)
            assert elapsed > 0
            seconds[name] = elapsed

    assert object is original_object
    assert object.__new__ is saved_object_new
    assert EntryOnly.__new__ is original_wrapper
    assert original_wrapper.__code__ is original_code
    assert saved_native_wrapper.__code__ is altered_code
    assert EntryOnly.__slots__ == ()
    assert EntryOnly.__init__ is object.__init__
    assert "__init__" not in EntryOnly.__dict__
    assert sys.gettrace() is None and sys.getprofile() is None
    after = {path: sha(path) for path in pins}
    assert after == pins

    print(json.dumps({
        "status": "artificial_object_new_lookup_diagnostic_passed",
        "terminal": True,
        "verify_only": args.verify_only,
        "diagnostic_only": True,
        "scored": False,
        "selection": args.selection,
        "case_order": order,
        "seconds": seconds,
        "operations_per_case": OPERATIONS,
        "timed_operation_count": 0 if args.verify_only else 2 * OPERATIONS,
        "untimed_validation_warm_calls_per_case": 1,
        "original_wrapper": function_metadata(original_wrapper),
        "altered_wrapper": function_metadata(saved_native_wrapper),
        "native_callable_type": type(saved_object_new).__name__,
        "native_optional_metadata": native_metadata,
        "native_callable_identity_unchanged": True,
        "python_callable_identities_unchanged": True,
        "same_allocated_class": True,
        "plain_slots_empty": True,
        "profile_enabled": False,
        "trace_enabled": False,
        "hashes_before": pins,
        "hashes_after": after,
        "hashes_unchanged": True,
        "setup_checks_excluded_from_timers": True,
        "loop_and_result_replacement_included": True,
        "original_pickle_body_run": False,
        "workload_cost_fraction_estimated": False,
        "scope": (
            "Original Python object.__new__(cls) body versus an altered "
            "saved-native lookup body. Both keep Python function entry and "
            "instance allocation. Saved lookup intentionally does not preserve "
            "dynamic global/class attribute replacement during the loop. "
            "No suite score, lookup-only cost share or projected gain."
        ),
    }, sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
