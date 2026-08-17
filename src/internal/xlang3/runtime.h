/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once

#include "xlang3/value.h"
#include "xlang3/vfs.h"

#if !defined(XLANG3_EMBEDDED)
#include <filesystem>
#include <ostream>
#endif
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3 {

struct OutputSink {
  void* context = nullptr;
  void (*write)(void* context, const char* data, std::size_t size) = nullptr;
};

struct RawBlockContext {
  std::function<bool(const std::string& name, Value& out, std::string& error)> get_var;
  std::function<bool(const std::string& name, const Value& value, std::string& error)> set_var;
};

class Runtime {
public:
  using RawBlockHandler = bool (*)(
      Runtime& runtime,
      RawBlockContext& context,
      const std::string& language,
      const std::string& provider,
      const std::string& body,
      std::string& error);

#if !defined(XLANG3_EMBEDDED)
  explicit Runtime(std::ostream& out);
#endif
  explicit Runtime(OutputSink output);
  ~Runtime();

  void write_output(const char* data, std::size_t size);
  void write_output(const std::string& text) { write_output(text.data(), text.size()); }
  void write_output(const char* text);
  void write_output(char ch) { write_output(&ch, 1); }

  void register_builtin(std::string name, Value value);
  void register_native_builtin(std::string name, NativeFunctionCallback callback);
  const Value* find_builtin(const std::string& name) const;
  Value make_exception(std::string class_name, std::string message);
  Value make_exception_from_class(Value klass, std::string message);
  Value exception_type(const Value& exception);
  bool raise_class_error(std::string class_name, std::string message);
  void set_pending_exception(Value exception);
  bool take_pending_exception(Value& out);
  Value make_native_function(
      std::string name,
      NativeFunctionCallback callback,
      void* user_data = nullptr,
      void (*user_data_cleanup)(void*) = nullptr,
      NativeFastCallCallback fast_callback = nullptr,
      bool fast_releases_vm_lock = false);
  void register_module(std::string name, Value module);
  void unregister_module(const std::string& name);
  void register_native_package_cleanup(void* data, void (*cleanup)(void*));
  void register_raw_block_handler(std::string language, std::string provider, RawBlockHandler handler);
  bool execute_raw_block(
      RawBlockContext& context,
      const std::string& language,
      const std::string& provider,
      const std::string& body,
      std::string& error);
  bool import_module(const std::string& name, Value& out, std::string& error);
  bool import_from(const std::string& module_name, const std::string& attr_name, Value& out, std::string& error);
  Vfs& vfs() { return *vfs_; }
  const Vfs& vfs() const { return *vfs_; }
#if !defined(XLANG3_EMBEDDED)
  void add_import_root(std::filesystem::path root);
  void prepend_import_root(std::filesystem::path root);
  const std::vector<std::filesystem::path>& import_roots() const { return import_roots_; }
#endif
  void set_last_error(std::string error) { last_error_ = std::move(error); }
  const std::string& last_error() const { return last_error_; }

private:
  void initialize();

  OutputSink output_;
  std::unique_ptr<Vfs> vfs_;
  std::string last_error_;
  Value pending_exception_;
  uint32_t next_native_id_ = 1;
  std::unordered_map<std::string, Value> builtins_;
  std::unordered_map<std::string, Value> modules_;
  std::vector<std::pair<void*, void (*)(void*)>> native_package_cleanups_;
  std::unordered_map<std::string, RawBlockHandler> raw_block_handlers_;
#if !defined(XLANG3_EMBEDDED)
  std::vector<std::filesystem::path> import_roots_;
#endif
};

} // namespace xlang3
