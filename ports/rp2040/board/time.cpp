/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "board/time.h"

#include "pico/stdlib.h"

namespace xlang3::pico::board {
namespace {

ServiceCallback g_service_callback = nullptr;
void* g_service_context = nullptr;

} // namespace

void set_service_callback(ServiceCallback callback, void* context) {
  g_service_callback = callback;
  g_service_context = context;
}

void sleep_milliseconds(uint32_t milliseconds) {
  if (g_service_callback == nullptr || milliseconds == 0) {
    sleep_ms(milliseconds);
    return;
  }
  for (uint32_t i = 0; i < milliseconds; ++i) {
    g_service_callback(g_service_context);
    sleep_ms(1);
  }
}

} // namespace xlang3::pico::board
