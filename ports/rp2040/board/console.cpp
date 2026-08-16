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
#include "board/console.h"

#include "pico/stdlib.h"

#include <cstdio>

namespace xlang3::pico::board {

void init_console() {
  stdio_init_all();
  sleep_ms(1500);
}

void write_line(const char* text) {
  std::printf("%s\n", text);
}

} // namespace xlang3::pico::board
