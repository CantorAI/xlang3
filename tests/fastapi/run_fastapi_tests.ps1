param(
    [Parameter(Mandatory = $true)][string]$XLang3,
    [Parameter(Mandatory = $true)][string]$SitePackages,
    [string]$TestPackages
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $TestPackages) {
    $TestPackages = Join-Path $root 'scratch\fastapi-test-deps'
}
$previousPythonPath = $env:PYTHONPATH
try {
    $sitePythonPath = if ([string]::IsNullOrEmpty($previousPythonPath)) {
        $SitePackages
    } else {
        "$SitePackages;$previousPythonPath"
    }
    $env:PYTHONPATH = $sitePythonPath
    $runtimeName = ((& $XLang3 -c 'import sys; print(sys.implementation.name)' | Out-String) -replace "`r`n", "`n").Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "XLang3 runtime preflight failed with exit code $LASTEXITCODE"
    }
    if ($runtimeName -ne 'xlang3') {
        throw "FastAPI gates must run on XLang3, got '$runtimeName'"
    }
    foreach ($case in @('module_subclass_contract', 'importlib_spec_loader_contract', 'math_combinatorics_contract', 'math_real_type_error_contract', 'contextvar_generic_alias_contract', 'set_remove_key_error_contract', 'private_generator_name_contract', 'subprocess_cwd_contract', 'try_loop_exception_scope_contract', 'native_module_function_binding_contract', 'csv_writer_contract', 'csv_reader_contract', 'ssl_timed_socket_contract', 'value_alias_assignment_contract', 'ctypes_structure_layout_contract', 'ctypes_pointer_return_contract', 'itertools_zip_longest_shared_contract', 'minmax_custom_key_contract', 'ctypes_truststore_policy_contract', 'finalizer_warning_contract', 'gc_objects_contract', 'gc_generator_referrers_contract', 'generator_result_release_contract', 'delegated_exception_contract', 'set_hash_caching_contract', 'weakref_hash_cache_contract', 'except_target_cleanup_contract', 'comprehension_lifetime_contract', 'winapi_handle_contract', 'cffi_array_assignment_contract', 'random_state_contract', 'cryptography_asn1_contract', 'cryptography_oid_contract', 'cryptography_hash_contract', 'cryptography_hmac_contract', 'cryptography_rsa_contract', 'cryptography_ec_contract', 'cryptography_x509_reader_contract', 'cryptography_x509_documents_contract', 'cryptography_x509_revoked_contract', 'cryptography_x509_name_contract', 'cryptography_x509_extension_der_contract', 'trustme_tls_contract', 'zstandard_contract', 'brotli_contract', 'mmap_contract', 'psutil_windows_contract', 'cryptography_x509_store_contract', 'cryptography_x509_sct_contract', 'cryptography_x509_verification_contract', 'cryptography_x509_client_policy_contract', 'cryptography_x509_extension_policy_contract', 'cryptography_dh_parameters_contract', 'cryptography_dsa_parameters_contract', 'cryptography_ed25519_contract', 'cryptography_x25519_contract', 'cryptography_ciphers_contract', 'compound_union_serialization_contract', 'import_submodule_failure_contract', 'multipart_upload_contract', 'nested_union_retry_contract', 'bool_fraction_contract', 'polymorphic_serialization_contract', 'generic_class_frame_contract', 'argument_field_info_contract', 'decorator_warning_line_contract', 'before_assignment_contract', 'literal_enum_identity_contract', 'model_after_none_contract', 'root_validator_none_contract', 'extra_assignment_contract', 'float_identity_validation_contract', 'argument_alias_choices_contract', 'ascii_string_constraint_contract', 'instanceof_serialize_as_any_contract', 'base64_error_contract', 'none_json_mode_contract', 'bytesize_division_contract', 'decimal_constraint_error_contract', 'uuid_error_context_contract', 'strict_string_enum_contract', 'validator_iterator_repr_contract', 'invalid_constraint_type_contract', 'enum_missing_repr_contract', 'decimal_bool_validation_contract', 'missing_module_import_contract', 'list_length_error_contract', 'forward_union_serialization_contract', 'serializer_return_repr_contract', 'type_repr_serialization_contract', 'serialization_info_contract', 'json_roundtrip_serializer_contract', 'tuple_union_serialization_contract', 'tagged_union_missing_field_warning_contract', 'nested_union_fallback_contract', 'model_root_assignment_before_contract', 'some_generic_pattern_contract', 'nested_wrap_serializer_contract', 'subclass_extra_serialization_contract', 'tagged_union_fallback_contract', 'root_assignment_location_contract', 'private_int_default_contract', 'exec_missing_name_contract', 'ast_model_container_format_contract', 'union_pipeline_side_effect_contract', 'url_network_semantics_contract', 'url_blank_input_contract', 'model_class_signature_contract', 'missing_sentinel_contract', 'underscore_field_name_contract', 'type_subclasses_contract', 'model_class_keyword_contract', 'nested_filter_type_contract', 'model_extra_iteration_contract', 'model_dir_fields_contract', 'model_hash_rule_contract', 'frozen_cached_property_contract', 'model_dict_comparison_contract', 'validation_error_environment_contract', 'plain_serializer_schema_contract', 'generic_enum_schema_contract', 'literal_list_schema_contract', 'file_path_schema_contract', 'generic_native_container_contract', 'generic_model_revalidation_contract', 'gc_class_cycle_contract', 'generic_alias_cache_contract', 'recursive_alias_error_contract', 'exception_group_generic_contract', 'pattern_response_model_contract', 'deque_response_model_contract', 'generic_string_union_model_contract', 'generic_class_model_contract', 'generic_function_ast_contract', 'type_alias_forward_union_contract', 'invalid_forward_subscript_contract', 'type_alias_ast_contract', 'root_model_private_contract', 'type_alias_model_contract', 'metaclass_deprecation_contract', 'deprecated_model_assignment_contract', 'tagged_union_attribute_input_contract', 'model_attribute_doc_source_contract', 'model_dump_dict_view_contract', 'type_union_comprehension_contract', 'model_fields_container_repr_contract', 'nested_validator_error_location_contract', 'hashable_serialization_contract', 'pathlike_module_spec_contract', 'bytes_subclass_validation_contract', 'class_with_abstract_validator_contract', 'model_mapping_input_contract', 'pydantic_features', 'pydantic_common_types', 'pydantic_constraints', 'pydantic_serializers', 'pydantic_urls', 'pydantic_core_schemas', 'pydantic_recursive_definition_contract', 'pydantic_validators', 'pydantic_enums', 'pydantic_structures', 'pydantic_dataclasses', 'tagged_union_contract', 'typed_dict_contract', 'union_contract', 'pydantic_error_catalog', 'pydantic_errors_contract', 'pydantic_error_cause_contract', 'pydantic_date_datetime_contract', 'pydantic_tuple_serializer_contract', 'pydantic_union_serializer_contract', 'pydantic_any_override_contract', 'pydantic_control_error_contract', 'pydantic_isinstance_error_contract', 'pydantic_json_input_type_contract', 'pydantic_top_level_json_contract', 'pydantic_prebuilt_contract', 'pydantic_subclass_contract', 'pydantic_schema_type_contract', 'pydantic_generator_gc_contract', 'pydantic_validate_strings_contract', 'pydantic_tzinfo_contract', 'pydantic_dataclass_names_contract', 'ast_import_contract', 'traceback_multiline_contract', 'circular_import_contract', 'typing_union_contract', 'int_base_contract', 'proactor_handoff_contract', 'socket_family_contract', 'socket_nonblocking_contract', 'bytearray_mask_contract', 'bigint_frame_contract', 'nested_comprehension_contract', 'bytesio_export_contract', 'module_attribute_delete_contract', 'dual_stack_listener_contract', 'pipe_poll_contract', 'signal_enum_contract', 'signal_console_control_contract', 'context_mapping_contract', 'signal_worker_contract', 'symlink_containment_contract', 'path_symlink_contract', 'set_weakref_hash_contract', 'set_difference_contract', 'sync_endpoint_task_lifetime', 'nested_model_error_location', 'nested_model_serialization_options', 'nested_model_from_attributes', 'headers_clear_contract', 'graphql_route_contract', 'int_enum_arithmetic_contract', 'pydantic_url_control_contract', 'asgi_end_to_end', 'framework_features', 'advanced_asgi_features', 'testclient_features', 'ctypes_dll_call_contract', 'ctypes_array_assignment_contract', 'cffi_array_assignment_contract', 'windows_stdio_pipe_contract', 'module_binding_reassignment_contract', 'function_dir_attributes_contract', 'sigint_default_contract', 'runtime_protocol_contract', 'ctypes_known_folder_contract', 'temporary_closure_contract', 'class_keyword_unpack_contract', 'generic_metaclass_contract', 'starred_expression_precedence', 'pydantic_assignment_contract', 'future_annotation_source_contract', 'getattribute_getattr_fallback', 'rust_regex_contract', 'alias_location_contract', 'annotated_alias_contract', 'computed_exclude_contract', 'string_config_contract', 'namedtuple_argument_contract', 'invalid_model_config_contract', 'decimal_inf_config_contract', 'union_encoder_order_contract', 'model_construct_warning_contract', 'dataclass_positional_location_contract', 'dataclass_frozen_init_contract', 'dataclass_assignment_contract', 'args_kwargs_empty_contract', 'dataclass_existing_attr_contract', 'dataclass_keyword_only_contract', 'dataclass_validator_order_contract', 'dataclass_generic_specialization_contract', 'dataclass_missing_alias_contract', 'temporal_bigint_contract', 'time_separator_contract', 'timedelta_day_suffix_contract', 'timedelta_invalid_text_contract', 'deferred_nested_model_contract')) {
        $env:PYTHONPATH = if ($case -in @('cffi_array_assignment_contract', 'random_state_contract', 'cryptography_asn1_contract', 'cryptography_oid_contract', 'cryptography_hash_contract', 'cryptography_hmac_contract', 'cryptography_rsa_contract', 'cryptography_ec_contract', 'cryptography_x509_reader_contract', 'cryptography_x509_documents_contract', 'cryptography_x509_revoked_contract', 'cryptography_x509_name_contract', 'cryptography_x509_extension_der_contract', 'trustme_tls_contract', 'ctypes_truststore_policy_contract', 'zstandard_contract', 'brotli_contract', 'mmap_contract', 'psutil_windows_contract', 'cryptography_x509_store_contract', 'cryptography_x509_sct_contract', 'cryptography_x509_verification_contract', 'cryptography_x509_client_policy_contract', 'cryptography_x509_extension_policy_contract', 'cryptography_dh_parameters_contract', 'cryptography_dsa_parameters_contract', 'cryptography_ed25519_contract', 'cryptography_x25519_contract', 'cryptography_ciphers_contract', 'graphql_route_contract', 'multipart_upload_contract')) {
            "$TestPackages;$sitePythonPath"
        } else {
            $sitePythonPath
        }
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
    $websocketRunner = Join-Path $PSScriptRoot 'run_uvicorn_websocket_test.py'
    $websocketOutput = (& $python $websocketRunner $XLang3 $SitePackages $TestPackages | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "uvicorn_websocket_end_to_end failed with exit code $LASTEXITCODE"
    }
    if ($websocketOutput -ne 'fastapi-uvicorn-websocket-ok') {
        throw "uvicorn_websocket_end_to_end output mismatch: '$websocketOutput'"
    }
    Write-Host "fastapi test uvicorn_websocket_end_to_end ok"
} finally {
    $env:PYTHONPATH = $previousPythonPath
}
