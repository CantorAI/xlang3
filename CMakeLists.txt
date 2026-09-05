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
cmake_minimum_required(VERSION 3.20)

project(xlang3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XLANG3_EMBEDDED "Build the static embedded runtime subset" OFF)

if(NOT XLANG3_EMBEDDED)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/yaml-cpp EXCLUDE_FROM_ALL)
add_subdirectory(third_party/zlib EXCLUDE_FROM_ALL)
add_subdirectory(modules/json)
add_subdirectory(modules/sqlite)
add_subdirectory(modules/yaml)
add_subdirectory(modules/device)
add_subdirectory(modules/net)
endif()

set(XLANG3_RUNTIME_SOURCES
  src/builtins/builtins.cpp
  src/builtins/builtin_modules.cpp
  src/builtins/exception_builtins.cpp
  src/builtins/functional_builtins.cpp
  src/builtins/io_builtins.cpp
  src/builtins/math_module.cpp
  src/builtins/object_type_builtins.cpp
  src/builtins/raw_block_builtins.cpp
  src/builtins/sequence_builtins.cpp
  src/ipc/ipc_runtime.cpp
  src/ipc/shared_memory_block_stream.cpp
  src/ipc/shared_memory_transport.cpp
  src/serialize/block_stream.cpp
  src/serialize/ipc_value_marshal.cpp
  src/serialize/xlang_stream.cpp
  src/runtime/c_api_bridge.cpp
  src/runtime/exceptions.cpp
  src/runtime/expression.cpp
  src/runtime/functional_iterators.cpp
  src/runtime/generator.cpp
  src/runtime/attribute.cpp
  src/runtime/methods/list_methods.cpp
  src/runtime/methods/dict_methods.cpp
  src/runtime/methods/file_methods.cpp
  src/runtime/methods/int_methods.cpp
  src/runtime/methods/set_methods.cpp
  src/runtime/methods/string_methods.cpp
  src/runtime/methods/tuple_methods.cpp
  src/runtime/methods/bytes_methods.cpp
  src/runtime/methods/iterator_methods.cpp
  src/runtime/methods/property_methods.cpp
  src/runtime/memory/x3_bucket_allocator.cpp
  src/runtime/memory/x3_memory.cpp
  src/runtime/memory/x3_object_pool.cpp
  src/runtime/memory/x3_page_allocator.cpp
  src/runtime/memory/x3_runtime_memory.cpp
  src/runtime/memory/x3_temp_arena.cpp
  src/runtime/perf_counters.cpp
  src/runtime/bigint.cpp
  src/runtime/value.cpp
  src/runtime/value_hash.cpp
  src/runtime/object_model.cpp
  src/runtime/sequence.cpp
  src/runtime/mapping.cpp
  src/runtime/set_object.cpp
  src/runtime/module_object.cpp
  src/runtime/module_slots.cpp
  src/runtime/runtime.cpp
  src/runtime/vfs.cpp
  src/parser/source_cursor.cpp
  src/parser/lexer.cpp
  src/parser/parser.cpp
  src/sema/scope_analysis.cpp
  src/sema/xlang_module_globals.cpp
  src/sema/lower.cpp
  src/ir/codec.cpp
  src/executor/xlang_vm/xlang_interpreter.cpp
  src/executor/xlang_vm/xlang_vm_attr.cpp
  src/executor/xlang_vm/xlang_vm_loop.cpp
  src/rpc/object_table.cpp
  src/rpc/memory_file_store.cpp
)

if(WIN32)
  list(APPEND XLANG3_RUNTIME_SOURCES
    src/ipc/shared_memory_transport_win32.cpp
  )
else()
  list(APPEND XLANG3_RUNTIME_SOURCES
    src/ipc/shared_memory_transport_posix.cpp
  )
endif()

set(XLANG3_THREAD_RUNTIME_SOURCES
  src/runtime/modules/thread/runtime_lock.cpp
  src/runtime/modules/thread/thread_module.cpp
  src/runtime/modules/thread/thread_objects.cpp
)

set(XLANG3_TASK_RUNTIME_SOURCES
  src/runtime/modules/task/task_module.cpp
  src/runtime/modules/task/task_objects.cpp
)

