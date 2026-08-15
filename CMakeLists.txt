cmake_minimum_required(VERSION 3.20)

project(xlang3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xlang3_core
  src/builtins/builtins.cpp
  src/builtins/io_builtins.cpp
  src/runtime/value.cpp
  src/runtime/runtime.cpp
  src/parser/lexer.cpp
  src/parser/parser.cpp
  src/sema/lower.cpp
  src/ir/dump.cpp
  src/executor/interpreter.cpp
)

target_include_directories(xlang3_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
)

target_compile_features(xlang3_core PUBLIC cxx_std_17)

add_executable(xlang3 src/xlang3.cpp)
target_link_libraries(xlang3 PRIVATE xlang3_core)
target_include_directories(xlang3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/internal)

enable_testing()

function(add_xlang3_cpp_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE xlang3_core)
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp
  )
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_xlang3_cpp_test(xlang3_smoke tests/smoke.cpp)
add_xlang3_cpp_test(xlang3_runtime_value_tests tests/cpp/runtime_value_tests.cpp)
add_xlang3_cpp_test(xlang3_parser_tests tests/cpp/parser_tests.cpp)
add_xlang3_cpp_test(xlang3_interpreter_tests tests/cpp/interpreter_tests.cpp)

add_executable(xlang3_sdk_c_header_smoke tests/sdk_c_header_smoke.c)
target_include_directories(xlang3_sdk_c_header_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME xlang3_sdk_c_header_smoke COMMAND xlang3_sdk_c_header_smoke)

add_test(
  NAME xlang3_cli_fixtures
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fixtures.ps1
          -XLang3 $<TARGET_FILE:xlang3>
)
