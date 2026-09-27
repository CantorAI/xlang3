# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

set(XLANG_ZSTANDARD_LIB_DIR "${PROJECT_SOURCE_DIR}/third_party/zstd-1.5.7/lib")
file(GLOB XLANG_ZSTANDARD_SOURCES CONFIGURE_DEPENDS
  "${XLANG_ZSTANDARD_LIB_DIR}/common/*.c"
  "${XLANG_ZSTANDARD_LIB_DIR}/compress/*.c"
  "${XLANG_ZSTANDARD_LIB_DIR}/decompress/*.c")

add_library(xlang_zstandard_native_package SHARED
  zstandard_module.cpp
  ${XLANG_ZSTANDARD_SOURCES})
target_compile_definitions(xlang_zstandard_native_package PRIVATE
  ZSTD_DISABLE_ASM
  _CRT_SECURE_NO_WARNINGS)
target_include_directories(xlang_zstandard_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
  ${XLANG_ZSTANDARD_LIB_DIR})
set_target_properties(xlang_zstandard_native_package PROPERTIES
  OUTPUT_NAME "zstandard.backend_c.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules")
if(MSVC)
  set_property(TARGET xlang_zstandard_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
