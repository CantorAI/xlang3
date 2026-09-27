# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_psutil_windows_native_package SHARED psutil_windows_module.cpp)
target_include_directories(xlang_psutil_windows_native_package PRIVATE ${PROJECT_SOURCE_DIR}/sdk)
target_link_libraries(xlang_psutil_windows_native_package PRIVATE iphlpapi psapi ws2_32)
target_compile_definitions(xlang_psutil_windows_native_package PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(xlang_psutil_windows_native_package PROPERTIES
  OUTPUT_NAME "psutil._psutil_windows.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
