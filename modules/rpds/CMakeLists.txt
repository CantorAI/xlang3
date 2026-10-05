# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

find_program(XLANG3_RPDS_CARGO NAMES cargo cargo.exe)
if(NOT XLANG3_RPDS_CARGO)
  message(STATUS "rpds native package disabled: Cargo was not found")
  return()
endif()

set(RPDS_RUST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/rust")
set(RPDS_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")
set(XLANG3_RUST_TARGET "" CACHE STRING "Rust target triple for native package cross-builds")
set(RPDS_CARGO_TARGET_ARGS)
set(RPDS_CARGO_OUTPUT_DIR "${RPDS_CARGO_TARGET_DIR}")
if(XLANG3_RUST_TARGET)
  list(APPEND RPDS_CARGO_TARGET_ARGS --target "${XLANG3_RUST_TARGET}")
  string(APPEND RPDS_CARGO_OUTPUT_DIR "/${XLANG3_RUST_TARGET}")
endif()
if(WIN32)
  set(RPDS_STATIC_LIB "${RPDS_CARGO_OUTPUT_DIR}/release/xlang3_rpds_bridge.lib")
else()
  set(RPDS_STATIC_LIB "${RPDS_CARGO_OUTPUT_DIR}/release/libxlang3_rpds_bridge.a")
endif()

add_custom_command(
  OUTPUT "${RPDS_STATIC_LIB}"
  COMMAND ${CMAKE_COMMAND} -E env
    "CARGO_TARGET_DIR=${RPDS_CARGO_TARGET_DIR}"
    ${XLANG3_RPDS_CARGO} build --release --locked ${RPDS_CARGO_TARGET_ARGS}
    --manifest-path "${RPDS_RUST_ROOT}/Cargo.toml"
  DEPENDS "${RPDS_RUST_ROOT}/Cargo.toml"
          "${RPDS_RUST_ROOT}/Cargo.lock"
          "${RPDS_RUST_ROOT}/src/lib.rs"
  WORKING_DIRECTORY "${RPDS_RUST_ROOT}"
  VERBATIM
)
add_custom_target(xlang_rpds_rust_bridge DEPENDS "${RPDS_STATIC_LIB}")

add_library(xlang_rpds_native_package SHARED rpds_module.cpp)
add_dependencies(xlang_rpds_native_package xlang_rpds_rust_bridge)
target_include_directories(xlang_rpds_native_package PRIVATE ${PROJECT_SOURCE_DIR}/sdk)
target_link_libraries(xlang_rpds_native_package PRIVATE "${RPDS_STATIC_LIB}")
if(WIN32)
  target_link_libraries(xlang_rpds_native_package PRIVATE bcrypt ntdll userenv ws2_32)
else()
  target_link_libraries(xlang_rpds_native_package PRIVATE ${CMAKE_DL_LIBS})
endif()
set_target_properties(xlang_rpds_native_package PROPERTIES
  OUTPUT_NAME "rpds.rpds.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)
if(MSVC)
  set_property(TARGET xlang_rpds_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
