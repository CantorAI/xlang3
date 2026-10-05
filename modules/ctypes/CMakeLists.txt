# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang3_ctypes_ffi STATIC ctypes_ffi.cpp)
target_include_directories(xlang3_ctypes_ffi PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/internal
  ${PROJECT_SOURCE_DIR}/sdk
)
target_compile_features(xlang3_ctypes_ffi PUBLIC cxx_std_17)
set_target_properties(xlang3_ctypes_ffi PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC AND (CMAKE_VS_PLATFORM_NAME STREQUAL "x64"
    OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"))
  enable_language(ASM_MASM)
  target_sources(xlang3_ctypes_ffi PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/cffi/libffi/ffi.c
    ${PROJECT_SOURCE_DIR}/modules/cffi/libffi/prep_cif.c
    ${PROJECT_SOURCE_DIR}/modules/cffi/libffi/types.c
    ${PROJECT_SOURCE_DIR}/modules/cffi/libffi/win64.asm
  )
  target_include_directories(xlang3_ctypes_ffi PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/cffi/libffi)
  target_compile_definitions(xlang3_ctypes_ffi PRIVATE XLANG_CTYPES_HAS_LIBFFI=1)
elseif(APPLE)
  find_path(XLANG3_CTYPES_LIBFFI_INCLUDE_DIR ffi.h
    HINTS "${CMAKE_OSX_SYSROOT}/usr/include/ffi" REQUIRED)
  find_library(XLANG3_CTYPES_LIBFFI_LIBRARY ffi
    HINTS "${CMAKE_OSX_SYSROOT}/usr/lib" REQUIRED)
  target_include_directories(xlang3_ctypes_ffi PRIVATE ${XLANG3_CTYPES_LIBFFI_INCLUDE_DIR})
  target_link_libraries(xlang3_ctypes_ffi PUBLIC ${XLANG3_CTYPES_LIBFFI_LIBRARY})
  target_compile_definitions(xlang3_ctypes_ffi PRIVATE XLANG_CTYPES_HAS_LIBFFI=1)
elseif(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(XLANG3_CTYPES_LIBFFI REQUIRED IMPORTED_TARGET libffi)
  target_link_libraries(xlang3_ctypes_ffi PUBLIC PkgConfig::XLANG3_CTYPES_LIBFFI)
  target_compile_definitions(xlang3_ctypes_ffi PRIVATE XLANG_CTYPES_HAS_LIBFFI=1)
endif()

if(WIN32)
  target_link_libraries(xlang3_ctypes_ffi PUBLIC kernel32)
else()
  target_link_libraries(xlang3_ctypes_ffi PUBLIC ${CMAKE_DL_LIBS})
endif()
