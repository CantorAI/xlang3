#include "xlang3/interpreter.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: xlang3 <file.py>\n";
    return 2;
  }

  std::ifstream file(argv[1], std::ios::binary);
  if (!file) {
    std::cerr << "cannot open " << argv[1] << "\n";
    return 2;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();

  auto parsed = xlang3::parse_source(buffer.str());
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) {
      std::cerr << "parse: " << error << "\n";
    }
    return 1;
  }

  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    for (const auto& error : lowered.errors) {
      std::cerr << "lower: " << error << "\n";
    }
    return 1;
  }

  xlang3::Interpreter interpreter(std::cout);
  auto result = interpreter.run(lowered.module);
  if (!result.errors.empty()) {
    for (const auto& error : result.errors) {
      std::cerr << "runtime: " << error << "\n";
    }
    return 1;
  }
  return 0;
}
