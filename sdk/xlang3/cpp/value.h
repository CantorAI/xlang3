/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/abi/xmodule.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace X {

class Runtime;

class Value {
public:
  Value() = default;
  explicit Value(std::nullptr_t) : value_(x3_value_none()) {}
  explicit Value(bool value) : value_(x3_value_bool(value ? 1 : 0)) {}
  explicit Value(int value) : value_(x3_value_int64(value)) {}
  explicit Value(long long value) : value_(x3_value_int64(value)) {}
  explicit Value(double value) : value_(x3_value_double(value)) {}
  Value(Runtime& runtime, X3Value value);
  Value(Runtime& runtime, int64_t value);
  Value(Runtime& runtime, int value);
  Value(Runtime& runtime, double value);
  Value(Runtime& runtime, bool value);
  Value(Runtime& runtime, const char* value);
  Value(Runtime& runtime, const std::string& value);
  Value(X3PackageHost* host, X3Value value, bool borrow) : host_(host), value_(value) {
    if (borrow) {
      retain();
    }
  }

  Value(const Value& other) : host_(other.host_), value_(other.value_) {
    retain();
  }

  Value(Value&& other) noexcept : host_(other.host_), value_(other.value_) {
    other.host_ = nullptr;
    other.value_ = x3_value_invalid();
  }

  Value& operator=(const Value& other) {
    if (this == &other) return *this;
    reset();
    host_ = other.host_;
    value_ = other.value_;
    retain();
    return *this;
  }

  Value& operator=(Value&& other) noexcept {
    if (this == &other) return *this;
    reset();
    host_ = other.host_;
    value_ = other.value_;
    other.host_ = nullptr;
    other.value_ = x3_value_invalid();
    return *this;
  }

  ~Value() { reset(); }

  static Value String(X3PackageHost* host, const std::string& value) {
    return Value(host, host->value_string(host->runtime, value.c_str()), false);
  }

  static Value Bytes(X3PackageHost* host, const void* data, uint64_t size) {
    return Value(host, host->value_bytes(host->runtime, data, size), false);
  }

  static Value List(X3PackageHost* host) {
    return Value(host, host->value_list(host->runtime), false);
  }

  static Value Dict(X3PackageHost* host) {
    return Value(host, host->value_dict(host->runtime), false);
  }

  X3PackageHost* host() const { return host_; }
  X3Runtime* runtime() const { return host_ == nullptr ? nullptr : host_->runtime; }
  X3Value raw() const { return value_; }

  X3Value Detach() {
    X3Value raw = value_;
    host_ = nullptr;
    value_ = x3_value_invalid();
    return raw;
  }

  bool IsValid() const { return value_.tag != X3_TAG_INVALID; }
  bool IsInt64() const { return value_.tag == X3_TAG_INT64; }
  bool IsString() const { return object_kind() == X3_OBJECT_KIND_STRING; }
  bool IsList() const { return object_kind() == X3_OBJECT_KIND_LIST; }
  bool IsDict() const { return object_kind() == X3_OBJECT_KIND_DICT; }
  bool IsEvent() const { return object_kind() == X3_OBJECT_KIND_EVENT; }

  bool IsBin() const {
    const void* data = nullptr;
    uint64_t size = 0;
    return host_ != nullptr &&
           host_->value_bytes_data(host_->runtime, value_, &data, &size) == X3_STATUS_OK;
  }

  long long ToLongLong() const {
    if (value_.tag == X3_TAG_INT64) return static_cast<long long>(value_.as.i64);
    if (value_.tag == X3_TAG_UINT64) return static_cast<long long>(value_.as.u64);
    if (value_.tag == X3_TAG_BOOL) return value_.as.b ? 1 : 0;
    return 0;
  }

  int64_t ToInt64() const { return static_cast<int64_t>(ToLongLong()); }

  double ToDouble() const {
    if (value_.tag == X3_TAG_DOUBLE) return value_.as.f64;
    return static_cast<double>(ToLongLong());
  }

  std::string ToString(bool = false) const {
    const char* text = nullptr;
    if (host_ != nullptr && host_->value_to_cstr != nullptr) {
      text = host_->value_to_cstr(host_->runtime, value_);
    }
    return text == nullptr ? std::string() : std::string(text);
  }

  uint64_t Size() const {
    const void* data = nullptr;
    uint64_t bytes_size = 0;
    if (bytes_data(&data, &bytes_size) == X3_STATUS_OK) {
      return bytes_size;
    }
    uint64_t len = 0;
    if (len_value(&len) == X3_STATUS_OK) {
      return len;
    }
    return 0;
  }

  uint64_t Len() const { return Size(); }

  bool Append(const Value& item) {
    if (host_ != nullptr && host_->list_append != nullptr) {
      return host_->list_append(host_->runtime, value_, item.raw()) == X3_STATUS_OK;
    }
    return false;
  }

  bool Append(Value& item) {
    return Append(static_cast<const Value&>(item));
  }

  template <typename T>
  bool Append(T&& item) {
    Value owned_item = MakeValue(std::forward<T>(item));
    return Append(owned_item);
  }

  Value& operator+=(const Value& item) {
    if (IsList()) {
      Append(item);
      return *this;
    }
    Value out;
    if (LocalAdd(item, out) || BinaryOp(X3_VALUE_BINARY_ADD, item, out)) {
      *this = std::move(out);
    }
    return *this;
  }

  Value operator+(const Value& right) const {
    Value out;
    if (!LocalAdd(right, out)) {
      BinaryOp(X3_VALUE_BINARY_ADD, right, out);
    }
    return out;
  }

  bool Set(const char* key, const Value& item) {
    if (host_ == nullptr || key == nullptr) return false;
    Value key_value = String(host_, key);
    return host_->dict_set_item(host_->runtime, value_, key_value.raw(), item.raw()) == X3_STATUS_OK;
  }

  bool SetItem(const Value& key, const Value& item) {
    if (host_ != nullptr && host_->dict_set_item != nullptr) {
      return host_->dict_set_item(host_->runtime, value_, key.raw(), item.raw()) == X3_STATUS_OK;
    }
    return false;
  }

  template <typename Key, typename Item>
  bool SetItem(Key&& key, Item&& item) {
    Value owned_key = MakeValue(std::forward<Key>(key));
    Value owned_item = MakeValue(std::forward<Item>(item));
    return SetItem(owned_key, owned_item);
  }

  Value Get(const char* key) const {
    if (key == nullptr) return {};
    Value key_value = MakeValue(key);
    X3Value out = x3_value_invalid();
    if (get_item(key_value.raw(), &out) != X3_STATUS_OK) return {};
    return Adopt(out);
  }

  Value Get(uint64_t index) const {
    X3Value out = x3_value_invalid();
    if (get_item(x3_value_int64(static_cast<int64_t>(index)), &out) != X3_STATUS_OK) return {};
    return Adopt(out);
  }

  Value GetItem(const Value& key) const {
    X3Value out = x3_value_invalid();
    if (get_item(key.raw(), &out) != X3_STATUS_OK) return {};
    return Adopt(out);
  }

  Value GetItem(Value& key) const {
    return GetItem(static_cast<const Value&>(key));
  }

  template <typename T>
  Value GetItem(T&& key) const {
    Value owned_key = MakeValue(std::forward<T>(key));
    return GetItem(owned_key);
  }

  Value operator[](const char* key) const {
    if (key == nullptr) return {};
    if (IsDict()) {
      return Get(key);
    }
    X3Value out = x3_value_invalid();
    if (get_attr(key, &out) != X3_STATUS_OK) return {};
    return Adopt(out);
  }

  Value operator[](const std::string& key) const {
    return Get(key.c_str());
  }

  Value operator[](int index) const {
    return Get(static_cast<uint64_t>(index));
  }

  Value operator[](long long index) const {
    return Get(static_cast<uint64_t>(index));
  }

  Value first() const { return (*this)[0]; }
  Value second() const { return (*this)[1]; }

  bool DictEntry(uint64_t index, Value& key, Value& item) const {
    if (host_ == nullptr || host_->dict_get_entry == nullptr) return false;
    X3Value raw_key = x3_value_invalid();
    X3Value raw_item = x3_value_invalid();
    X3Status status = host_->dict_get_entry(host_->runtime, value_, index, &raw_key, &raw_item);
    if (status != X3_STATUS_OK) return false;
    key = Adopt(raw_key);
    item = Adopt(raw_item);
    return true;
  }

  const void* BytesData(uint64_t* size) const {
    const void* data = nullptr;
    uint64_t local_size = 0;
    if (bytes_data(&data, &local_size) != X3_STATUS_OK) {
      if (size != nullptr) *size = 0;
      return nullptr;
    }
    if (size != nullptr) *size = local_size;
    return data;
  }

  bool ToBytes(Value& out) const {
    if (host_ == nullptr || host_->value_to_bytes == nullptr) return false;
    X3Value bytes = x3_value_invalid();
    X3Status status = host_->value_to_bytes(host_->runtime, value_, &bytes);
    if (status != X3_STATUS_OK) return false;
    out = Adopt(bytes);
    return true;
  }

  bool FromBytes(Value& out) const {
    if (host_ == nullptr || host_->value_from_bytes == nullptr) return false;
    X3Value value = x3_value_invalid();
    X3Status status = host_->value_from_bytes(host_->runtime, value_, &value);
    if (status != X3_STATUS_OK) return false;
    out = Adopt(value);
    return true;
  }

  bool Subscribe(const Value& callable, uint64_t& cookie) const {
    if (host_ != nullptr && host_->event_subscribe != nullptr) {
      return host_->event_subscribe(host_->runtime, value_, callable.raw(), &cookie) == X3_STATUS_OK;
    }
    return false;
  }

  bool Unsubscribe(uint64_t cookie) const {
    if (host_ != nullptr && host_->event_unsubscribe != nullptr) {
      return host_->event_unsubscribe(host_->runtime, value_, cookie) == X3_STATUS_OK;
    }
    return false;
  }

  bool Fire(const Value* args, uint32_t argc, Value& out) const {
    if (host_ == nullptr || host_->event_fire == nullptr || (argc != 0 && args == nullptr)) return false;
    std::vector<X3Value> raw_args;
    raw_args.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) {
      raw_args.push_back(args[i].raw());
    }
    X3Value result = x3_value_invalid();
    X3Status status = host_->event_fire(host_->runtime, value_, raw_args.empty() ? nullptr : raw_args.data(), argc, &result);
    if (status != X3_STATUS_OK) return false;
    out = Adopt(result);
    return true;
  }

  bool Fire(const std::vector<Value>& args, Value& out) const {
    return Fire(args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), out);
  }

  bool Call(const Value* args, uint32_t argc, Value& out) const {
    if (host_ == nullptr || host_->call == nullptr || (argc != 0 && args == nullptr)) return false;
    std::vector<X3Value> raw_args;
    raw_args.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) {
      raw_args.push_back(args[i].raw());
    }
    X3Value result = x3_value_invalid();
    X3Status status = host_->call(host_->runtime, value_, raw_args.empty() ? nullptr : raw_args.data(), argc, &result);
    if (status != X3_STATUS_OK) return false;
    out = Adopt(result);
    return true;
  }

  bool Call(const std::vector<Value>& args, Value& out) const {
    return Call(args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), out);
  }

  template <typename... Args>
  Value operator()(Args&&... args) const {
    std::vector<Value> values;
    values.reserve(sizeof...(Args));
    add_call_args(values, std::forward<Args>(args)...);
    Value out;
    Call(values, out);
    return out;
  }

  bool operator==(const Value& right) const {
    int32_t result = 0;
    return LocalEqual(right, result) ? result != 0 : CompareOp(X3_VALUE_COMPARE_EQ, right, result) && result != 0;
  }

  bool operator!=(const Value& right) const {
    int32_t result = 0;
    return LocalEqual(right, result) ? result == 0 : CompareOp(X3_VALUE_COMPARE_NE, right, result) && result != 0;
  }

  bool operator<(const Value& right) const {
    int32_t result = 0;
    return CompareOp(X3_VALUE_COMPARE_LT, right, result) && result != 0;
  }

  bool operator<=(const Value& right) const {
    int32_t result = 0;
    return CompareOp(X3_VALUE_COMPARE_LE, right, result) && result != 0;
  }

  bool operator>(const Value& right) const {
    int32_t result = 0;
    return CompareOp(X3_VALUE_COMPARE_GT, right, result) && result != 0;
  }

  bool operator>=(const Value& right) const {
    int32_t result = 0;
    return CompareOp(X3_VALUE_COMPARE_GE, right, result) && result != 0;
  }

