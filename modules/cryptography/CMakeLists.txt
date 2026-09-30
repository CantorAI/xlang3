# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_cryptography_rust_native_package SHARED
  asn1_module.cpp
  exceptions_module.cpp
  ec_module.cpp
  dh_module.cpp
  dsa_module.cpp
  ed25519_module.cpp
  x25519_module.cpp
  ciphers_module.cpp
  hashes_module.cpp
  hmac_module.cpp
  legacy_openssl_module.cpp
  oid_module.cpp
  rsa_module.cpp
  x509_module.cpp
)

set_target_properties(xlang_cryptography_rust_native_package PROPERTIES
  OUTPUT_NAME "cryptography.hazmat.bindings._rust.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

target_include_directories(xlang_cryptography_rust_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
)

if(WIN32)
  set(XLANG_CRYPTO_WIN_ARCH "x64")
  if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" XLANG_CRYPTO_WIN_PLATFORM)
  else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" XLANG_CRYPTO_WIN_PLATFORM)
  endif()
  if(XLANG_CRYPTO_WIN_PLATFORM MATCHES "arm64|aarch64")
    set(XLANG_CRYPTO_WIN_ARCH "arm64")
  endif()
  set(XLANG_CRYPTO_WIN_ROOT "${PROJECT_SOURCE_DIR}/modules/net/depends/windows")
  target_compile_definitions(xlang_cryptography_rust_native_package PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(xlang_cryptography_rust_native_package PRIVATE
    "${XLANG_CRYPTO_WIN_ROOT}/include")
  target_link_libraries(xlang_cryptography_rust_native_package PRIVATE
    "${XLANG_CRYPTO_WIN_ROOT}/lib/${XLANG_CRYPTO_WIN_ARCH}/libcrypto_static.lib"
    ws2_32 crypt32 bcrypt)
  set_property(TARGET xlang_cryptography_rust_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(xlang_cryptography_rust_native_package PRIVATE OpenSSL::Crypto)
endif()
