# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_net_native_package SHARED
  main.cpp
  net_common.cpp
  http_module.cpp
  curl_module.cpp
  cypher_module.cpp
  smtp_module.cpp
)

set_target_properties(xlang_net_native_package PROPERTIES
  OUTPUT_NAME "xlang_net.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

target_include_directories(xlang_net_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
  ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
  target_compile_definitions(xlang_net_native_package PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
    CURL_STATICLIB
    XLANG_NET_WITH_CURL
  )
  target_include_directories(xlang_net_native_package PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/depends/windows/include
  )

  set(XLANG_NET_WIN_ARCH "x64")
  if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" XLANG_NET_WIN_PLATFORM)
  else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" XLANG_NET_WIN_PLATFORM)
  endif()
  if(XLANG_NET_WIN_PLATFORM MATCHES "arm64|aarch64")
    set(XLANG_NET_WIN_ARCH "arm64")
  endif()

  set(XLANG_NET_WIN_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/depends/windows/lib/${XLANG_NET_WIN_ARCH}")
  if(EXISTS "${XLANG_NET_WIN_LIB_DIR}/libcrypto_static.zip" AND NOT EXISTS "${XLANG_NET_WIN_LIB_DIR}/libcrypto_static.lib")
    file(ARCHIVE_EXTRACT
      INPUT "${XLANG_NET_WIN_LIB_DIR}/libcrypto_static.zip"
      DESTINATION "${XLANG_NET_WIN_LIB_DIR}"
    )
  endif()

  foreach(XLANG_NET_REQUIRED_LIB
      libcrypto_static.lib
      libssl_static.lib
      libcurl_static.lib
      zlib_static.lib)
    if(NOT EXISTS "${XLANG_NET_WIN_LIB_DIR}/${XLANG_NET_REQUIRED_LIB}")
      message(FATAL_ERROR "Missing ${XLANG_NET_REQUIRED_LIB} in ${XLANG_NET_WIN_LIB_DIR}")
    endif()
  endforeach()

  target_link_libraries(xlang_net_native_package PRIVATE
    "${XLANG_NET_WIN_LIB_DIR}/libcurl_static.lib"
    "${XLANG_NET_WIN_LIB_DIR}/libssl_static.lib"
    "${XLANG_NET_WIN_LIB_DIR}/libcrypto_static.lib"
    "${XLANG_NET_WIN_LIB_DIR}/zlib_static.lib"
    ws2_32
    crypt32
    wldap32
    normaliz
    bcrypt
  )
  # The bundled curl/OpenSSL/zlib archives use the static MSVC runtime.
  set_property(TARGET xlang_net_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
