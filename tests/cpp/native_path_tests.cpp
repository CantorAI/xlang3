#include "xlang3/xlang3.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("expected native library path");
    X::Runtime runtime;
    X::Module builtins(runtime, "builtins");
    X::Value callable, result;
    auto globals = runtime.Dict();
    if (!builtins["eval"].Call({X::Value(runtime, "lambda: len([])"), globals}, callable))
      throw std::runtime_error(runtime.LastError());
    auto config = runtime.Dict();
    if (config["missing"].IsValid()) throw std::runtime_error("missing key unexpectedly found");
    if (!callable.Call({}, result) || result.ToLongLong() != 0)
      throw std::runtime_error("handled missing key poisoned the next call: " + runtime.LastError());
    for (int i = 0; i < 2; ++i) {
      X::Module module(runtime, "xlang1_compat_sample", argv[1]);
      X::Value count;
      if (!module["package_initializations"].Call({}, count) || count.ToLongLong() != 1)
        throw std::runtime_error("explicit library import initialized its package more than once");
    }
    std::cout << "native-library-path-single-initialization-passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
