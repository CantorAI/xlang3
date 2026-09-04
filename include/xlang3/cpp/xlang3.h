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

#include "xlang3/xapi.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace X {

class Value;

class Runtime {
public:
  Runtime() : runtime_(x3_runtime_create()) {
    if (runtime_ == nullptr) {
      throw std::runtime_error("failed to create XLang3 runtime");
    }
  }

  explicit Runtime(X3Runtime* runtime) : runtime_(runtime), owns_(false) {}

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  Runtime(Runtime&& other) noexcept : runtime_(other.runtime_), owns_(other.owns_) {
    other.runtime_ = nullptr;
    other.owns_ = false;
  }

  Runtime& operator=(Runtime&& other) noexcept {
    if (this != &other) {
      reset();
      runtime_ = other.runtime_;
      owns_ = other.owns_;
      other.runtime_ = nullptr;
      other.owns_ = false;
    }
    return *this;
  }

  ~Runtime() { reset(); }

  X3Runtime* get() const { return runtime_; }

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
  void reset() {
    if (owns_ && runtime_ != nullptr) {
      x3_runtime_destroy(runtime_);
    }
    runtime_ = nullptr;
  }

  X3Runtime* runtime_ = nullptr;
  bool owns_ = true;
};

class Value {
public:
  Value() : runtime_(nullptr), value_(x3_value_invalid()) {}
  Value(Runtime& runtime, X3Value value) : runtime_(runtime.get()), value_(value) {}
  Value(X3Runtime* runtime, X3Value value) : runtime_(runtime), value_(value) {}
  Value(Runtime& runtime, int64_t value) : runtime_(runtime.get()), value_(x3_value_int64(value)) {}
  Value(Runtime& runtime, int value) : Value(runtime, static_cast<int64_t>(value)) {}
  Value(Runtime& runtime, double value) : runtime_(runtime.get()), value_(x3_value_double(value)) {}
  Value(Runtime& runtime, bool value) : runtime_(runtime.get()), value_(x3_value_bool(value ? 1 : 0)) {}
  Value(Runtime& runtime, const char* value) : runtime_(runtime.get()), value_(x3_value_string(runtime.get(), value)) {}
  Value(Runtime& runtime, const std::string& value) : Value(runtime, value.c_str()) {}

  Value(const Value& other) : runtime_(other.runtime_), value_(other.value_) {
    x3_value_retain(value_);
  }

  Value(Value&& other) noexcept : runtime_(other.runtime_), value_(other.value_) {
    other.runtime_ = nullptr;
    other.value_ = x3_value_invalid();
  }

  Value& operator=(const Value& other) {
    if (this != &other) {
      x3_value_release(value_);
      runtime_ = other.runtime_;
      value_ = other.value_;
      x3_value_retain(value_);
    }
    return *this;
  }

  Value& operator=(Value&& other) noexcept {
    if (this != &other) {
      x3_value_release(value_);
      runtime_ = other.runtime_;
      value_ = other.value_;
      other.runtime_ = nullptr;
      other.value_ = x3_value_invalid();
    }
    return *this;
  }

  ~Value() { x3_value_release(value_); }

  X3Value raw() const { return value_; }
  X3Runtime* runtime() const { return runtime_; }

  bool IsValid() const { return value_.tag != X3_TAG_INVALID; }
  bool IsInt64() const { return value_.tag == X3_TAG_INT64; }
  int64_t ToInt64() const { return value_.as.i64; }

  uint64_t Len() const {
    uint64_t result = 0;
    check(x3_len(runtime_, value_, &result));
    return result;
  }

  std::string ToString() const {
    const char* text = x3_value_to_cstr(runtime_, value_);
    return text == nullptr ? std::string() : std::string(text);
  }

  Value operator[](const char* name) const {
    X3Value result = x3_value_invalid();
    check(x3_get_attr(runtime_, value_, name, &result));
    return Value(runtime_, result);
  }

  Value operator[](const std::string& name) const {
    return (*this)[name.c_str()];
  }

  Value operator[](int index) const {
    return GetItem(static_cast<int64_t>(index));
  }

  Value operator[](int64_t index) const {
    return GetItem(index);
  }

