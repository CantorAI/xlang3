#include "xlang3/builtins.h"

namespace xlang3 {

namespace {

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

} // namespace

void register_io_builtins(Runtime& runtime) {
  runtime.register_native_builtin("print", builtin_print);
}

} // namespace xlang3
