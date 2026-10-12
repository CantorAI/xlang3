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
"""Root-only untimed native-boundary diagnostic; not a benchmark or score.

Setup creates the one date before the counted operations. The two finite loops
exercise direct reduction and reconstruction only; they do not replace pickle,
install observers, measure elapsed time, or estimate a workload cost fraction.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


CP_EXE = Path("C:/Python/Python314/python.exe")
CP_DLL = Path("C:/Python/Python314/python314.dll")
X_EXE = Path("D:/CantorAI/xlang3/build-repro/main-verify-20261006/Release/xlang3.exe")
X_DLL = X_EXE.with_name("xlang3_runtime.dll")
DATETIME_SOURCE = Path("C:/Python/Python314/Lib/datetime.py")
PYDATETIME_SOURCE = Path("C:/Python/Python314/Lib/_pydatetime.py")
DATETIME_SHA256 = "8262c677654011417ae08751ae9c38edc8ca802d21c65232452529248a17aae9"
PYDATETIME_SHA256 = "63249ee7bac11a6c1cee7fae0355f1432d17c60bd3d503346cf9eab2cb875ec9"
REDUCTIONS = 64
RESTORATIONS = 64
EXPECTED_STATE = b"\x07\xbc\x05\x07"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def callable_metadata(value):
    target = getattr(value, "__func__", value)
    code = getattr(target, "__code__", None)
    kind = type(value)
    return {
        "type_module": kind.__module__,
        "type_name": kind.__name__,
        "name": getattr(value, "__name__", None),
        "has_python_code": code is not None,
        "python_filename": None if code is None else code.co_filename,
        "python_first_line": None if code is None else code.co_firstlineno,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", choices=("cpython3147", "xlang3"), required=True)
    parser.add_argument("--exe-sha256", required=True)
    parser.add_argument("--dll-sha256", required=True)
    args = parser.parse_args()
    assert sys.flags.optimize == 0
    assert sys.version_info[:3] == (3, 14, 7)
    assert sys.gettrace() is None and sys.getprofile() is None
    cpython = args.runtime == "cpython3147"
    assert sys.implementation.name == ("cpython" if cpython else "xlang3")
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
    assert Path(datetime.__file__).resolve(strict=True) == DATETIME_SOURCE.resolve(strict=True)
    try:
        import _datetime
    except ImportError as error:
        native_module = None
        import_failure = type(error).__name__
    else:
        native_module = _datetime
        import_failure = None
    assert (native_module is not None) == cpython
    if native_module is not None:
        assert native_module.date is datetime.date

    # This instance and the canonical constructor are setup, not loop counts.
    date_class = datetime.date
    value = date_class(1980, 5, 7)
    reducer = value.__reduce__
    reduction_count = 0
    restoration_count = 0
    for _ in range(REDUCTIONS):
        reduced = reducer()
        assert type(reduced) is tuple and len(reduced) == 2
        assert reduced[0] is date_class
        assert type(reduced[1]) is tuple and len(reduced[1]) == 1
        assert type(reduced[1][0]) is bytes and reduced[1][0] == EXPECTED_STATE
        reduction_count += 1
    for _ in range(RESTORATIONS):
        restored = date_class(EXPECTED_STATE)
        assert type(restored) is date_class
        assert (restored.year, restored.month, restored.day) == (1980, 5, 7)
        assert restored == value
        restoration_count += 1

    metadata = callable_metadata(reducer)
    assert metadata["has_python_code"] is (not cpython)
    assert reduction_count == REDUCTIONS and restoration_count == RESTORATIONS
    assert datetime.date is date_class
    assert sys.gettrace() is None and sys.getprofile() is None
    after = {path: digest(Path(path)) for path in pins}
    assert after == pins
    print(json.dumps({
        "status": "untimed_boundary_diagnostic_passed", "terminal": True,
        "diagnostic_only": True, "scored": False, "runtime": args.runtime,
        "executable": str(executable), "version_info": list(sys.version_info[:3]),
        "native_datetime_imported": native_module is not None,
        "native_datetime_import_failure_type": import_failure,
        "native_datetime_file": None if native_module is None else getattr(native_module, "__file__", None),
        "date_module": date_class.__module__, "date_qualname": date_class.__qualname__,
        "reducer": metadata, "constructor": callable_metadata(date_class.__new__),
        "direct_reduction_calls": reduction_count,
        "direct_state_restoration_calls": restoration_count,
        "expected_state_hex": EXPECTED_STATE.hex(),
        "canonical_reducer_class_and_fields_preserved": True,
        "setup_excluded_from_counts": True, "profile_enabled": False,
        "trace_enabled": False, "elapsed_time_measured": False,
        "original_pickle_body_run": False, "workload_cost_fraction_estimated": False,
        "hashes_before": pins, "hashes_after": after, "hashes_unchanged": True,
        "frequency_scope": "64 explicit reducer calls and 64 explicit state constructors; no original-workload frequency measurement",
    }, sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
