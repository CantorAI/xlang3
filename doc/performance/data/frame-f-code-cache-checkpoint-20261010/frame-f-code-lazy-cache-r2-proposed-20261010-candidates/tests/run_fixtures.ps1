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
param(
    [string]$XLang3
)

$ErrorActionPreference = "Stop"

# XLang3 writes UTF-8. Windows PowerShell otherwise decodes native stdout
# using IBM437 when CTest launches it without a console, corrupting Unicode
# before the fixture comparison even though Get-Content already reads UTF-8.
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

if (-not $XLang3) {
    throw "XLang3 executable path is required"
}

$root = $PSScriptRoot
$cases = @(
    "sorted_key_scoped_entry",
    "sorted_key_iteration_owner",
    "sorted_key_nested_handled_context",
    "ordinary_canonical_slot_constructor",
    "call_module_global_binding",
    "sorted_exact_integer_keys",
    "native_str_utf8_encode",
    "synchronous_class_argument_lifetime",
    "nested_profile_setting",
    "nested_trace_setting",
    "frame_f_code_cache",
    "trace_disable_local_events",
    "class_namespace_lifetime",
    "class_method_annotation_capture",
    "explicit_slot_descriptor_fallback",
    "slot_descriptor_owner",
    "dict_get_exception_preservation",
    "call_method_dict_cache_touch",
    "inherited_call_ex_constructor",
    "call_ex_cross_activation_constructor",
    "sqlite_statement_cache",
    "native_bound_zero_args",
    "identity_last_use",
    "hash_exception_preservation",
    "sqlite_cursor_completion",
    "sqlite_native_aggregate",
    "scalar_loop",
    "functions",
    "keyword_method_call",
    "module_getattr_call",
    "nested_function_no_closure",
    "if_else",
    "syntax_logical_lines",
    "docstring_suite_execution",
    "runtime_specialization_semantics",
    "syntax_simple_suites",
    "statement_syntax",
    "module_statement_partials",
    "module_subclass_constructor",
    "module_subclass_import_probe",
    "structural_pattern_matching",
    "expression_operators",
    "modulo_type_error",
    "bigint_base_format",
    "chained_comparisons",
    "function_class_syntax",
    "future_annotations",
    "function_metadata",
    "function_annotation_getattr",
    "annotation_fake_globals",
    "annotation_lambda_ast",
    "generic_class_annotation_scope",
    "generic_class_frame_locals",
    "float_identity_containers",
    "bool_numeric_properties",
    "decorator_warning_line",
    "breakpoint_builtin",
    "deferred_annotation_closure",
    "object_type_model",
    "object_attribute_hooks",
    "inherited_eq_ne",
    "getattr_nondata_descriptor_precedence",
    "set_identity_before_equality",
    "dict_custom_hash_equality",
    "metaclass_binary_operator",
    "metaclass_dynamic_keyword_expansion",
    "new_class_attribute",
    "generic_union_substitution",
    "optional_union_none_type",
    "generic_class_parameters",
    "module_subclass_descriptor",
    "dict_init_mappingproxy",
    "ast_runtime_parser",
    "ast_assert_roundtrip",
    "compile_ast_module",
    "annotation_string_format",
    "descriptor_protocol",
    "default_object_repr",
    "code_traceback_model",
    "mro_model",
    "property_descriptor",
    "property_callable_getter",
    "lambda_eager_comprehension_capture",
    "python_new_vm_continuation",
    "python_new_completion_finalizer",
    "chained_object_methods",
    "builtin_alias",
    "builtin_function_batch",
    "dynamic_execution_builtins",
    "globals_locals_identity",
    "iterator_protocol",
    "tuples",
    "tuple_methods",
    "dict_views",
    "slices",
    "string_slice_offsets",
    "regex_borrowed_ascii_match",
    "abc_descriptor_abstract",
    "super_replaced_method",
    "functional_iterator_abc",
    "metaclass_getattr",
    "metaclass_getattribute",
    "inspect_class_source_block",
    "dict_view_contains",
    "importlib_pathlike_spec",
    "bytes_subclass_constructor",
    "class_with_bindings",
    "comprehension_iterable_scope",
    "container_str_repr",
    "class_attr_callable",
    "vars_class_mappingproxy",
    "typing_generic_class_getitem",
    "builtin_type_signatures",
    "deferred_nested_annotation",
    "operator_or_typing_alias",
    "ast_lambda_annotation",
    "slots_model",
    "raw_strings",
    "string_compat",
    "percent_mapping_protocol",
    "str_subclass_add",
    "str_subclass_percent",
    "str_subclass_comparison",
    "int_subclass_bitwise",
    "module_class_constructor_frame_growth",
    "binary_buffers",
    "bytes_translate_delete",
    "bytes_count_integer",
    "set_union_bulk",
    "zlib_strict_decompressor_kwargs",
    "sum_start_keyword",
    "int_enum_subtraction",
    "gzip_missing_cleanup",
    "gzip_finalizer_cleanup",
    "dict_hash_index",
    "bytes_isascii",
    "typing_literal_union",
    "sequence_contains_method",
    "instance_dict_contains_dispatch",
    "dict_get_missing_semantics",
    "assigned_binary_special_method",
    "dynamic_builtins_lookup",
    "custom_getattribute_descriptor",
    "multiple_inheritance_attribute_storage",
    "builtin_types_edges",
    "starred_expressions",
    "dict_set_comprehensions",
    "nested_comprehensions",
    "generator_expressions",
    "closure_in_raise",
    "walrus_operator",
    "named_expression_call_trailing_comma",
    "unpacking",
    "annotated_assignment",
    "augmented_assignment",
    "lists_for",
    "sequences_index",
    "dict_set",
    "raw_blocks",
    "re_sub_callable_none",
    "re_escaped_punctuation",
    "native_import",
    "sha2_complete",
    "json_module",
    "math_module",
    "time_module",
    "native_sys_time_audit",
    "os_process_windows",
    "atexit_module",
    "io_os_modules",
    "io_module_streams",
    "imp_stat_modules",
    "pickle_module",
    "collections_queue_modules",
    "simple_queue_threadpool",
    "types_module",
    "traceback_module",
    "linecache_module",
    "runpy_module",
    "importlib_module",
    "import_cached_child_export",
    "missing_module_exception",
    "import_submodule_failure",
    "compile_ast_decorators",
    "compile_ast_class_annotations",
    "compile_ast_with",
    "compile_ast_unpack",
    "compile_ast_scope",
    "compile_ast_delete",
    "compile_ast_try",
    "compile_ast_yield",
    "compile_ast_while",
    "compile_ast_slice",
    "compile_ast_list_comprehension",
    "compile_ast_for",
    "compile_ast_lambda_generator",
    "compile_ast_ellipsis",
    "compile_ast_set_dict_comprehension",
    "compile_ast_starred",
    "ast_leading_zero_literal",
    "compile_ast_function_signature",
    "compile_ast_break_continue",
    "compile_ast_named_expr",
    "compile_ast_await",
    "class_name_assignment",
    "exec_live_globals",
    "exec_missing_name",
    "int_subclass_copy",
    "random_mt19937_state",
    "init_subclass_exception",
    "type_alias_statement",
    "type_alias_ast",
    "class_subscription",
    "type_alias_union",
    "generic_definition_ast",
    "generic_class_base",
    "generic_string_forward_ref",
    "generic_alias_hash",
    "generic_alias_resubscript",
    "lambda_dict_unpack",
    "dict_fromkeys_unhashable",
    "resolved_prepare_bases",
    "ast_nested_fstring_builtin",
    "ast_container_format",
    "builtin_generic_subclasses",
    "type_parameter_repr",
    "gc_class_components",
    "gc_generic_cycles",
    "two_argument_double_ir_plan",
    "deque_class_getitem",
    "sre_class_getitem",
    "exception_group_class_getitem",
    "recursive_property_error",
    "importlib_spec_package",
    "importlib_spec_loader_keywords",
    "marshal_bigint_code",
    "stale_bytecode_import",
    "zlib_module",
    "zipfile_module",
    "zipimport_module",
    "weakref_module",
    "weakref_thread_lifetime",
    "weakref_hash_cache",
    "generator_frame_referrers",
    "generator_result_release",
    "delegated_exception_result",
    "except_target_cleanup",
    "comprehension_target_lifetime",
    "set_weakref_hash",
    "set_hash_caching",
    "math_real_type_error",
    "contextvar_generic_alias",
    "set_remove_key_error",
    "set_hash_equality",
    "instance_mapping_comparison",
    "instance_dict_attribute_delete",
    "class_eq_hash_rule",
    "dir_instance_mapping",
    "instance_dict_method_visibility",
    "object_init_subclass_keywords",
    "type_subclasses_descriptor",
    "private_name_all_underscores",
    "private_generator_name_mangling",
    "subprocess_inherits_chdir",
    "object_getattribute_descriptor",
    "builtin_iterator_identity",
    "ast_recursive_visitor",
    "ast_match_patterns",
    "ast_while_live_local",
    "ast_traceback_multiline",
    "vm_borrowed_local_overwrite",
    "compile_ast_source_locations",
    "match_protocol_semantics",
    "inspect_module",
    "sequence_int_subclass",
    "memoryview_iteration",
    "socket_bytes_hostname",
    "dict_values_equality",
    "set_constructor_custom_hash",
    "format_property_field",
    "mmap_native",
    "regex_lookbehind_capture",
    "inspect_currentframe",
    "debug_frame_metadata",
    "debug_frame_source_edges",
    "debug_breakpoint_step",
    "logging_pathlib_modules",
    "socket_select_modules",
    "file_import",
    "global_from_import",
    "package_import",
    "import_system_model",
    "vfs_file_io",
    "file_context_open",
    "file_io_compat",
    "filesystem_io_edges",
    "exceptions",
    "runtime_error_exceptions",
    "exception_unwind_with",
    "typed_exceptions",
    "exception_chaining_sys",
    "exception_self_context",
    "exception_instance_dict",
    "finally_blocks",
    "classes",
    "class_dynamic_attrs",
    "context_managers",
    "context_manager_exception_traceback",
    "with_assignment_targets",
    "with_manager_lifetime",
    "input_builtin",
    "exception_group_split",
    "builtin_methods",
    "trace_hooks",
    "trace_events",
    "trace_local_and_exception",
    "debug_trace_profile_edges",
    "sys_command_path",
    "sys_startup_config",
    "task_async",
    "async_syntax",
    "async_generator_await_exception_unwind",
    "async_await_expression_precedence",
    "async_generator_suppressed_cancellation",
    "threading_runtime_edges",
    "thread_target_lifetime",
    "signal_main_thread",
    "symlink_realpath",
    "asyncio_runtime_edges",
    "closures",
    "nonlocal_counter",
    "comprehension_multiple_filters",
    "csv_reader_dialect_keyword",
    "array_w_unicode",
    "regex_lookbehind_anchor_alternative",
    "ssl_timed_socket",
    "value_alias_assignment",
    "ctypes_structure_layout",
    "ctypes_pointer_return",
    "ctypes_cast_errcheck_array",
    "itertools_zip_longest_shared",
    "minmax_custom_key",
    "math_combinatorics",
    "try_loop_exception_scope",
    "native_module_function_binding",
    "classmethod_attr_int_compare"
)

