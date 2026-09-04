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

#include "xlang3/cpp/value.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace X {

class Runtime {
public:
  Runtime() : runtime_(x3_runtime_create()) {
    if (runtime_ == nullptr) {
      throw std::runtime_error("failed to create XLang3 runtime");
    }
    init_host();
  }

  explicit Runtime(X3Runtime* runtime) : runtime_(runtime), owns_(false) {
    init_host();
  }

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  Runtime(Runtime&& other) noexcept : runtime_(other.runtime_), owns_(other.owns_) {
    host_ = other.host_;
    host_.runtime = runtime_;
    other.runtime_ = nullptr;
    other.owns_ = false;
    other.host_ = {};
  }

  Runtime& operator=(Runtime&& other) noexcept {
    if (this != &other) {
      reset();
      runtime_ = other.runtime_;
      owns_ = other.owns_;
      host_ = other.host_;
      host_.runtime = runtime_;
      other.runtime_ = nullptr;
      other.owns_ = false;
      other.host_ = {};
    }
    return *this;
  }

  ~Runtime() { reset(); }

  X3Runtime* get() const { return runtime_; }
  X3PackageHost* host() { return &host_; }
  const X3PackageHost* host() const { return &host_; }

  Value List();
  Value Dict();

  void AddImportRoot(const std::string& path) {
    check(x3_runtime_add_import_root(runtime_, path.c_str()));
  }

  std::string LastError() const {
    const char* error = x3_runtime_last_error(runtime_);
    return error == nullptr ? std::string() : std::string(error);
  }

  void check(X3Status status) const {
    if (status != X3_STATUS_OK) {
      throw std::runtime_error(LastError());
    }
  }

private:
  void init_host() {
    host_ = {};
    host_.abi_version = X3_ABI_VERSION;
    host_.size = sizeof(X3PackageHost);
    host_.runtime = runtime_;
    host_.runtime_last_error = x3_runtime_last_error;
    host_.value_retain = x3_value_retain;
    host_.value_release = x3_value_release;
    host_.value_string = x3_value_string;
    host_.value_bytes = x3_value_bytes;
    host_.value_list = x3_value_list;
    host_.value_dict = x3_value_dict;
    host_.value_to_cstr = x3_value_to_cstr;
    host_.value_object_kind = x3_value_object_kind;
    host_.value_bytes_data = x3_value_bytes_data;
    host_.value_to_bytes = x3_value_to_bytes;
    host_.value_from_bytes = x3_value_from_bytes;
    host_.value_binary_op = x3_value_binary_op;
    host_.value_compare_op = x3_value_compare_op;
    host_.event_create = x3_event_create;
    host_.event_subscribe = x3_event_subscribe;
    host_.event_unsubscribe = x3_event_unsubscribe;
    host_.event_fire = x3_event_fire;
    host_.call = x3_call;
    host_.len = x3_len;
    host_.get_attr = x3_get_attr;
    host_.get_item = x3_get_item;
    host_.set_attr = x3_set_attr;
    host_.list_append = x3_list_append;
    host_.dict_set_item = x3_dict_set_item;
    host_.dict_get_entry = x3_dict_get_entry;
  }

  void reset() {
    if (owns_ && runtime_ != nullptr) {
      x3_runtime_destroy(runtime_);
    }
    runtime_ = nullptr;
  }

  X3Runtime* runtime_ = nullptr;
  X3PackageHost host_{};
  bool owns_ = true;
};

inline Value::Value(Runtime& runtime, X3Value value) : Value(runtime.host(), value, false) {}
inline Value::Value(Runtime& runtime, int64_t value) : Value(runtime.host(), x3_value_int64(value), false) {}
inline Value::Value(Runtime& runtime, int value) : Value(runtime, static_cast<int64_t>(value)) {}
inline Value::Value(Runtime& runtime, double value) : Value(runtime.host(), x3_value_double(value), false) {}
inline Value::Value(Runtime& runtime, bool value) : Value(runtime.host(), x3_value_bool(value ? 1 : 0), false) {}
inline Value::Value(Runtime& runtime, const char* value)
    : Value(runtime.host(), runtime.host()->value_string(runtime.get(), value == nullptr ? "" : value), false) {}
inline Value::Value(Runtime& runtime, const std::string& value) : Value(runtime, value.c_str()) {}

inline Value Runtime::List() {
  return Value::List(&host_);
}

inline Value Runtime::Dict() {
  return Value::Dict(&host_);
}

class Function : public Value {
public:
  Function() = default;
  Function(X3PackageHost* host, X3Value value) : Value(host, value, false) {}
  Function(const Value& value) : Value(value) {}
  Function(Value&& value) noexcept : Value(std::move(value)) {}
};

class Module : public Value {
public:
  using Value::operator=;

  Module(Runtime& runtime, const char* module_name, const char* package_name = nullptr)
      : Value(runtime.host(), x3_value_invalid(), false) {
    X3Value result = x3_value_invalid();
    runtime.check(x3_runtime_import_module(runtime.get(), package_name, module_name, &result));
    static_cast<Value&>(*this) = Value(runtime.host(), result, false);
  }

  Function fn(const char* name) const {
    return Function((*this)[name]);
  }
};

} // namespace X

#include "xlang3/cpp/package.h"
