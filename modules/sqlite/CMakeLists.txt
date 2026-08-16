# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(xlang_sqlite3_native_package SHARED
  sqlite_package.cpp
  sqlite_values.cpp
  sqlite_handles.cpp
  ${PROJECT_SOURCE_DIR}/third_party/sqlite/sqlite3.c
)

set_target_properties(xlang_sqlite3_native_package PROPERTIES
  OUTPUT_NAME "xlang_sqlite3.x3pkg"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/modules"
)

target_include_directories(xlang_sqlite3_native_package PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/third_party/sqlite
)

target_compile_definitions(xlang_sqlite3_native_package PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_OMIT_LOAD_EXTENSION=1
)