  Value GetItem(const Value& key) const {
    X3Value result = x3_value_invalid();
    check(x3_get_item(runtime_, value_, key.raw(), &result));
    return Value(runtime_, result);
  }

  template <typename T>
  Value GetItem(T&& key) const {
    Value owned_key(runtime_, to_value(std::forward<T>(key)));
    return GetItem(static_cast<const Value&>(owned_key));
  }

  void Append(const Value& item) {
    check(x3_list_append(runtime_, value_, item.raw()));
  }

  template <typename T>
  void Append(T&& item) {
    Value owned_item(runtime_, to_value(std::forward<T>(item)));
    Append(static_cast<const Value&>(owned_item));
  }

  void SetItem(const Value& key, const Value& item) {
    check(x3_dict_set_item(runtime_, value_, key.raw(), item.raw()));
  }

  template <typename Key, typename Item>
  void SetItem(Key&& key, Item&& item) {
    Value owned_key(runtime_, to_value(std::forward<Key>(key)));
    Value owned_item(runtime_, to_value(std::forward<Item>(item)));
    SetItem(static_cast<const Value&>(owned_key), static_cast<const Value&>(owned_item));
  }

  bool SetPropValue(const char* name, const Value& value) {
    return x3_set_attr(runtime_, value_, name, value.raw()) == X3_STATUS_OK;
  }

  template <typename... Args>
  Value operator()(Args&&... args) const {
    std::vector<Value> owned_args;
    owned_args.reserve(sizeof...(Args));
    add_args(owned_args, std::forward<Args>(args)...);

    std::vector<X3Value> raw_args;
    raw_args.reserve(owned_args.size());
    for (const auto& arg : owned_args) {
      raw_args.push_back(arg.raw());
    }

    X3Value result = x3_value_invalid();
    check(x3_call(runtime_, value_, raw_args.data(), static_cast<uint32_t>(raw_args.size()), &result));
    return Value(runtime_, result);
  }

protected:
  void check(X3Status status) const {
    if (status != X3_STATUS_OK) {
      const char* error = x3_runtime_last_error(runtime_);
      throw std::runtime_error(error == nullptr ? "XLang3 call failed" : error);
    }
  }

  void add_args(std::vector<Value>&) const {}

  template <typename T, typename... Rest>
  void add_args(std::vector<Value>& out, T&& first, Rest&&... rest) const {
    out.emplace_back(runtime_, to_value(std::forward<T>(first)));
    add_args(out, std::forward<Rest>(rest)...);
  }

  X3Value to_value(const Value& value) const {
    x3_value_retain(value.raw());
    return value.raw();
  }

  X3Value to_value(Value&& value) const {
    X3Value raw = value.raw();
    x3_value_retain(raw);
    return raw;
  }

  X3Value to_value(int value) const { return x3_value_int64(value); }
  X3Value to_value(int64_t value) const { return x3_value_int64(value); }
  X3Value to_value(double value) const { return x3_value_double(value); }
  X3Value to_value(bool value) const { return x3_value_bool(value ? 1 : 0); }
  X3Value to_value(const char* value) const { return x3_value_string(runtime_, value); }
  X3Value to_value(const std::string& value) const { return x3_value_string(runtime_, value.c_str()); }

  X3Runtime* runtime_;
  X3Value value_;
};

inline Value Runtime::List() {
  return Value(runtime_, x3_value_list(runtime_));
}

inline Value Runtime::Dict() {
  return Value(runtime_, x3_value_dict(runtime_));
}

class Function : public Value {
public:
  Function() = default;
  Function(X3Runtime* runtime, X3Value value) : Value(runtime, value) {}
  Function(const Value& value) : Value(value) {}
  Function(Value&& value) noexcept : Value(std::move(value)) {}
};

class Package : public Value {
public:
  Package(Runtime& runtime, const char* module_name, const char* package_name = nullptr)
      : Value(runtime.get(), x3_value_invalid()) {
    X3Value result = x3_value_invalid();
    runtime.check(x3_runtime_import_module(runtime.get(), package_name, module_name, &result));
    value_ = result;
  }

  Function fn(const char* name) const {
    return Function((*this)[name]);
  }
};

} // namespace X
