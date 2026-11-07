#include "xlang3/xlang3.h"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <chrono>

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("expected native library path");
#if defined(_WIN32)
    struct RestoreCurrentPath {
      std::filesystem::path original, temporary;
      ~RestoreCurrentPath() {
        std::error_code ignored;
        std::filesystem::current_path(original, ignored);
        std::filesystem::remove_all(temporary, ignored);
      }
    } restore{std::filesystem::current_path(), {}};
    std::wstring leaf = L"xlang3-runtime-path-";
    leaf.push_back(static_cast<wchar_t>(0x96ea));
    leaf += std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
    restore.temporary = std::filesystem::temp_directory_path() / leaf;
    std::filesystem::create_directories(restore.temporary);
    std::filesystem::current_path(restore.temporary);
#endif
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
    auto dotted_stem = stem.parent_path() / "." / stem.filename();
    X::Runtime dotted_runtime;
    for (int i = 0; i < 2; ++i) {
      X::Module dotted(dotted_runtime, "xlang1_compat_sample", dotted_stem.string().c_str());
      if (!dotted["package_initializations"].Call({}, result) || result.ToLongLong() != 3)
        throw std::runtime_error("dotted-directory native stem failed or initialized twice");
    }
    std::cout << "native-library-dotted-directory-stem-passed\n";
    {
      struct RestoreWorkingDirectory {
        std::filesystem::path original = std::filesystem::current_path();
        ~RestoreWorkingDirectory() {
          std::error_code ignored;
          std::filesystem::current_path(original, ignored);
        }
      } working_directory;
      std::filesystem::current_path(stem.parent_path());
      auto relative_stem = std::filesystem::path(".") / stem.filename();
      X::Runtime relative_runtime;
      for (int i = 0; i < 2; ++i) {
        X::Module relative(relative_runtime, "xlang1_compat_sample", relative_stem.string().c_str());
        if (!relative["package_initializations"].Call({}, result) || result.ToLongLong() != 4)
          throw std::runtime_error("relative native stem failed or initialized twice");
      }
    }
    std::cout << "native-library-relative-stem-passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
