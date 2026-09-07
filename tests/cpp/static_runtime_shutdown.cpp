#include "xlang3/xlang3.h"
#include <iostream>
#include <memory>

struct GlobalHost {
  std::unique_ptr<X::Runtime> runtime;
  X::Value retained;
};
static GlobalHost host;

int main() {
  try {
    host.runtime = std::make_unique<X::Runtime>();
    X::Module builtins(*host.runtime, "builtins");
    auto globals = host.runtime->Dict();
    if (!builtins["eval"].Call({X::Value(*host.runtime,
        "[[], {}, {1}, (1, 2), type('Sample', (), {})()]"), globals}, host.retained)) {
      throw X::Error(host.runtime->LastError());
    }
    std::cout << "global runtime retained until process teardown\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
