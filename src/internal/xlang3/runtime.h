#pragma once

#include "xlang3/value.h"

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

class Runtime {
public:
  explicit Runtime(std::ostream& out);

  std::ostream& out() { return out_; }

  void register_builtin(std::string name, Value value);
  void register_native_builtin(std::string name, NativeFunctionCallback callback);
  const Value* find_builtin(const std::string& name) const;

private:
  std::ostream& out_;
  uint32_t next_native_id_ = 1;
  std::unordered_map<std::string, Value> builtins_;
};

} // namespace xlang3
