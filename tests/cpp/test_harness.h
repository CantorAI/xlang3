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
#pragma once

#include "xlang3/interpreter.h"
#include "xlang3/parser.h"
#include "xlang3/runtime.h"
#include "xlang3/sema.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace xlang3::test {

struct CaseResult {
  bool ok = true;
  std::vector<std::string> errors;
};

inline void expect_true(CaseResult& result, bool condition, const std::string& message) {
  if (!condition) {
    result.ok = false;
    result.errors.push_back(message);
  }
}

inline CaseResult run_source(const std::string& source, std::string& output) {
  CaseResult result;
  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    result.ok = false;
    result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());
    return result;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    result.ok = false;
    result.errors.insert(result.errors.end(), lowered.errors.begin(), lowered.errors.end());
    return result;
  }
  std::ostringstream out;
  Runtime runtime(out);
  Interpreter interp(runtime);
  auto run_result = interp.run(lowered.module);
  if (!run_result.errors.empty()) {
    result.ok = false;
    result.errors.insert(result.errors.end(), run_result.errors.begin(), run_result.errors.end());
    return result;
  }
  output = out.str();
  return result;
}

inline int finish(const CaseResult& result) {
  if (result.ok) {
    return 0;
  }
  for (const auto& error : result.errors) {
    std::cerr << error << "\n";
  }
  return 1;
}

} // namespace xlang3::test
