# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import re
import subprocess
import sys


CORE_CASES = """
scalar_loop functions nested_function_no_closure if_else syntax_logical_lines docstring_suite_execution runtime_specialization_semantics
syntax_simple_suites statement_syntax module_statement_partials structural_pattern_matching
expression_operators chained_comparisons function_class_syntax future_annotations future_class_conditional_annotations function_metadata deferred_annotation_closure
object_type_model object_attribute_hooks inherited_getattr_slots inherited_eq_ne getattr_nondata_descriptor_precedence prepared_namespace_transform private_slots_mangling descriptor_protocol instance_stored_descriptor_value instance_dict_assignment instance_dict_contains_dispatch dict_get_missing_semantics dynamic_builtins_lookup custom_getattribute_descriptor multiple_inheritance_attribute_storage default_object_repr code_traceback_model mro_model
property_descriptor partialmethod_init_keywords chained_object_methods builtin_alias builtin_function_batch dynamic_execution_builtins globals_locals_identity
abc_runtime
iterator_protocol tuples tuple_methods dict_views container_dynamic_repr slices slots_model raw_strings string_compat
binary_buffers bytes_percent_width bytes_translate_delete bytes_isascii percent_mapping_protocol str_subclass_add str_subclass_percent str_subclass_comparison int_subclass_bitwise module_class_constructor_frame_growth builtin_types_edges builtin_subclass_init branch_join_instruction_fusion descriptor_noncallable frozenset_type_methods set_subclass_protocol set_identity_before_equality dict_custom_hash_equality metaclass_binary_operator metaclass_dynamic_keyword_expansion new_class_attribute generic_union_substitution optional_union_none_type typing_literal_union generic_class_parameters module_subclass_descriptor dict_init_mappingproxy ast_runtime_parser compile_ast_module annotation_string_format fstring_not_equal match_nested_as match_soft_keyword_annotation starred_expressions starred_protocol_iterables dict_set_comprehensions nested_comprehensions generator_expressions comprehension_multiple_filters
walrus_operator named_expression_call_trailing_comma unpacking annotated_assignment augmented_assignment assigned_binary_special_method lists_for sequences_index sequence_contains_method dict_set recursive_generator_closure closure_in_raise
str_join_generator re_sub_callable_none re_escaped_punctuation raw_blocks native_import sha2_complete json_module math_module math_trunc time_module native_sys_time_audit os_process_windows atexit_module io_os_modules io_module_streams
imp_stat_modules collections_queue_modules simple_queue_threadpool types_module traceback_module linecache_module runpy_module
pickle_module marshal_module
importlib_module zlib_module zipfile_module zipimport_module weakref_module inspect_module inspect_currentframe
dataclass_gc numeric_hash_invariant unicode_repr_surrogate
debug_frame_metadata debug_frame_source_edges debug_breakpoint_step logging_pathlib_modules socket_select_modules file_import
global_from_import package_import import_system_model relative_import_ellipsis_compile vfs_file_io file_context_open file_io_compat filesystem_io_edges io_wrapper_getattr_buffer io_open_layering thread_daemon_cleanup gc_rooted_candidate_graph
exceptions runtime_error_exceptions exception_unwind_with typed_exceptions exception_chaining_sys exception_self_context exception_instance_dict
finally_blocks classes class_dynamic_attrs context_managers context_manager_exception_traceback with_assignment_targets with_manager_lifetime input_builtin exception_group_split builtin_methods trace_hooks trace_events
trace_local_and_exception debug_trace_profile_edges sys_command_path sys_startup_config task_async async_syntax async_await_expression_precedence async_generator_await_exception_unwind async_generator_suppressed_cancellation threading_runtime_edges asyncio_runtime_edges closures nonlocal_counter
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
    actual = actual.replace("tests\\fixtures\\core\\", "tests/fixtures/core/")
    expected = normalize(expected_path.read_text(encoding="utf-8")).replace("tests\\fixtures\\core\\", "tests/fixtures/core/")
    if source.stem == "standard_modules" and sys.platform != "win32":
        expected = "\n".join(line for line in expected.splitlines() if not NON_WINDOWS_STANDARD.match(line))
    if actual != expected:
        raise RuntimeError(f"{source.stem} output mismatch\n--- expected ---\n{expected}\n--- actual ---\n{actual}")


def assert_failure(executable, source, required):
    result = subprocess.run([executable, str(source)], text=True, capture_output=True)
    output = result.stdout + result.stderr
    if result.returncode != 1 or any(fragment not in output for fragment in required):
        raise RuntimeError(f"unexpected failure result for {source.name}: {result.returncode}\n{output}")


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    root = pathlib.Path(__file__).resolve().parent
    for name in CORE_CASES:
        run_case(executable, root / "fixtures" / "core" / f"{name}.py", root / "fixtures" / "expected" / f"{name}.out", root)
    for name in SECTION_CASES:
        run_case(executable, root / "fixtures" / "compat_sections" / f"{name}.py", root / "fixtures" / "expected" / "compat_sections" / f"{name}.out", root)
    assert_failure(executable, root / "fixtures" / "core" / "uncaught_exception.py", ("Traceback (most recent call last):", "RuntimeError: top"))
    assert_failure(executable, root / "fixtures" / "core" / "uncaught_runtime_error.py", ("Traceback (most recent call last):", "ZeroDivisionError: division by zero"))
    assert_failure(executable, root / "fixtures" / "core" / "unset_instance_attr.py", ("Traceback (most recent call last):", "AttributeError: 'A' object has no attribute 'x'"))


if __name__ == "__main__":
    main()
