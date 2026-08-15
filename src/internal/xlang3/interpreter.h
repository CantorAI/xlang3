#pragma once

#include "xlang3/ir.h"

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

struct RuntimeResult {
  Value value;
  std::vector<std::string> errors;
};

class Interpreter {
public:
  explicit Interpreter(std::ostream& out);
  RuntimeResult run(const ir::Module& module);

private:
  RuntimeResult run_function(const ir::Module& module, uint32_t function_id, const std::vector<Value>& args);

  std::ostream& out_;
  std::unordered_map<std::string, Value> globals_;
};

} // namespace xlang3
