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
  src/runtime/c_api_bridge.cpp
  src/runtime/exceptions.cpp
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

set(XLANG3_THREAD_RUNTIME_SOURCES
  src/runtime/modules/thread/runtime_lock.cpp
  src/runtime/modules/thread/thread_module.cpp
  src/runtime/modules/thread/threading_module.cpp
  src/runtime/modules/thread/thread_objects.cpp
)

set(XLANG3_TASK_RUNTIME_SOURCES
  src/runtime/modules/task/asyncio_module.cpp
  src/runtime/modules/task/task_module.cpp
  src/runtime/modules/task/task_objects.cpp
)

set(XLANG3_SYSTEM_RUNTIME_SOURCES
  src/runtime/modules/system/abc_module.cpp
  src/runtime/modules/system/ast_module.cpp
  src/runtime/modules/system/atexit_module.cpp
  src/runtime/modules/system/codecs_module.cpp
  src/runtime/modules/system/collections_module.cpp
  src/runtime/modules/system/errno_module.cpp
  src/runtime/modules/system/imp_module.cpp
  src/runtime/modules/system/importlib_module.cpp
  src/runtime/modules/system/itertools_module.cpp
  src/runtime/modules/system/io_module.cpp
  src/runtime/modules/system/marshal_module.cpp
  src/runtime/modules/system/opcode_module.cpp
  src/runtime/modules/system/os_module.cpp
  src/runtime/modules/system/pickle_module.cpp
  src/runtime/modules/system/queue_module.cpp
  src/runtime/modules/system/signal_module.cpp
  src/runtime/modules/system/socket_select_module.cpp
  src/runtime/modules/system/sre_module.cpp
  src/runtime/modules/system/source_encoding.cpp
  src/runtime/modules/system/stat_module.cpp
  src/runtime/modules/system/string_module.cpp
  src/runtime/modules/system/struct_module.cpp
  src/runtime/modules/system/sys_module.cpp
  src/runtime/modules/system/time_module.cpp
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
    src/import/import_loader.cpp
    src/import/native_package_loader.cpp
  )
endif()

if(XLANG3_EMBEDDED)
add_library(xlang3_runtime_embedded STATIC ${XLANG3_RUNTIME_SOURCES})
target_compile_definitions(xlang3_runtime_embedded PUBLIC XLANG3_EMBEDDED=1 XLANG3_SIZE_OPT=1)
target_include_directories(xlang3_runtime_embedded
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
target_link_libraries(xlang3_runtime PRIVATE xlang3_thread_runtime xlang3_task_runtime xlang3_system_runtime ZLIB::ZLIBSTATIC)
target_link_libraries(xlang3_runtime_static PRIVATE xlang3_thread_runtime xlang3_task_runtime xlang3_system_runtime ZLIB::ZLIBSTATIC)

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
add_xlang3_cpp_test(xlang3_runtime_value_tests tests/cpp/runtime_value_tests.cpp)
add_xlang3_cpp_test(xlang3_memory_tests tests/cpp/memory_tests.cpp)
add_xlang3_cpp_test(xlang3_parser_tests tests/cpp/parser_tests.cpp)
add_xlang3_cpp_test(xlang3_interpreter_tests tests/cpp/interpreter_tests.cpp)
add_xlang3_cpp_test(xlang3_debug_session_tests tests/cpp/debug_session_tests.cpp)
add_xlang3_cpp_test(xlang3_dap_session_tests tests/cpp/dap_session_tests.cpp)
add_xlang3_cpp_test(xlang3_module_slot_tests tests/cpp/module_slot_tests.cpp)
add_xlang3_cpp_test(xlang3_threading_tests tests/cpp/threading_tests.cpp)
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

add_executable(xlang3_sdk_c_header_smoke tests/sdk_c_header_smoke.c)
target_include_directories(xlang3_sdk_c_header_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME xlang3_sdk_c_header_smoke COMMAND xlang3_sdk_c_header_smoke)

add_test(
  NAME xlang3_cli_yaml_module_import
  COMMAND xlang3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/yaml_module.py
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
  NAME xlang3_cli_sqlite_module_imports
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/sqlite/run_sqlite_tests.ps1
          -XLang3 $<TARGET_FILE:xlang3>
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
  NAME xlang3_cli_dap_stdio_smoke
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/run_dap_stdio_smoke.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)
