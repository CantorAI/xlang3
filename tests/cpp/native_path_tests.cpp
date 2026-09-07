#include "xlang3/xlang3.h"
#include <iostream>
#include <filesystem>
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
    auto stem = std::filesystem::absolute(argv[1]);
    stem.replace_extension();
    if (stem.extension() == ".x3pkg") stem.replace_extension();
#if !defined(_WIN32)
    auto name = stem.filename().string();
    if (name.rfind("lib", 0) == 0) stem = stem.parent_path() / name.substr(3);
#endif
    X::Runtime path_runtime;
    X::Module from_stem(path_runtime, "xlang1_compat_sample", stem.string().c_str());
    if (!from_stem["package_initializations"].Call({}, result) || result.ToLongLong() != 2)
      throw std::runtime_error("extensionless absolute library import failed: " + path_runtime.LastError());
    X::Module repeated(path_runtime, "xlang1_compat_sample", stem.string().c_str());
    if (!repeated["package_initializations"].Call({}, result) || result.ToLongLong() != 2)
      throw std::runtime_error("extensionless import initialized its package twice");
    std::cout << "native-library-absolute-stem-passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
