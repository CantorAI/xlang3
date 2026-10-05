# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

find_program(XLANG3_PYDANTIC_CARGO NAMES cargo cargo.exe)
if(NOT XLANG3_PYDANTIC_CARGO)
  message(FATAL_ERROR "Cargo is required for pydantic-core's Rust regex engine")
endif()

set(PYDANTIC_REGEX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/rust_regex")
set(PYDANTIC_REGEX_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/regex-cargo-target")
set(XLANG3_RUST_TARGET "" CACHE STRING "Rust target triple for native package cross-builds")
set(PYDANTIC_REGEX_TARGET_ARGS)
set(PYDANTIC_REGEX_OUTPUT_DIR "${PYDANTIC_REGEX_TARGET_DIR}")
if(XLANG3_RUST_TARGET)
  list(APPEND PYDANTIC_REGEX_TARGET_ARGS --target "${XLANG3_RUST_TARGET}")
  string(APPEND PYDANTIC_REGEX_OUTPUT_DIR "/${XLANG3_RUST_TARGET}")
endif()
if(WIN32)
  set(PYDANTIC_REGEX_STATIC_LIB "${PYDANTIC_REGEX_OUTPUT_DIR}/release/xlang3_pydantic_regex_bridge.lib")
else()
  set(PYDANTIC_REGEX_STATIC_LIB "${PYDANTIC_REGEX_OUTPUT_DIR}/release/libxlang3_pydantic_regex_bridge.a")
endif()

set(PYDANTIC_REGEX_CARGO_ENV "CARGO_TARGET_DIR=${PYDANTIC_REGEX_TARGET_DIR}")
foreach(name CARGO_HOME RUSTUP_HOME RUSTUP_TOOLCHAIN)
  if(DEFINED XLANG3_PYDANTIC_${name})
    list(APPEND PYDANTIC_REGEX_CARGO_ENV "${name}=${XLANG3_PYDANTIC_${name}}")
  endif()
endforeach()

add_custom_command(
  OUTPUT "${PYDANTIC_REGEX_STATIC_LIB}"
  COMMAND ${CMAKE_COMMAND} -E env ${PYDANTIC_REGEX_CARGO_ENV}
    ${XLANG3_PYDANTIC_CARGO} build --release --locked ${PYDANTIC_REGEX_TARGET_ARGS}
    --manifest-path "${PYDANTIC_REGEX_ROOT}/Cargo.toml"
  DEPENDS "${PYDANTIC_REGEX_ROOT}/Cargo.toml"
          "${PYDANTIC_REGEX_ROOT}/Cargo.lock"
          "${PYDANTIC_REGEX_ROOT}/src/lib.rs"
  WORKING_DIRECTORY "${PYDANTIC_REGEX_ROOT}"
  VERBATIM
)
add_custom_target(xlang_pydantic_regex_bridge DEPENDS "${PYDANTIC_REGEX_STATIC_LIB}")

add_library(xlang_pydantic_core_native_package SHARED
  pydantic_core_module.cpp
)
add_dependencies(xlang_pydantic_core_native_package xlang_pydantic_regex_bridge)
target_link_libraries(xlang_pydantic_core_native_package PRIVATE "${PYDANTIC_REGEX_STATIC_LIB}")
if(WIN32)
  target_link_libraries(xlang_pydantic_core_native_package PRIVATE bcrypt ntdll userenv ws2_32)
else()
  target_link_libraries(xlang_pydantic_core_native_package PRIVATE ${CMAKE_DL_LIBS})
endif()

set_target_properties(xlang_pydantic_core_native_package PROPERTIES
  OUTPUT_NAME "pydantic_core._pydantic_core.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

target_include_directories(xlang_pydantic_core_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
)

if(MSVC)
  set_property(TARGET xlang_pydantic_core_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
