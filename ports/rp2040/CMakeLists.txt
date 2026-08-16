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
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ENV{PICO_SDK_PATH})
  message(FATAL_ERROR "PICO_SDK_PATH must point to the Raspberry Pi Pico SDK")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(xlang3_rp2040 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

pico_sdk_init()

set(XLANG3_EMBEDDED ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/xlang3)

add_executable(xlang3_pico
  main.cpp
  board/console.cpp
  board/gpio.cpp
  board/time.cpp
  embedded/embedded_host.cpp
  embedded/frozen_app.cpp
  embedded/static_modules.cpp
)

target_compile_definitions(xlang3_pico PRIVATE XLANG3_SIZE_OPT=1)

target_compile_options(xlang3_pico PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-Os;-ffunction-sections;-fdata-sections>
  $<$<COMPILE_LANGUAGE:CXX>:-Os;-ffunction-sections;-fdata-sections>
)

target_link_options(xlang3_pico PRIVATE
  -Wl,--gc-sections
)

target_link_libraries(xlang3_pico
  PRIVATE
    xlang3_runtime_embedded
    hardware_i2c
    pico_stdlib
)

target_include_directories(xlang3_pico
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../src/internal
)

pico_enable_stdio_usb(xlang3_pico 1)
pico_enable_stdio_uart(xlang3_pico 1)
pico_add_extra_outputs(xlang3_pico)
