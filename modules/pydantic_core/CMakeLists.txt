# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_pydantic_core_native_package SHARED
  pydantic_core_module.cpp
)

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
