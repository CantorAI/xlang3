#include "xlang3/interpreter.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"

#include <iostream>
#include <sstream>

int main() {
  const char* source =
      "def add(a, b):\n"
      "    return a + b\n"
      "\n"
      "x = add(20, 22)\n";

  auto parsed = xlang3::parse_source(source);
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) std::cerr << error << "\n";
    return 1;
  }
  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    for (const auto& error : lowered.errors) std::cerr << error << "\n";
    return 1;
  }
  std::ostringstream out;
  xlang3::Interpreter interp(out);
  auto result = interp.run(lowered.module);
  if (!result.errors.empty()) {
    for (const auto& error : result.errors) std::cerr << error << "\n";
    return 1;
  }
  return 0;
}
