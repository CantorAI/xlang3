#pragma once

#include "xlang3/interpreter.h"
#include "xlang3/parser.h"
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
  Interpreter interp(out);
  auto runtime = interp.run(lowered.module);
  if (!runtime.errors.empty()) {
    result.ok = false;
    result.errors.insert(result.errors.end(), runtime.errors.begin(), runtime.errors.end());
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
