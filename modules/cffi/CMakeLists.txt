# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_cffi_backend_native_package SHARED
  cffi_backend_module.cpp
)

if(MSVC AND CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
  enable_language(ASM_MASM)
  target_sources(xlang_cffi_backend_native_package PRIVATE
    libffi/ffi.c
    libffi/prep_cif.c
    libffi/types.c
    libffi/win64.asm
  )
  target_include_directories(xlang_cffi_backend_native_package PRIVATE libffi)
  target_compile_definitions(xlang_cffi_backend_native_package PRIVATE XLANG_CFFI_HAS_LIBFFI=1)
endif()

set_target_properties(xlang_cffi_backend_native_package PROPERTIES
  OUTPUT_NAME "_cffi_backend.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

target_include_directories(xlang_cffi_backend_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
)

if(WIN32)
  target_link_libraries(xlang_cffi_backend_native_package PRIVATE kernel32 ws2_32 ntdll)
endif()

if(MSVC)
  set_property(TARGET xlang_cffi_backend_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
