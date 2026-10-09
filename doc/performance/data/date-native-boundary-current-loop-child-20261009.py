# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Root-only artificial date-loop diagnostic, not an official benchmark.

Three selections distinguish CPython's native date boundary from the same
_pydatetime Python methods on CPython and XLang3. Setup, validation, imports,
hashing, and metadata are outside the two timers. Loop and result replacement
costs are included; these times do not estimate pickle's date cost fraction.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time


CP_EXE = Path("C:/Python/Python314/python.exe")
CP_DLL = Path("C:/Python/Python314/python314.dll")
X_EXE = Path("D:/CantorAI/xlang3/build-repro/main-verify-20261006/Release/xlang3.exe")
X_DLL = X_EXE.with_name("xlang3_runtime.dll")
DATETIME_SOURCE = Path("C:/Python/Python314/Lib/datetime.py")
PYDATETIME_SOURCE = Path("C:/Python/Python314/Lib/_pydatetime.py")
DATETIME_SHA256 = "8262c677654011417ae08751ae9c38edc8ca802d21c65232452529248a17aae9"
PYDATETIME_SHA256 = "63249ee7bac11a6c1cee7fae0355f1432d17c60bd3d503346cf9eab2cb875ec9"
REDUCTIONS = 10000
RESTORATIONS = 10000
EXPECTED_STATE = b"\x07\xbc\x05\x07"
EXPECTED_FIELDS = (1980, 5, 7)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def callable_metadata(value):
    target = getattr(value, "__func__", value)
    code = getattr(target, "__code__", None)
    return {
        "type_module": type(value).__module__,
        "type_name": type(value).__name__,
        "name": getattr(value, "__name__", None),
        "has_python_code": code is not None,
        "python_filename": None if code is None else code.co_filename,
        "python_first_line": None if code is None else code.co_firstlineno,
    }


def check_reduction(reduced, selected_class):
    assert type(reduced) is tuple and len(reduced) == 2
    assert reduced[0] is selected_class
    assert type(reduced[1]) is tuple and len(reduced[1]) == 1
    assert type(reduced[1][0]) is bytes and reduced[1][0] == EXPECTED_STATE


def check_restoration(restored, selected_class, setup_value):
    assert type(restored) is selected_class
    assert (restored.year, restored.month, restored.day) == EXPECTED_FIELDS
    assert restored == setup_value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", choices=("cpython-native", "cpython-python", "xlang3-public"), required=True)
    parser.add_argument("--exe-sha256", required=True)
    parser.add_argument("--dll-sha256", required=True)
    args = parser.parse_args()
    cpython = args.selection != "xlang3-public"
    native_selection = args.selection == "cpython-native"
    assert sys.flags.optimize == 0
    assert sys.version_info[:3] == (3, 14, 7)
    assert sys.implementation.name == ("cpython" if cpython else "xlang3")
    assert sys.gettrace() is None and sys.getprofile() is None
    executable, runtime_dll = (CP_EXE, CP_DLL) if cpython else (X_EXE, X_DLL)
    assert Path(sys.executable).resolve(strict=True) == executable.resolve(strict=True)
    pins = {
        str(executable): args.exe_sha256,
        str(runtime_dll): args.dll_sha256,
        str(DATETIME_SOURCE): DATETIME_SHA256,
        str(PYDATETIME_SOURCE): PYDATETIME_SHA256,
    }
    assert all(digest(Path(path)) == expected for path, expected in pins.items())

    import datetime
    import _pydatetime
    assert Path(datetime.__file__).resolve(strict=True) == DATETIME_SOURCE.resolve(strict=True)
    assert Path(_pydatetime.__file__).resolve(strict=True) == PYDATETIME_SOURCE.resolve(strict=True)
    try:
        import _datetime
    except ImportError as error:
        native_module = None
        import_failure = type(error).__name__
    else:
        native_module = _datetime
        import_failure = None
    assert (native_module is not None) == cpython
    if cpython:
        assert datetime.date is native_module.date
        assert _pydatetime.date is not datetime.date
    else:
        assert datetime.date is _pydatetime.date
    public_date_before = datetime.date
    python_date_before = _pydatetime.date
    selected_class = _pydatetime.date if args.selection == "cpython-python" else datetime.date
    setup_value = selected_class(1980, 5, 7)
    reducer = setup_value.__reduce__
    reducer_metadata = callable_metadata(reducer)
    constructor_metadata = callable_metadata(selected_class.__new__)
    assert reducer_metadata["has_python_code"] is (not native_selection)
    assert constructor_metadata["has_python_code"] is (not native_selection)

    # One untimed validation of each operation also warms its entry path.
    reduced = reducer()
    check_reduction(reduced, selected_class)
    restored = selected_class(EXPECTED_STATE)
    check_restoration(restored, selected_class, setup_value)
    reduction_iterations = range(REDUCTIONS)
    restoration_iterations = range(RESTORATIONS)
    assert sys.gettrace() is None and sys.getprofile() is None

    start = time.perf_counter()
    for _ in reduction_iterations:
        reduced = reducer()
    reduction_seconds = time.perf_counter() - start
    check_reduction(reduced, selected_class)

    start = time.perf_counter()
    for _ in restoration_iterations:
        restored = selected_class(EXPECTED_STATE)
    restoration_seconds = time.perf_counter() - start
    check_restoration(restored, selected_class, setup_value)

    assert reduction_seconds > 0 and restoration_seconds > 0
    assert datetime.date is public_date_before and _pydatetime.date is python_date_before
    assert sys.gettrace() is None and sys.getprofile() is None
    after = {path: digest(Path(path)) for path in pins}
    assert after == pins
    print(json.dumps({
        "status": "artificial_date_loop_diagnostic_passed", "terminal": True,
        "diagnostic_only": True, "scored": False, "selection": args.selection,
        "runtime": "cpython3147" if cpython else "xlang3",
        "executable": str(executable), "version_info": list(sys.version_info[:3]),
        "native_datetime_imported": native_module is not None,
        "native_datetime_import_failure_type": import_failure,
        "selected_date_is_public_date": selected_class is datetime.date,
        "selected_date_is_python_date": selected_class is _pydatetime.date,
        "date_module": selected_class.__module__, "date_qualname": selected_class.__qualname__,
        "reducer": reducer_metadata, "constructor": constructor_metadata,
        "timed_reduction_calls": REDUCTIONS,
        "timed_state_restoration_calls": RESTORATIONS,
        "reduction_seconds": reduction_seconds,
        "restoration_seconds": restoration_seconds,
        "untimed_validation_reduction_calls": 1,
        "untimed_validation_restoration_calls": 1,
        "setup_date_count": 1, "expected_state_hex": EXPECTED_STATE.hex(),
        "expected_fields": list(EXPECTED_FIELDS),
        "selected_reducer_class_and_fields_preserved": True,
        "setup_imports_checks_and_hashing_excluded_from_timers": True,
        "loop_and_result_replacement_costs_included": True,
        "profile_enabled": False, "trace_enabled": False,
        "original_pickle_body_run": False,
        "workload_cost_fraction_estimated": False,
        "hashes_before": pins, "hashes_after": after, "hashes_unchanged": True,
        "timing_scope": "two artificial loops of 10000 direct reductions and 10000 bytes-state constructors; no official score or inferred suite gain",
    }, sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
