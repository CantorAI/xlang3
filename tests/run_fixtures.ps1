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

if (-not $XLang3) {
    throw "XLang3 executable path is required"
}

$root = $PSScriptRoot
$cases = @(
    "scalar_loop",
    "functions",
    "nested_function_no_closure",
    "if_else",
    "syntax_logical_lines",
    "docstring_suite_execution",
    "runtime_specialization_semantics",
    "syntax_simple_suites",
    "statement_syntax",
    "module_statement_partials",
    "structural_pattern_matching",
    "expression_operators",
    "chained_comparisons",
    "function_class_syntax",
    "future_annotations",
    "function_metadata",
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
    "stale_bytecode_import",
    "zlib_module",
    "zipfile_module",
    "zipimport_module",
    "weakref_module",
    "inspect_module",
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
    "asyncio_runtime_edges",
    "closures",
    "nonlocal_counter",
    "comprehension_multiple_filters"
)

foreach ($case in $cases) {
    $source = Join-Path $root "fixtures/core/$case.py"
    $expectedPath = Join-Path $root "fixtures/expected/$case.out"
    $expected = ((Get-Content -LiteralPath $expectedPath -Raw) -replace "`r`n", "`n").TrimEnd()
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
    $expected = ((Get-Content -LiteralPath $expectedPath -Raw) -replace "`r`n", "`n").TrimEnd()
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
