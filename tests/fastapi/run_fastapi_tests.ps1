param(
    [Parameter(Mandatory = $true)][string]$XLang3,
    [Parameter(Mandatory = $true)][string]$SitePackages
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$previousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = if ([string]::IsNullOrEmpty($previousPythonPath)) {
        $SitePackages
    } else {
        "$SitePackages;$previousPythonPath"
    }
    $runtimeName = ((& $XLang3 -c 'import sys; print(sys.implementation.name)' | Out-String) -replace "`r`n", "`n").Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "XLang3 runtime preflight failed with exit code $LASTEXITCODE"
    }
    if ($runtimeName -ne 'xlang3') {
        throw "FastAPI gates must run on XLang3, got '$runtimeName'"
    }
    foreach ($case in @('pydantic_features', 'pydantic_common_types', 'pydantic_constraints', 'pydantic_serializers', 'pydantic_urls', 'pydantic_core_schemas', 'pydantic_validators', 'pydantic_enums', 'pydantic_structures', 'pydantic_dataclasses', 'tagged_union_contract', 'typed_dict_contract', 'union_contract', 'pydantic_error_catalog', 'pydantic_errors_contract', 'pydantic_error_cause_contract', 'pydantic_date_datetime_contract', 'pydantic_tuple_serializer_contract', 'pydantic_union_serializer_contract', 'pydantic_any_override_contract', 'pydantic_control_error_contract', 'pydantic_isinstance_error_contract', 'pydantic_json_input_type_contract', 'pydantic_top_level_json_contract', 'pydantic_prebuilt_contract', 'pydantic_subclass_contract', 'pydantic_schema_type_contract', 'pydantic_generator_gc_contract', 'pydantic_validate_strings_contract', 'pydantic_tzinfo_contract', 'pydantic_dataclass_names_contract', 'ast_import_contract', 'circular_import_contract', 'typing_union_contract', 'int_base_contract', 'proactor_handoff_contract', 'socket_family_contract', 'bytearray_mask_contract', 'bigint_frame_contract', 'nested_comprehension_contract', 'bytesio_export_contract', 'module_attribute_delete_contract', 'dual_stack_listener_contract', 'pipe_poll_contract', 'signal_enum_contract', 'int_enum_arithmetic_contract', 'pydantic_url_control_contract', 'asgi_end_to_end', 'framework_features', 'advanced_asgi_features', 'testclient_features')) {
        $source = Join-Path $PSScriptRoot "$case.py"
        $expectedPath = Join-Path $PSScriptRoot "$case.out"
        $expected = ((Get-Content -LiteralPath $expectedPath -Raw) -replace "`r`n", "`n").TrimEnd()
        $actual = ((& $XLang3 $source | Out-String) -replace "`r`n", "`n").TrimEnd()
        if ($LASTEXITCODE -ne 0) {
            throw "$case failed with exit code $LASTEXITCODE"
        }
        if ($actual -ne $expected) {
            throw "$case output mismatch. Expected '$expected', got '$actual'"
        }
        Write-Host "fastapi test $case ok"
    }
    $python = (Get-Command python -ErrorAction Stop).Source
    $uvicornRunner = Join-Path $PSScriptRoot 'run_uvicorn_test.py'
    $uvicornOutput = (& $python $uvicornRunner $XLang3 $SitePackages | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "uvicorn_end_to_end failed with exit code $LASTEXITCODE"
    }
    if ($uvicornOutput -ne 'fastapi-uvicorn-http-https-ok') {
        throw "uvicorn_end_to_end output mismatch: '$uvicornOutput'"
    }
    Write-Host "fastapi test uvicorn_end_to_end ok"
} finally {
    $env:PYTHONPATH = $previousPythonPath
}