set(XLANG3_SYSTEM_RUNTIME_SOURCES
  src/runtime/modules/system/abc_module.cpp
  src/runtime/modules/system/ast_module.cpp
  src/runtime/modules/system/atexit_module.cpp
  src/runtime/modules/system/binascii_module.cpp
  src/runtime/modules/system/codecs_module.cpp
  src/runtime/modules/system/collections_module.cpp
  src/runtime/modules/system/contextvars_module.cpp
  src/runtime/modules/system/errno_module.cpp
  src/runtime/modules/system/imp_module.cpp
  src/runtime/modules/system/importlib_module.cpp
  src/runtime/modules/system/itertools_module.cpp
  src/runtime/modules/system/io_module.cpp
  src/runtime/modules/system/marshal_module.cpp
  src/runtime/modules/system/msvcrt_module.cpp
  src/runtime/modules/system/opcode_module.cpp
  src/runtime/modules/system/operator_module.cpp
  src/runtime/modules/system/os_module.cpp
  src/runtime/modules/system/overlapped_module.cpp
  src/runtime/modules/system/pickle_module.cpp
  src/runtime/modules/system/pyexpat_module.cpp
  src/runtime/modules/system/queue_module.cpp
  src/runtime/modules/system/random_module.cpp
  src/runtime/modules/system/signal_module.cpp
  src/runtime/modules/system/socket_select_module.cpp
  src/runtime/modules/system/sre_module.cpp
  src/runtime/modules/system/source_encoding.cpp
  src/runtime/modules/system/stat_module.cpp
  src/runtime/modules/system/string_module.cpp
  src/runtime/modules/system/struct_module.cpp
  src/runtime/modules/system/sysconfig_native_module.cpp
  src/runtime/modules/system/sys_module.cpp
  src/runtime/modules/system/time_module.cpp
  src/runtime/modules/system/tokenize_module.cpp
  src/runtime/modules/system/typing_module.cpp
  src/runtime/modules/system/unicodedata_module.cpp
  src/runtime/modules/system/weakref_module.cpp
  src/runtime/modules/system/warnings_module.cpp
  src/runtime/modules/system/winapi_module.cpp
  src/runtime/modules/system/winreg_module.cpp
  src/runtime/modules/system/zlib_module.cpp
  src/runtime/modules/system/zip_archive.cpp
  src/runtime/modules/system/zipimport_module.cpp
)

if(NOT XLANG3_EMBEDDED)
  list(APPEND XLANG3_RUNTIME_SOURCES
    src/debug/debug_session.cpp
    src/debug/dap_session.cpp
    src/ir/dump.cpp
    src/api/c_api.cpp
    src/api/stream_api.cpp
    src/api/native_serialization_api.cpp
    src/api/ipc_api.cpp
    src/serialize/value_graph_writer.cpp
    src/serialize/value_graph_reader.cpp
    src/serialize/value_graph_lifetime.cpp
    src/import/import_loader.cpp
    src/import/native_package_loader.cpp
  )
endif()

if(XLANG3_EMBEDDED)
add_library(xlang3_runtime_embedded STATIC ${XLANG3_RUNTIME_SOURCES})
target_compile_definitions(xlang3_runtime_embedded PUBLIC XLANG3_EMBEDDED=1 XLANG3_SIZE_OPT=1)
target_include_directories(xlang3_runtime_embedded
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
)
target_compile_features(xlang3_runtime_embedded PUBLIC cxx_std_17)
return()
endif()

add_library(xlang3_runtime SHARED ${XLANG3_RUNTIME_SOURCES})
add_library(xlang3_runtime_static STATIC ${XLANG3_RUNTIME_SOURCES})
add_library(xlang3_thread_runtime STATIC ${XLANG3_THREAD_RUNTIME_SOURCES})
add_library(xlang3_task_runtime STATIC ${XLANG3_TASK_RUNTIME_SOURCES})
add_library(xlang3_system_runtime STATIC ${XLANG3_SYSTEM_RUNTIME_SOURCES})

find_package(Threads REQUIRED)

