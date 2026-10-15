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
    // Initialize exception TLS before main returns. The global runtime must still
    // be able to clear it after the main thread's TLS destructors have run.
    X::Value result;
    if (!builtins["exec"].Call({X::Value(*host.runtime,
        "try:\n"
        "    raise RuntimeError('static runtime shutdown')\n"
        "except RuntimeError:\n"
        "    pass\n"), globals}, result)) {
      throw X::Error(host.runtime->LastError());
    }
    std::cout << "global runtime retained until process teardown\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