private:
  Value Adopt(X3Value value) const {
    return Value(host_, value, false);
  }

  Value MakeValue(const Value& value) const {
    return value;
  }

  Value MakeValue(Value&& value) const {
    return std::move(value);
  }

  Value MakeValue(const char* value) const {
    return host_ == nullptr ? Value() : Value::String(host_, value == nullptr ? std::string() : std::string(value));
  }

  Value MakeValue(const std::string& value) const {
    return host_ == nullptr ? Value() : Value::String(host_, value);
  }

  template <typename T>
  Value MakeValue(T value) const {
    return Value(value);
  }

  bool BinaryOp(X3ValueBinaryOp op, const Value& right, Value& out) const {
    X3PackageHost* host = host_ != nullptr ? host_ : right.host_;
    X3Value result = x3_value_invalid();
    if (host != nullptr && host->value_binary_op != nullptr) {
      if (host->value_binary_op(host->runtime, op, value_, right.raw(), &result) != X3_STATUS_OK) return false;
      out = Value(host, result, false);
      return true;
    }
    return false;
  }

  bool CompareOp(X3ValueCompareOp op, const Value& right, int32_t& out) const {
    X3PackageHost* host = host_ != nullptr ? host_ : right.host_;
    if (host != nullptr && host->value_compare_op != nullptr) {
      return host->value_compare_op(host->runtime, op, value_, right.raw(), &out) == X3_STATUS_OK;
    }
    return false;
  }

  static bool IsLocalNumber(const X3Value& value) {
    return value.tag == X3_TAG_BOOL || value.tag == X3_TAG_INT64 ||
           value.tag == X3_TAG_UINT64 || value.tag == X3_TAG_DOUBLE;
  }

  static double LocalDouble(const X3Value& value) {
    if (value.tag == X3_TAG_DOUBLE) return value.as.f64;
    if (value.tag == X3_TAG_UINT64) return static_cast<double>(value.as.u64);
    if (value.tag == X3_TAG_BOOL) return value.as.b ? 1.0 : 0.0;
    return static_cast<double>(value.as.i64);
  }

  static bool LocalAddValue(const X3Value& left, const X3Value& right, Value& out) {
    if (!IsLocalNumber(left) || !IsLocalNumber(right)) return false;
    if (left.tag == X3_TAG_DOUBLE || right.tag == X3_TAG_DOUBLE) {
      out = Value(LocalDouble(left) + LocalDouble(right));
      return true;
    }
    out = Value(static_cast<long long>(LocalDouble(left)) + static_cast<long long>(LocalDouble(right)));
    return true;
  }

  bool LocalAdd(const Value& right, Value& out) const {
    return LocalAddValue(value_, right.value_, out);
  }

  bool LocalEqual(const Value& right, int32_t& out) const {
    if (value_.tag == X3_TAG_NONE || right.value_.tag == X3_TAG_NONE ||
        value_.tag == X3_TAG_INVALID || right.value_.tag == X3_TAG_INVALID) {
      out = value_.tag == right.value_.tag ? 1 : 0;
      return true;
    }
    if (IsLocalNumber(value_) && IsLocalNumber(right.value_)) {
      out = LocalDouble(value_) == LocalDouble(right.value_) ? 1 : 0;
      return true;
    }
    if (value_.tag == X3_TAG_OBJECT && right.value_.tag == X3_TAG_OBJECT &&
        value_.as.obj == right.value_.as.obj) {
      out = 1;
      return true;
    }
    return false;
  }

  void add_call_args(std::vector<Value>&) const {}

  void add_one_call_arg(std::vector<Value>& out, const Value& value) const {
    out.push_back(value);
  }

  void add_one_call_arg(std::vector<Value>& out, Value&& value) const {
    out.push_back(std::move(value));
  }

  void add_one_call_arg(std::vector<Value>& out, const char* value) const {
    out.push_back(MakeValue(value));
  }

  void add_one_call_arg(std::vector<Value>& out, const std::string& value) const {
    out.push_back(MakeValue(value));
  }

  template <typename T>
  void add_one_call_arg(std::vector<Value>& out, T value) const {
    out.push_back(Value(value));
  }

  template <typename T, typename... Rest>
  void add_call_args(std::vector<Value>& out, T&& first, Rest&&... rest) const {
    add_one_call_arg(out, std::forward<T>(first));
    add_call_args(out, std::forward<Rest>(rest)...);
  }

  X3ObjectKind object_kind() const {
    if (host_ != nullptr && host_->value_object_kind != nullptr) return host_->value_object_kind(value_);
    return X3_OBJECT_KIND_UNKNOWN;
  }

  X3Status get_item(X3Value key, X3Value* out) const {
    if (host_ != nullptr && host_->get_item != nullptr) {
      return host_->get_item(host_->runtime, value_, key, out);
    }
    return X3_STATUS_ERROR;
  }

  X3Status get_attr(const char* name, X3Value* out) const {
    if (host_ != nullptr && host_->get_attr != nullptr) {
      return host_->get_attr(host_->runtime, value_, name, out);
    }
    return X3_STATUS_ERROR;
  }

  X3Status bytes_data(const void** data, uint64_t* size) const {
    if (host_ != nullptr && host_->value_bytes_data != nullptr) {
      return host_->value_bytes_data(host_->runtime, value_, data, size);
    }
    return X3_STATUS_ERROR;
  }

  X3Status len_value(uint64_t* len) const {
    if (host_ != nullptr && host_->len != nullptr) {
      return host_->len(host_->runtime, value_, len);
    }
    return X3_STATUS_ERROR;
  }

  void retain() {
    if (host_ != nullptr && host_->value_retain != nullptr) {
      host_->value_retain(value_);
    }
  }

  void reset() {
    if (host_ != nullptr && host_->value_release != nullptr) {
      host_->value_release(value_);
    }
    host_ = nullptr;
    value_ = x3_value_invalid();
  }

  X3PackageHost* host_ = nullptr;
  X3Value value_ = x3_value_invalid();
};

} // namespace X
