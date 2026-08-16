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
#include "pico/stdlib.h"

#include "xlang3/interpreter.h"
#include "xlang3/parser.h"
#include "xlang3/runtime.h"
#include "xlang3/sema.h"

#include <iostream>

namespace {

constexpr const char* kProgram = R"py(
x = 1 + 2
print(x)
)py";

int run_xlang3_program() {
  auto parsed = xlang3::parse_source(kProgram);
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) {
      std::cout << "parse: " << error << "\n";
    }
    return 1;
  }

  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    for (const auto& error : lowered.errors) {
      std::cout << "lower: " << error << "\n";
    }
    return 1;
  }

  xlang3::Runtime runtime(std::cout);
  xlang3::Interpreter interpreter(runtime);
  auto result = interpreter.run(lowered.module);
  if (!result.errors.empty()) {
    for (const auto& error : result.errors) {
      std::cout << "runtime: " << error << "\n";
    }
    return 1;
  }
  return 0;
}

} // namespace

int main() {
  stdio_init_all();
  sleep_ms(1500);
  std::cout << "xlang3 rp2040 start\n";
  const int status = run_xlang3_program();
  std::cout << "xlang3 rp2040 status=" << status << "\n";
  while (true) {
    tight_loop_contents();
  }
}
