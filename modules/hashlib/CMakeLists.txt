# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_hashlib_native_package SHARED hashlib_module.cpp)
add_library(xlang_blake2_native_package SHARED blake2_module.cpp)

set_target_properties(xlang_hashlib_native_package PROPERTIES
  OUTPUT_NAME "xlang__hashlib.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules")

set_target_properties(xlang_blake2_native_package PROPERTIES
  OUTPUT_NAME "xlang__blake2.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules")

target_include_directories(xlang_hashlib_native_package PRIVATE ${PROJECT_SOURCE_DIR}/sdk)
target_include_directories(xlang_blake2_native_package PRIVATE ${PROJECT_SOURCE_DIR}/sdk)

if(WIN32)
  target_compile_definitions(xlang_hashlib_native_package PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(xlang_hashlib_native_package PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/net/depends/windows/include)
  set(XLANG_HASHLIB_WIN_ARCH "x64")
  if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" XLANG_HASHLIB_WIN_PLATFORM)
  else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" XLANG_HASHLIB_WIN_PLATFORM)
  endif()
  if(XLANG_HASHLIB_WIN_PLATFORM MATCHES "arm64|aarch64")
    set(XLANG_HASHLIB_WIN_ARCH "arm64")
  endif()
  set(XLANG_HASHLIB_WIN_LIB_DIR
    "${PROJECT_SOURCE_DIR}/modules/net/depends/windows/lib/${XLANG_HASHLIB_WIN_ARCH}")
  target_link_libraries(xlang_hashlib_native_package PRIVATE
    "${XLANG_HASHLIB_WIN_LIB_DIR}/libcrypto_static.lib" ws2_32 crypt32 bcrypt)
  set_property(TARGET xlang_hashlib_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_property(TARGET xlang_blake2_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(xlang_hashlib_native_package PRIVATE OpenSSL::Crypto)
endif()
