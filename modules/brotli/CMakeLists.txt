# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

set(XLANG_BROTLI_DIR "${PROJECT_SOURCE_DIR}/third_party/brotli-1.2.0/c")
file(GLOB XLANG_BROTLI_SOURCES CONFIGURE_DEPENDS
  "${XLANG_BROTLI_DIR}/common/*.c"
  "${XLANG_BROTLI_DIR}/dec/*.c"
  "${XLANG_BROTLI_DIR}/enc/*.c")
add_library(xlang_brotli_native_package SHARED
  brotli_module.cpp ${XLANG_BROTLI_SOURCES})
target_include_directories(xlang_brotli_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
  ${XLANG_BROTLI_DIR}/include)
target_compile_definitions(xlang_brotli_native_package PRIVATE
  _CRT_SECURE_NO_WARNINGS)
set_target_properties(xlang_brotli_native_package PROPERTIES
  OUTPUT_NAME "_brotli.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules")
if(MSVC)
  set_property(TARGET xlang_brotli_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
