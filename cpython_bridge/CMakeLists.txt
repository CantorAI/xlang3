find_package(Python3 3.14...<3.15 REQUIRED COMPONENTS Interpreter Development.Module Development.Embed)
set(bridge_sources engine.cpp conversion.cpp xlang_object.cpp python_object.cpp buffer.cpp protocols.cpp snapshot.cpp
  serialize/writer.cpp serialize/reader.cpp serialize/reduction.cpp)
Python3_add_library(xlang3_cpython MODULE WITH_SOABI
  main.cpp import.cpp ${bridge_sources})
execute_process(COMMAND ${Python3_EXECUTABLE} -c "import sys; print(sys.base_prefix)"
  OUTPUT_VARIABLE XLANG3_PYTHON_HOME OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
configure_file(python_config.h.in python_config.h @ONLY)
add_library(xlang_cpython_native_package SHARED
  native_module.cpp python_runtime.cpp ${bridge_sources})
target_include_directories(xlang_cpython_native_package PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(xlang_cpython_native_package PRIVATE xlang3_runtime Python3::Python ${CMAKE_DL_LIBS})
target_compile_definitions(xlang_cpython_native_package PRIVATE XLANG3_USE_SHARED)
set_target_properties(xlang_cpython_native_package PROPERTIES
  OUTPUT_NAME "xlang_cpython.x3pkg" PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules")
target_link_libraries(xlang3_cpython PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_cpython PRIVATE XLANG3_USE_SHARED)
set_target_properties(xlang3_cpython PROPERTIES
  OUTPUT_NAME xlang3
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/cpython_bridge"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/cpython_bridge")
# Keep the shared runtime next to the extension for CPython's DLL loader.
if(WIN32)
  add_custom_target(xlang_cpython_runtime
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:Python3::Python> $<TARGET_FILE_DIR:xlang3>
    COMMENT "Staging CPython for the XLang3 host")
  add_dependencies(xlang_cpython_native_package xlang_cpython_runtime)
  add_custom_target(xlang3_cpython_runtime
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:xlang3_cpython>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:xlang3_runtime> $<TARGET_FILE_DIR:xlang3_cpython>
    DEPENDS xlang3_runtime
    COMMENT "Staging the current XLang3 runtime for CPython")
  add_dependencies(xlang3_cpython xlang3_cpython_runtime)
endif()
add_test(NAME xlang3_cpython_import
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_import.py
    $<TARGET_FILE_DIR:xlang3_cpython> ${PROJECT_SOURCE_DIR}/tests/cpython_bridge
    $<TARGET_FILE_DIR:xlang_json_native_package>)
set_tests_properties(xlang3_cpython_import PROPERTIES TIMEOUT 60)
add_test(NAME xlang3_cpython_objects
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_python_objects.py
    $<TARGET_FILE_DIR:xlang3_cpython> ${PROJECT_SOURCE_DIR}/tests/cpython_bridge)
set_tests_properties(xlang3_cpython_objects PROPERTIES TIMEOUT 30)
add_test(NAME xlang3_cpython_lifetime
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_lifetime.py
    $<TARGET_FILE_DIR:xlang3_cpython> ${PROJECT_SOURCE_DIR}/tests/cpython_bridge)
set_tests_properties(xlang3_cpython_lifetime PROPERTIES TIMEOUT 30)
add_dependencies(xlang3_cpython xlang_json_native_package xlang_net_native_package xlang3 xlang_cpython_native_package)
add_test(NAME xlang3_cpython_ipc
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_ipc.py
    $<TARGET_FILE_DIR:xlang3_cpython> $<TARGET_FILE:xlang3>
    ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/ipc_server.py)
set_tests_properties(xlang3_cpython_ipc PROPERTIES TIMEOUT 60)
add_test(NAME xlang3_cpython_hosted
  COMMAND xlang3 ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_hosted.py
    $<TARGET_FILE_DIR:xlang_cpython_native_package> ${PROJECT_SOURCE_DIR}/tests/cpython_bridge)
set_tests_properties(xlang3_cpython_hosted PROPERTIES TIMEOUT 60)
add_executable(xlang3_cpython_hosted_lifetime ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/hosted_lifetime.cpp)
target_link_libraries(xlang3_cpython_hosted_lifetime PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_cpython_hosted_lifetime PRIVATE XLANG3_USE_SHARED)
add_dependencies(xlang3_cpython_hosted_lifetime xlang_cpython_native_package)
add_test(NAME xlang3_cpython_hosted_lifetime
  COMMAND xlang3_cpython_hosted_lifetime $<TARGET_FILE_DIR:xlang_cpython_native_package>
    ${PROJECT_SOURCE_DIR}/tests/cpython_bridge)
set_tests_properties(xlang3_cpython_hosted_lifetime PROPERTIES TIMEOUT 60)
add_test(NAME xlang3_cpython_buffers
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_buffers.py
    $<TARGET_FILE_DIR:xlang3_cpython> ${PROJECT_SOURCE_DIR}/tests/cpython_bridge)
set_tests_properties(xlang3_cpython_buffers PROPERTIES TIMEOUT 60)
add_test(NAME xlang3_cpython_protocols
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_protocols.py
    $<TARGET_FILE_DIR:xlang3_cpython> ${PROJECT_SOURCE_DIR}/tests/cpython_bridge)
add_test(NAME xlang3_cpython_snapshots
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_snapshots.py
    $<TARGET_FILE_DIR:xlang3_cpython> ${PROJECT_SOURCE_DIR}/tests/cpython_bridge
    $<TARGET_FILE_DIR:xlang_cpython_native_package>)
set_tests_properties(xlang3_cpython_protocols xlang3_cpython_snapshots PROPERTIES TIMEOUT 60)
add_test(NAME xlang3_cpython_reducers
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_reducers.py
    $<TARGET_FILE_DIR:xlang3_cpython> $<TARGET_FILE_DIR:xlang_cpython_native_package>)
set_tests_properties(xlang3_cpython_reducers PROPERTIES TIMEOUT 60)
add_test(NAME xlang3_cpython_classes
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/cpython_bridge/test_classes.py
    $<TARGET_FILE_DIR:xlang3_cpython>)
set_tests_properties(xlang3_cpython_classes PROPERTIES TIMEOUT 60)