foreach ($case in $cases) {
    $source = Join-Path $root "fixtures/core/$case.py"
    $expectedPath = Join-Path $root "fixtures/expected/$case.out"
    $expected = ((Get-Content -LiteralPath $expectedPath -Raw -Encoding UTF8) -replace "`r`n", "`n").TrimEnd()
    $actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
    $actual = $actual -replace [regex]::Escape($root), "tests"
    if ($env:OS -ne 'Windows_NT') {
        $expected = $expected.Replace('tests\fixtures\core\', 'tests/fixtures/core/')
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$case failed with exit code $LASTEXITCODE"
    }
    if ($actual -ne $expected) {
        throw "$case output mismatch. Expected '$expected', got '$actual'"
    }
    Write-Host "fixture $case ok"
}

$sectionCases = @(
    "module_and_statement_syntax",
    "function_and_class_syntax",
    "expression_syntax",
    "core_value_and_object_model",
    "functions_and_calls",
    "exceptions",
    "containers",
    "strings_and_unicode",
    "imports_and_modules",
    "builtins",
    "standard_modules",
    "system_stdlib"
)

foreach ($case in $sectionCases) {
    $source = Join-Path $root "fixtures/compat_sections/$case.py"
    $expectedPath = Join-Path $root "fixtures/expected/compat_sections/$case.out"
    $expected = ((Get-Content -LiteralPath $expectedPath -Raw -Encoding UTF8) -replace "`r`n", "`n").TrimEnd()
    if ($case -eq 'standard_modules' -and $env:OS -ne 'Windows_NT') {
        # These assertions exercise APIs that exist only on Windows.
        $expected = ($expected -split "`n" | Where-Object {
            $_ -notmatch '^(winapi-native |time-clock-info-windows |strftime-invalid |sys-windowsversion-|sys-noarg-keyword (getwindowsversion|_enablelegacywindowsfsencoding) |2147483649 131097 1 None$)'
        }) -join "`n"
    }
    $actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0) {
        throw "compat section $case failed with exit code $LASTEXITCODE"
    }
    if ($actual -ne $expected) {
        throw "compat section $case output mismatch. Expected '$expected', got '$actual'"
    }
    Write-Host "compat section $case ok"
}