set_target_properties(xlang3_runtime PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

set_target_properties(xlang3_runtime_static PROPERTIES
  OUTPUT_NAME "xlang3_runtime_static"
)

target_compile_definitions(xlang3_runtime PRIVATE XLANG3_BUILD_SHARED)

foreach(xlang3_runtime_target xlang3_runtime xlang3_runtime_static xlang3_thread_runtime xlang3_task_runtime xlang3_system_runtime)
target_include_directories(${xlang3_runtime_target}
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/modules/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/modules/task
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/modules/system
)

target_compile_features(${xlang3_runtime_target} PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(${xlang3_runtime_target} PRIVATE /MP)
endif()
endforeach()

target_include_directories(xlang3_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json)
target_include_directories(xlang3_runtime_static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json)
target_include_directories(xlang3_system_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json)

target_link_libraries(xlang3_system_runtime PUBLIC ZLIB::ZLIBSTATIC)
target_link_libraries(xlang3_runtime PRIVATE xlang3_thread_runtime xlang3_task_runtime xlang3_system_runtime ZLIB::ZLIBSTATIC Threads::Threads)
target_link_libraries(xlang3_runtime_static PRIVATE xlang3_thread_runtime xlang3_task_runtime xlang3_system_runtime ZLIB::ZLIBSTATIC Threads::Threads)

if(WIN32)
  target_link_libraries(xlang3_runtime PRIVATE ws2_32)
  target_link_libraries(xlang3_runtime_static PRIVATE ws2_32)
endif()

add_executable(xlang3 src/xlang3.cpp)
target_link_libraries(xlang3 PRIVATE xlang3_runtime)
target_compile_definitions(xlang3 PRIVATE XLANG3_USE_SHARED)
target_include_directories(xlang3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/internal)

if(WIN32)
  add_custom_command(TARGET xlang3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:xlang3>
            $<TARGET_FILE_DIR:xlang3>/python.exe
    COMMENT "Copying xlang3.exe to python.exe for Python-compatible tooling"
  )
endif()

enable_testing()
add_executable(xlang3_cpp_ipc_client tests/cpp/ipc_client.cpp)
target_link_libraries(xlang3_cpp_ipc_client PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_cpp_ipc_client PRIVATE XLANG3_USE_SHARED)

function(add_xlang3_cpp_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE xlang3_runtime)
  target_compile_definitions(${name} PRIVATE XLANG3_USE_SHARED)
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json
  )
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_xlang3_cpp_test(xlang3_smoke tests/smoke.cpp)
add_executable(xlang3_embedded_package_tests tests/cpp/embedded_package_tests.cpp)
target_link_libraries(xlang3_embedded_package_tests PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_embedded_package_tests PRIVATE XLANG3_USE_SHARED)
add_test(NAME xlang3_embedded_package_tests COMMAND xlang3_embedded_package_tests)
add_executable(xlang3_sdk_stream_call_tests tests/cpp/sdk_stream_call_tests.cpp)
target_link_libraries(xlang3_sdk_stream_call_tests PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_sdk_stream_call_tests PRIVATE XLANG3_USE_SHARED)
add_test(NAME xlang3_sdk_stream_call_tests COMMAND xlang3_sdk_stream_call_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp)
add_executable(xlang3_graph_process_tests tests/serialize/graph_process_tests.cpp)
add_library(xlang3_graph_native_package SHARED tests/serialize/native_package.cpp)
target_include_directories(xlang3_graph_native_package PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
set_target_properties(xlang3_graph_native_package PROPERTIES
  OUTPUT_NAME "xlang_graph_native.x3pkg" PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules")
add_dependencies(xlang3_graph_process_tests xlang3_graph_native_package)
target_link_libraries(xlang3_graph_process_tests PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_graph_process_tests PRIVATE XLANG3_USE_SHARED)
add_test(NAME xlang3_graph_producer COMMAND xlang3_graph_process_tests write ${CMAKE_CURRENT_BINARY_DIR}/graph_payload.bin ${CMAKE_CURRENT_SOURCE_DIR}/tests/serialize/graph_source.py $<TARGET_FILE_DIR:xlang3_graph_native_package>)
set_tests_properties(xlang3_graph_producer PROPERTIES FIXTURES_SETUP serialized_graph)
add_test(NAME xlang3_graph_consumer COMMAND xlang3_graph_process_tests read ${CMAKE_CURRENT_BINARY_DIR}/graph_payload.bin ${CMAKE_CURRENT_SOURCE_DIR}/tests/serialize/graph_source.py $<TARGET_FILE_DIR:xlang3_graph_native_package>)
set_tests_properties(xlang3_graph_consumer PROPERTIES FIXTURES_REQUIRED serialized_graph)
foreach(failure IN ITEMS BAD_VERSION NO_CODEC DECODE_FAIL)
  add_test(NAME xlang3_graph_reject_${failure} COMMAND xlang3_graph_process_tests reject ${CMAKE_CURRENT_BINARY_DIR}/graph_payload.bin ${CMAKE_CURRENT_SOURCE_DIR}/tests/serialize/graph_source.py $<TARGET_FILE_DIR:xlang3_graph_native_package>)
  set_tests_properties(xlang3_graph_reject_${failure} PROPERTIES FIXTURES_REQUIRED serialized_graph ENVIRONMENT "XLANG3_TEST_GRAPH_${failure}=1")
endforeach()
add_xlang3_cpp_test(xlang3_runtime_value_tests tests/cpp/runtime_value_tests.cpp)
add_xlang3_cpp_test(xlang3_ipc_capacity_tests tests/cpp/ipc_capacity_tests.cpp)
add_xlang3_cpp_test(xlang3_memory_tests tests/cpp/memory_tests.cpp)
add_xlang3_cpp_test(xlang3_parser_tests tests/cpp/parser_tests.cpp)
add_xlang3_cpp_test(xlang3_interpreter_tests tests/cpp/interpreter_tests.cpp)
add_xlang3_cpp_test(xlang3_debug_session_tests tests/cpp/debug_session_tests.cpp)
add_xlang3_cpp_test(xlang3_dap_session_tests tests/cpp/dap_session_tests.cpp)
add_xlang3_cpp_test(xlang3_module_slot_tests tests/cpp/module_slot_tests.cpp)
add_xlang3_cpp_test(xlang3_task_tests tests/cpp/task_tests.cpp)
add_xlang3_cpp_test(xlang3_ir_codec_tests tests/cpp/ir_codec_tests.cpp)

add_executable(xlang3_native_package_real_tests tests/cpp/native_package_real_tests.cpp)
target_link_libraries(xlang3_native_package_real_tests PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_native_package_real_tests PRIVATE XLANG3_USE_SHARED)
target_include_directories(xlang3_native_package_real_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp
)
add_test(
  NAME xlang3_native_package_real_tests
  COMMAND xlang3_native_package_real_tests $<TARGET_FILE_DIR:xlang_json_native_package>
)

add_library(xlang1_compat_sample_package SHARED
  tests/cpp/xlang1_compat_package.cpp
)
set_target_properties(xlang1_compat_sample_package PROPERTIES
  OUTPUT_NAME "xlang1_compat_sample.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)
target_include_directories(xlang1_compat_sample_package PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/sdk
)
if(NOT PROJECT_IS_TOP_LEVEL)
  add_custom_command(TARGET xlang1_compat_sample_package POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:xlang3>/modules"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:xlang1_compat_sample_package>"
      "$<TARGET_FILE_DIR:xlang3>/modules/$<TARGET_FILE_NAME:xlang1_compat_sample_package>")
endif()

add_library(xlang3_curmodule_probe_package SHARED
  tests/cpp/curmodule_probe_package.cpp
)
set_target_properties(xlang3_curmodule_probe_package PROPERTIES
  OUTPUT_NAME "xlang_curmodule_probe.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)
target_include_directories(xlang3_curmodule_probe_package PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/sdk
)
if(NOT PROJECT_IS_TOP_LEVEL)
  add_custom_command(TARGET xlang3_curmodule_probe_package POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:xlang3>/modules"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:xlang3_curmodule_probe_package>"
      "$<TARGET_FILE_DIR:xlang3>/modules/$<TARGET_FILE_NAME:xlang3_curmodule_probe_package>")
endif()

add_executable(xlang3_xlang1_compat_package_tests tests/cpp/xlang1_compat_package_tests.cpp)
target_link_libraries(xlang3_xlang1_compat_package_tests PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_xlang1_compat_package_tests PRIVATE XLANG3_USE_SHARED)
target_include_directories(xlang3_xlang1_compat_package_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/sdk
)
add_dependencies(xlang3_xlang1_compat_package_tests xlang1_compat_sample_package)
add_test(NAME xlang3_expression_decorators
  COMMAND xlang3 -c "exec(open('${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/expression_decorators.py').read())")
set_tests_properties(xlang3_expression_decorators PROPERTIES
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:xlang1_compat_sample_package>")
if(WIN32)
  add_test(NAME xlang3_expression_decorators_ipc
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/ipc/run_lrpc_smoke.ps1
      -XLang3 $<TARGET_FILE:xlang3>
      -NativeModules $<TARGET_FILE_DIR:xlang1_compat_sample_package>
      -ClientScript expression_client.py -Expected expression_expected.out)
endif()
add_test(
  NAME xlang3_xlang1_compat_package_tests
  COMMAND xlang3_xlang1_compat_package_tests $<TARGET_FILE_DIR:xlang1_compat_sample_package>
)

add_executable(xlang3_sdk_c_header_smoke tests/sdk_c_header_smoke.c)
target_include_directories(xlang3_sdk_c_header_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
add_test(NAME xlang3_sdk_c_header_smoke COMMAND xlang3_sdk_c_header_smoke)

add_test(
  NAME xlang3_cli_yaml_module_import
  COMMAND xlang3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/yaml_module.py $<TARGET_FILE_DIR:xlang_yaml_native_package>
)

add_test(
  NAME xlang3_cli_device_module_import
  COMMAND xlang3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/device_module.py
)

add_test(
  NAME xlang3_cli_real_native_imports
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/run_real_native_import_tests.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)

add_test(
  NAME xlang3_curmodule_probe_import
  COMMAND xlang3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/curmodule_probe_import.py
)

add_test(
  NAME xlang3_xlang1_compat_import
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/run_xlang1_compat_import.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)

add_test(
  NAME xlang3_cli_net_module_import
  COMMAND xlang3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/net/module.py $<TARGET_FILE_DIR:xlang_net_native_package>
)

add_test(
  NAME xlang3_cli_net_module_server_client
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/net/run_module.ps1
          -XLang3 $<TARGET_FILE:xlang3>
          -NativeModules $<TARGET_FILE_DIR:xlang_net_native_package>
)

add_test(
  NAME xlang3_cli_sqlite_module_imports
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/sqlite/run_sqlite_tests.ps1
          -XLang3 $<TARGET_FILE:xlang3>
          -NativeModules $<TARGET_FILE_DIR:xlang_sqlite3_native_package>
)

add_test(
  NAME xlang3_cli_fixtures
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fixtures.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)

add_test(
  NAME xlang3_cli_compat
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/run_cli_compat.ps1
          -XLang3 $<TARGET_FILE:xlang3>
          -WorkDir ${CMAKE_CURRENT_BINARY_DIR}/cli_compat
)

add_test(
  NAME xlang3_cli_pickle_interop
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/run_pickle_interop.ps1
          -XLang3 $<TARGET_FILE:xlang3>
          -WorkDir ${CMAKE_CURRENT_BINARY_DIR}/pickle_interop
)

add_test(
  NAME xlang3_ipc_lrpc_smoke
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/ipc/run_lrpc_smoke.ps1
          -XLang3 $<TARGET_FILE:xlang3>
          -NativeClient $<TARGET_FILE:xlang3_cpp_ipc_client>
)

add_test(
  NAME xlang3_ipc_lrpc_server_exit
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/ipc/run_lrpc_server_exit.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)

add_test(
  NAME xlang3_ipc_lrpc_backpressure
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/ipc/run_lrpc_smoke.ps1
          -XLang3 $<TARGET_FILE:xlang3>
          -ClientScript pressure_client.py
          -Expected pressure_expected.out
)
set_tests_properties(xlang3_ipc_lrpc_backpressure PROPERTIES TIMEOUT 120)
add_test(
  NAME xlang3_ipc_lrpc_backpressure_server_exit
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/ipc/run_pressure_exit.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)
set_tests_properties(xlang3_ipc_lrpc_backpressure_server_exit PROPERTIES TIMEOUT 45)

add_test(
  NAME xlang3_cli_dap_stdio_smoke
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/run_dap_stdio_smoke.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)
