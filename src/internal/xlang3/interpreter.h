#pragma once

#include "xlang3/ir.h"
#include "xlang3/runtime.h"

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
  explicit Interpreter(Runtime& runtime);
  RuntimeResult run(const ir::Module& module);

private:
  RuntimeResult run_function(
      const ir::Module& module,
      uint32_t function_id,
      const std::vector<Value>& args,
      const std::vector<Value>& closure);

  Runtime& runtime_;
  std::unordered_map<std::string, Value> globals_;
};

} // namespace xlang3
