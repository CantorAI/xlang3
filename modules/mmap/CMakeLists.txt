# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_mmap_native_package SHARED mmap_module.cpp)
target_include_directories(xlang_mmap_native_package PRIVATE ${PROJECT_SOURCE_DIR}/sdk)
set_target_properties(xlang_mmap_native_package PROPERTIES
  OUTPUT_NAME "mmap.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules")
if(MSVC)
  set_property(TARGET xlang_mmap_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