$uncaughtSource = Join-Path $root "fixtures/core/uncaught_exception.py"
$oldErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$uncaughtOutput = ((& $XLang3 $uncaughtSource 2>&1 | Out-String) -replace "`r`n", "`n").TrimEnd()
$uncaughtExitCode = $LASTEXITCODE
$ErrorActionPreference = $oldErrorActionPreference
if ($uncaughtExitCode -ne 1) {
    throw "uncaught_exception expected exit code 1, got $uncaughtExitCode"
}
if ($uncaughtOutput -notlike "*Traceback (most recent call last):*" -or $uncaughtOutput -notlike "*RuntimeError: top*") {
    throw "uncaught_exception output mismatch. Got '$uncaughtOutput'"
}
Write-Host "fixture uncaught_exception ok"

$uncaughtRuntimeSource = Join-Path $root "fixtures/core/uncaught_runtime_error.py"
$oldErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$uncaughtRuntimeOutput = ((& $XLang3 $uncaughtRuntimeSource 2>&1 | Out-String) -replace "`r`n", "`n").TrimEnd()
$uncaughtRuntimeExitCode = $LASTEXITCODE
$ErrorActionPreference = $oldErrorActionPreference
if ($uncaughtRuntimeExitCode -ne 1) {
    throw "uncaught_runtime_error expected exit code 1, got $uncaughtRuntimeExitCode"
}
if ($uncaughtRuntimeOutput -notlike "*Traceback (most recent call last):*" -or $uncaughtRuntimeOutput -notlike "*ZeroDivisionError: division by zero*") {
    throw "uncaught_runtime_error output mismatch. Got '$uncaughtRuntimeOutput'"
}
Write-Host "fixture uncaught_runtime_error ok"

$unsetAttrSource = Join-Path $root "fixtures/core/unset_instance_attr.py"
$oldErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$unsetAttrOutput = ((& $XLang3 $unsetAttrSource 2>&1 | Out-String) -replace "`r`n", "`n").TrimEnd()
$unsetAttrExitCode = $LASTEXITCODE
$ErrorActionPreference = $oldErrorActionPreference
if ($unsetAttrExitCode -ne 1) {
    throw "unset_instance_attr expected exit code 1, got $unsetAttrExitCode"
}
if ($unsetAttrOutput -notlike "*Traceback (most recent call last):*" -or $unsetAttrOutput -notlike "*AttributeError: 'A' object has no attribute 'x'*") {
    throw "unset_instance_attr output mismatch. Got '$unsetAttrOutput'"
}
Write-Host "fixture unset_instance_attr ok"
