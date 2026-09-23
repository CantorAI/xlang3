# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_argon2_cffi_native_package SHARED
  argon2_cffi_module.cpp
  ${PROJECT_SOURCE_DIR}/third_party/argon2/src/argon2.c
  ${PROJECT_SOURCE_DIR}/third_party/argon2/src/blake2/blake2b.c
  ${PROJECT_SOURCE_DIR}/third_party/argon2/src/core.c
  ${PROJECT_SOURCE_DIR}/third_party/argon2/src/encoding.c
  ${PROJECT_SOURCE_DIR}/third_party/argon2/src/ref.c
  ${PROJECT_SOURCE_DIR}/third_party/argon2/src/thread.c
)

set_target_properties(xlang_argon2_cffi_native_package PROPERTIES
  OUTPUT_NAME "_argon2_cffi_bindings._ffi.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

target_include_directories(xlang_argon2_cffi_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
  ${PROJECT_SOURCE_DIR}/third_party/argon2/include
  ${PROJECT_SOURCE_DIR}/third_party/argon2/src
)

if(MSVC)
  set_property(TARGET xlang_argon2_cffi_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
