# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import re
import subprocess
import sys


CORE_CASES = """
scalar_loop functions nested_function_no_closure if_else syntax_logical_lines
syntax_simple_suites statement_syntax module_statement_partials structural_pattern_matching
expression_operators chained_comparisons function_class_syntax future_annotations function_metadata
object_type_model object_attribute_hooks descriptor_protocol code_traceback_model mro_model
property_descriptor chained_object_methods builtin_alias builtin_function_batch dynamic_execution_builtins
iterator_protocol tuples tuple_methods dict_views slices slots_model raw_strings string_compat
binary_buffers starred_expressions dict_set_comprehensions nested_comprehensions generator_expressions
walrus_operator unpacking annotated_assignment augmented_assignment lists_for sequences_index dict_set
raw_blocks native_import json_module math_module time_module native_sys_time_audit atexit_module io_os_modules io_module_streams
imp_stat_modules collections_queue_modules types_module traceback_module linecache_module runpy_module
importlib_module zlib_module zipfile_module weakref_module inspect_module inspect_currentframe
debug_frame_metadata debug_breakpoint_step logging_pathlib_modules socket_select_modules file_import
global_from_import package_import import_system_model vfs_file_io file_context_open file_io_compat
exceptions runtime_error_exceptions exception_unwind_with typed_exceptions exception_chaining_sys
finally_blocks classes class_dynamic_attrs context_managers builtin_methods trace_events
trace_local_and_exception task_async async_syntax closures nonlocal_counter
""".split()

SECTION_CASES = """
module_and_statement_syntax function_and_class_syntax expression_syntax core_value_and_object_model
functions_and_calls exceptions containers strings_and_unicode imports_and_modules builtins standard_modules
""".split()

NON_WINDOWS_STANDARD = re.compile(
    r"^(winapi-native |time-clock-info-windows |strftime-invalid |sys-windowsversion-|"
    r"sys-noarg-keyword (getwindowsversion|_enablelegacywindowsfsencoding) |2147483649 131097 1 None$)"
)


def normalize(text):
    return text.replace("\r\n", "\n").rstrip()


def run_case(executable, source, expected_path, root):
    result = subprocess.run([executable, str(source)], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{source.stem} failed ({result.returncode}):\n{result.stdout}{result.stderr}")
    actual = normalize(result.stdout).replace(str(root), "tests")
    expected = normalize(expected_path.read_text(encoding="utf-8")).replace("tests\\fixtures\\core\\", "tests/fixtures/core/")
    if source.stem == "standard_modules":
        expected = "\n".join(line for line in expected.splitlines() if not NON_WINDOWS_STANDARD.match(line))
    if actual != expected:
        raise RuntimeError(f"{source.stem} output mismatch\n--- expected ---\n{expected}\n--- actual ---\n{actual}")


def assert_failure(executable, source, required):
    result = subprocess.run([executable, str(source)], text=True, capture_output=True)
    output = result.stdout + result.stderr
    if result.returncode != 1 or any(fragment not in output for fragment in required):
        raise RuntimeError(f"unexpected failure result for {source.name}: {result.returncode}\n{output}")


def main():
    executable = sys.argv[1]
    root = pathlib.Path(__file__).resolve().parent
    for name in CORE_CASES:
        run_case(executable, root / "fixtures" / "core" / f"{name}.py", root / "fixtures" / "expected" / f"{name}.out", root)
    for name in SECTION_CASES:
        run_case(executable, root / "fixtures" / "compat_sections" / f"{name}.py", root / "fixtures" / "expected" / "compat_sections" / f"{name}.out", root)
    assert_failure(executable, root / "fixtures" / "core" / "uncaught_exception.py", ("Traceback (most recent call last):", "RuntimeError: top"))
    assert_failure(executable, root / "fixtures" / "core" / "uncaught_runtime_error.py", ("Traceback (most recent call last):", "ZeroDivisionError: division by zero"))
    assert_failure(executable, root / "fixtures" / "core" / "unset_instance_attr.py", ("Traceback (most recent call last):", "AttributeError: object has no attribute"))


if __name__ == "__main__":
    main()
