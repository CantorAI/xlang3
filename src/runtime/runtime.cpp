#include "xlang3/runtime.h"

namespace xlang3 {

Runtime::Runtime(std::ostream& out) : out_(out) {
  register_core_builtins();
}

void Runtime::register_builtin(std::string name, Value value) {
  builtins_[std::move(name)] = std::move(value);
}

void Runtime::register_native_builtin(std::string name, NativeFunctionCallback callback) {
  const uint32_t native_id = next_native_id_++;
  auto function_value = Value::native_function(native_id, name, callback);
  register_builtin(std::move(name), std::move(function_value));
}

const Value* Runtime::find_builtin(const std::string& name) const {
  auto it = builtins_.find(name);
  if (it == builtins_.end()) {
    return nullptr;
  }
  return &it->second;
}

void Runtime::register_core_builtins() {
  register_native_builtin("print", builtin_print);
}

bool builtin_print(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error) {
  (void)error;
  for (uint32_t i = 0; i < argc; ++i) {
    if (i != 0) {
      runtime.out() << " ";
    }
    runtime.out() << value_to_string(args[i]);
  }
  runtime.out() << "\n";
  out = Value::none();
  return true;
}

} // namespace xlang3
