cmake_minimum_required(VERSION 3.20)

project(xlang3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xlang3_core
  src/runtime/value.cpp
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

add_executable(xlang3_smoke tests/smoke.cpp)
target_link_libraries(xlang3_smoke PRIVATE xlang3_core)
target_include_directories(xlang3_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/internal)
add_test(NAME xlang3_smoke COMMAND xlang3_smoke)

add_executable(xlang3_sdk_c_header_smoke tests/sdk_c_header_smoke.c)
target_include_directories(xlang3_sdk_c_header_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME xlang3_sdk_c_header_smoke COMMAND xlang3_sdk_c_header_smoke)
