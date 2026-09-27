# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

add_library(xlang_httptools_url_native_package SHARED
  url_parser_module.cpp
  vendor/http-parser/http_parser.c
)

add_library(xlang_httptools_parser_native_package SHARED
  parser_module.cpp
  vendor/llhttp/src/api.c
  vendor/llhttp/src/http.c
  vendor/llhttp/src/llhttp.c
)

set_target_properties(xlang_httptools_url_native_package PROPERTIES
  OUTPUT_NAME "httptools.parser.url_parser.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

set_target_properties(xlang_httptools_parser_native_package PROPERTIES
  OUTPUT_NAME "httptools.parser.parser.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

target_include_directories(xlang_httptools_url_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/http-parser
)

target_include_directories(xlang_httptools_parser_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/sdk
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/llhttp/include
)

if(MSVC)
  set_property(TARGET xlang_httptools_url_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_property(TARGET xlang_httptools_parser_native_package PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
