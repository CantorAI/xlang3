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
    "statement_syntax",
    "expression_operators",
    "chained_comparisons",
    "function_class_syntax",
    "object_type_model",
    "object_attribute_hooks",
    "descriptor_protocol",
    "code_traceback_model",
    "mro_model",
    "property_descriptor",
    "chained_object_methods",
    "builtin_alias",
    "builtin_function_batch",
    "dynamic_execution_builtins",
    "iterator_protocol",
    "tuples",
    "tuple_methods",
    "dict_views",
    "slices",
    "raw_strings",
    "string_compat",
    "binary_buffers",
    "starred_expressions",
    "dict_set_comprehensions",
    "nested_comprehensions",
    "generator_expressions",
    "walrus_operator",
    "unpacking",
    "annotated_assignment",
    "augmented_assignment",
    "lists_for",
    "sequences_index",
    "dict_set",
    "raw_blocks",
    "native_import",
    "json_module",
    "math_module",
    "time_module",
    "atexit_module",
    "io_os_modules",
    "imp_stat_modules",
    "collections_queue_modules",
    "collections_queue_facades",
    "types_module",
    "traceback_module",
    "runpy_module",
    "importlib_module",
    "weakref_module",
    "file_import",
    "global_from_import",
    "package_import",
    "import_system_model",
    "vfs_file_io",
    "file_context_open",
    "exceptions",
    "runtime_error_exceptions",
    "exception_unwind_with",
    "typed_exceptions",
    "exception_chaining_sys",
    "finally_blocks",
    "classes",
    "class_dynamic_attrs",
    "context_managers",
    "builtin_methods",
    "threading_module",
    "task_async",
    "asyncio_module",
    "async_syntax",
    "closures",
    "nonlocal_counter"
)

foreach ($case in $cases) {
    $source = Join-Path $root "fixtures/core/$case.py"
    $expectedPath = Join-Path $root "fixtures/expected/$case.out"
    $expected = ((Get-Content -LiteralPath $expectedPath -Raw) -replace "`r`n", "`n").TrimEnd()
    $actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
    if ($LASTEXITCODE -ne 0) {
        throw "$case failed with exit code $LASTEXITCODE"
    }
    if ($actual -ne $expected) {
        throw "$case output mismatch. Expected '$expected', got '$actual'"
    }
    Write-Host "fixture $case ok"
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
if ($uncaughtOutput -notlike "*runtime: uncaught exception: top*") {
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
if ($uncaughtRuntimeOutput -notlike "*runtime: uncaught exception: division by zero*") {
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
if ($unsetAttrOutput -notlike "*runtime: uncaught exception: object has no attribute*") {
    throw "unset_instance_attr output mismatch. Got '$unsetAttrOutput'"
}
Write-Host "fixture unset_instance_attr ok"
