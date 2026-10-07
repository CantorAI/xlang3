# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import re
import subprocess
import sys
import tempfile


CORE_CASES = """
scalar_loop functions module_getattr_call nested_function_no_closure if_else syntax_logical_lines docstring_suite_execution runtime_specialization_semantics bytearray_inplace_add subclass_cached_mro vm_cache_materialization richards_boolean_method_inline constant_return_method_args runtime_protocol_fastcheck saved_frame_recursion_limit call_argument_last_use_transfer getitem_python_method_dispatch method_forwarding_semantics print_stringio_dispatch
syntax_simple_suites statement_syntax module_statement_partials structural_pattern_matching
expression_operators modulo_type_error chained_comparisons function_class_syntax future_annotations future_class_conditional_annotations function_metadata function_kwdefaults_live function_code_replacement function_annotation_getattr annotation_fake_globals annotation_lambda_ast deferred_annotation_closure
object_type_model type_self_metaclass_repr object_attribute_hooks inherited_getattr_slots inherited_eq_ne getattr_nondata_descriptor_precedence prepared_namespace_transform private_slots_mangling private_generator_name_mangling subprocess_inherits_chdir descriptor_protocol special_truth_descriptor instance_stored_descriptor_value instance_dict_assignment instance_dict_contains_dispatch dict_get_missing_semantics dynamic_builtins_lookup custom_getattribute_descriptor load_attr_cache_precedence multiple_inheritance_attribute_storage default_object_repr code_traceback_model mro_model tuple_compare_scalar_fastpath object_hash_identity_fastpath
property_descriptor partialmethod_init_keywords chained_object_methods builtin_alias builtin_function_batch dynamic_execution_builtins globals_locals_identity
abc_runtime native_module_function_binding try_loop_exception_scope math_combinatorics csv_reader_dialect_keyword array_w_unicode regex_lookbehind_anchor_alternative ssl_timed_socket value_alias_assignment ctypes_structure_layout ctypes_pointer_return ctypes_cast_errcheck_array itertools_zip_longest_shared minmax_custom_key
iterator_protocol builtin_iterator_identity ast_recursive_visitor ast_match_patterns ast_while_live_local ast_traceback_multiline vm_borrowed_local_overwrite match_protocol_semantics tuples tuple_methods dict_views dict_iterator_ownership container_dynamic_repr slices slots_model raw_strings string_compat
binary_buffers bytes_percent_width bytes_translate_delete bytes_isascii percent_mapping_protocol str_subclass_add str_subclass_percent str_subclass_comparison int_subclass_bitwise module_class_constructor_frame_growth builtin_types_edges builtin_subclass_init branch_join_instruction_fusion descriptor_noncallable frozenset_type_methods set_subclass_protocol set_identity_before_equality dict_custom_hash_equality metaclass_binary_operator metaclass_dynamic_keyword_expansion new_class_attribute generic_union_substitution optional_union_none_type typing_literal_union generic_class_parameters module_subclass_descriptor dict_init_mappingproxy ast_runtime_parser compile_ast_module annotation_string_format fstring_not_equal match_nested_as match_soft_keyword_annotation starred_expressions starred_protocol_iterables dict_set_comprehensions nested_comprehensions generator_expressions comprehension_multiple_filters
walrus_operator named_expression_call_trailing_comma unpacking annotated_assignment augmented_assignment assigned_binary_special_method lists_for sequences_index sequence_contains_method dict_set recursive_generator_closure nested_generator_comprehension_capture yield_from_protocol yield_from_deep_trampoline yield_from_custom_throw_close oserror_subclass_errno closure_in_raise
str_join_generator re_sub_callable_none re_escaped_punctuation raw_blocks native_import sha2_complete json_module math_module math_trunc time_module native_sys_time_audit os_process_windows atexit_module io_os_modules io_module_streams
imp_stat_modules collections_queue_modules simple_queue_threadpool types_module traceback_module linecache_module runpy_module
call_ex_varargs_star_fastpath call_ex_varargs_keyword_defaults call_varargs_kwargs_empty_fastpath call_default_positional_frame_fastpath call_keyword_second_param_fastpath callex_function_target_cache call_ex_constructor_cache
pickle_module marshal_module importlib_spec_package importlib_spec_loader_keywords
importlib_module importlib_raw_magic_number import_cached_child_export missing_module_exception compile_ast_source_locations compile_ast_decorators compile_ast_class_annotations compile_ast_with compile_ast_unpack compile_ast_scope compile_ast_delete compile_ast_try compile_ast_yield compile_ast_while compile_ast_slice compile_ast_list_comprehension compile_ast_for compile_ast_lambda_generator compile_ast_ellipsis compile_ast_set_dict_comprehension compile_ast_starred ast_leading_zero_literal compile_ast_function_signature compile_ast_break_continue compile_ast_named_expr compile_ast_await class_name_assignment exec_live_globals marshal_bigint_code zlib_module zipfile_module zipimport_module weakref_module weakref_thread_lifetime weakref_hash_cache generator_frame_referrers generator_result_release delegated_exception_result except_target_cleanup comprehension_target_lifetime set_weakref_hash set_hash_caching set_growing_index math_real_type_error contextvar_generic_alias context_run_empty_star context_run_same_context_reentry set_remove_key_error inspect_module sequence_int_subclass memoryview_iteration socket_bytes_hostname socket_nonblocking_errors socket_closed_errors socket_index_args dict_values_equality set_constructor_custom_hash format_property_field mmap_native regex_lookbehind_capture inspect_currentframe
dataclass_gc numeric_hash_invariant decimal_string_format decimal_native_arithmetic decimal_native_quantize decimal_native_string asyncio_native_gc_traversal async_taskgroup_current_task asyncio_native_call_method_dispatch asyncio_native_future_constructor asyncio_native_task_constructor asyncio_future_new_module_rebind asyncio_scheduled_tasks_add_patch asyncio_task_init_override context_run_bound_callback unicode_repr_surrogate callmethod_instance_shadow_cache call_method_negative_instance_shadow_cache callmethod_exact_positional_cache call_accumulate_noninteger_fallback call_argument_ownership_transfer classmethod_attr_int_compare
debug_frame_metadata debug_frame_source_edges debug_breakpoint_step logging_pathlib_modules socket_select_modules file_import
global_from_import package_import import_system_model relative_import_ellipsis_compile vfs_file_io file_context_open file_io_compat filesystem_io_edges io_wrapper_getattr_buffer io_base_iteration io_open_layering thread_daemon_cleanup gc_rooted_candidate_graph
exceptions runtime_error_exceptions exception_unwind_with typed_exceptions exception_chaining_sys exception_self_context exception_instance_dict
inline_attr_constructor guarded_numeric_mixed_float list_append_batch property_access_batch range_sum_fusion float_point_fastpaths json_native_encoder_path bound_method_keyword_repeated lru_cache_method_binding lru_cache_positional_tuple weakref_cached_hash_lru gzip_buffered_tell regex_fixed_width_alt_lookbehind yield_from_full_expression
finally_blocks classes class_dynamic_attrs descriptor_setter_only_reads descriptor_callable_keyword context_managers context_manager_exception_traceback with_assignment_targets with_manager_lifetime input_builtin exception_group_split builtin_methods native_list_append_unbound native_string_methods_unbound str_subclass_native_repr trace_hooks trace_events
trace_local_and_exception debug_trace_profile_edges sys_command_path sys_startup_config task_async async_syntax async_await_expression_precedence async_generator_await_exception_unwind async_generator_suspended_exception async_generator_suppressed_cancellation threading_runtime_edges thread_target_lifetime signal_main_thread symlink_realpath asyncio_runtime_edges closures nonlocal_counter closure_cell_semantics
any_generator_consume_fastpath sum_generator_consume_fastpath
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
    # Some fixtures spawn children that inherit stdio handles; files let us
    # observe the direct process exit without waiting for every child pipe EOF.
    with tempfile.TemporaryFile() as stdout_file, tempfile.TemporaryFile() as stderr_file:
        result = subprocess.run([executable, str(source)], stdout=stdout_file, stderr=stderr_file)
        stdout_file.seek(0)
        stderr_file.seek(0)
        stdout = stdout_file.read().decode("utf-8", errors="replace")
        stderr = stderr_file.read().decode("utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(f"{source.stem} failed ({result.returncode}):\n{stdout}{stderr}")
    actual = normalize(stdout).replace(str(root), "tests")
    actual = actual.replace("tests\\fixtures\\core\\", "tests/fixtures/core/")
    expected = normalize(expected_path.read_text(encoding="utf-8")).replace("tests\\fixtures\\core\\", "tests/fixtures/core/")
    if source.stem == "standard_modules" and sys.platform != "win32":
        expected = "\n".join(line for line in expected.splitlines() if not NON_WINDOWS_STANDARD.match(line))
    if actual != expected:
        raise RuntimeError(f"{source.stem} output mismatch\n--- expected ---\n{expected}\n--- actual ---\n{actual}")


def assert_failure(executable, source, required):
    with tempfile.TemporaryFile() as stdout_file, tempfile.TemporaryFile() as stderr_file:
        result = subprocess.run([executable, str(source)], stdout=stdout_file, stderr=stderr_file)
        stdout_file.seek(0)
        stderr_file.seek(0)
        output = stdout_file.read().decode("utf-8", errors="replace") + stderr_file.read().decode("utf-8", errors="replace")
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
