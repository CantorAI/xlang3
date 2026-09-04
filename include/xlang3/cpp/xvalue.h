/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/xmodule.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace X {

class Value {
public:
  Value() = default;
  explicit Value(std::nullptr_t) : value_(x3_value_none()) {}
  explicit Value(bool value) : value_(x3_value_bool(value ? 1 : 0)) {}
  explicit Value(int value) : value_(x3_value_int64(value)) {}
  explicit Value(long long value) : value_(x3_value_int64(value)) {}
  explicit Value(double value) : value_(x3_value_double(value)) {}

  Value(X3PackageHost* host, X3Value value, bool borrow) : host_(host), value_(value) {
    if (borrow && host_ != nullptr && host_->value_retain != nullptr) {
      host_->value_retain(value_);
    }
  }

  Value(const Value& other) : host_(other.host_), value_(other.value_) {
    if (host_ != nullptr && host_->value_retain != nullptr) {
      host_->value_retain(value_);
    }
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
    if (host_ != nullptr && host_->value_retain != nullptr) {
      host_->value_retain(value_);
    }
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
  X3Value raw() const { return value_; }

  X3Value Detach() {
    X3Value raw = value_;
    host_ = nullptr;
    value_ = x3_value_invalid();
    return raw;
  }

  bool IsValid() const { return value_.tag != X3_TAG_INVALID; }
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

  double ToDouble() const {
    if (value_.tag == X3_TAG_DOUBLE) return value_.as.f64;
    return static_cast<double>(ToLongLong());
  }

  std::string ToString(bool = false) const {
    if (host_ == nullptr || host_->value_to_cstr == nullptr) return {};
    const char* text = host_->value_to_cstr(host_->runtime, value_);
    return text == nullptr ? std::string() : std::string(text);
  }

  uint64_t Size() const {
    const void* data = nullptr;
    uint64_t bytes_size = 0;
    if (host_ != nullptr && host_->value_bytes_data(host_->runtime, value_, &data, &bytes_size) == X3_STATUS_OK) {
      return bytes_size;
    }
    uint64_t len = 0;
    if (host_ != nullptr && host_->len(host_->runtime, value_, &len) == X3_STATUS_OK) {
      return len;
    }
    return 0;
  }

  bool Append(const Value& item) {
    return host_ != nullptr && host_->list_append(host_->runtime, value_, item.raw()) == X3_STATUS_OK;
  }

  bool Set(const char* key, const Value& item) {
    if (host_ == nullptr || key == nullptr) return false;
    Value key_value = String(host_, key);
    return host_->dict_set_item(host_->runtime, value_, key_value.raw(), item.raw()) == X3_STATUS_OK;
  }

  Value Get(const char* key) const {
    if (host_ == nullptr || key == nullptr) return {};
    Value key_value = String(host_, key);
    X3Value out = x3_value_invalid();
    if (host_->get_item(host_->runtime, value_, key_value.raw(), &out) != X3_STATUS_OK) return {};
    return Value(host_, out, false);
  }

  Value Get(uint64_t index) const {
    if (host_ == nullptr) return {};
    X3Value out = x3_value_invalid();
    if (host_->get_item(host_->runtime, value_, x3_value_int64(static_cast<int64_t>(index)), &out) != X3_STATUS_OK) return {};
    return Value(host_, out, false);
  }

  bool DictEntry(uint64_t index, Value& key, Value& item) const {
    if (host_ == nullptr || host_->dict_get_entry == nullptr) return false;
    X3Value raw_key = x3_value_invalid();
    X3Value raw_item = x3_value_invalid();
    if (host_->dict_get_entry(host_->runtime, value_, index, &raw_key, &raw_item) != X3_STATUS_OK) return false;
    key = Value(host_, raw_key, false);
    item = Value(host_, raw_item, false);
    return true;
  }

  const void* BytesData(uint64_t* size) const {
    const void* data = nullptr;
    uint64_t local_size = 0;
    if (host_ == nullptr ||
        host_->value_bytes_data(host_->runtime, value_, &data, &local_size) != X3_STATUS_OK) {
      if (size != nullptr) *size = 0;
      return nullptr;
    }
    if (size != nullptr) *size = local_size;
    return data;
  }

  bool ToBytes(Value& out) const {
    if (host_ == nullptr || host_->value_to_bytes == nullptr) return false;
    X3Value bytes = x3_value_invalid();
    if (host_->value_to_bytes(host_->runtime, value_, &bytes) != X3_STATUS_OK) return false;
    out = Value(host_, bytes, false);
    return true;
  }

  bool FromBytes(Value& out) const {
    if (host_ == nullptr || host_->value_from_bytes == nullptr) return false;
    X3Value value = x3_value_invalid();
    if (host_->value_from_bytes(host_->runtime, value_, &value) != X3_STATUS_OK) return false;
    out = Value(host_, value, false);
    return true;
  }

  bool Subscribe(const Value& callable, uint64_t& cookie) const {
    if (host_ == nullptr || host_->event_subscribe == nullptr) return false;
    return host_->event_subscribe(host_->runtime, value_, callable.raw(), &cookie) == X3_STATUS_OK;
  }

  bool Unsubscribe(uint64_t cookie) const {
    if (host_ == nullptr || host_->event_unsubscribe == nullptr) return false;
    return host_->event_unsubscribe(host_->runtime, value_, cookie) == X3_STATUS_OK;
  }

  bool Fire(const Value* args, uint32_t argc, Value& out) const {
    if (host_ == nullptr || host_->event_fire == nullptr || (argc != 0 && args == nullptr)) return false;
    std::vector<X3Value> raw_args;
    raw_args.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) {
      raw_args.push_back(args[i].raw());
    }
    X3Value result = x3_value_invalid();
    if (host_->event_fire(host_->runtime, value_, raw_args.empty() ? nullptr : raw_args.data(), argc, &result) != X3_STATUS_OK) return false;
    out = Value(host_, result, false);
    return true;
  }

  bool Fire(const std::vector<Value>& args, Value& out) const {
    return Fire(args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), out);
  }

  bool Call(const Value* args, uint32_t argc, Value& out) const {
    if (host_ == nullptr || (argc != 0 && args == nullptr)) return false;
    std::vector<X3Value> raw_args;
    raw_args.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) {
      raw_args.push_back(args[i].raw());
    }
    X3Value result = x3_value_invalid();
    if (host_->call == nullptr ||
        host_->call(host_->runtime, value_, raw_args.empty() ? nullptr : raw_args.data(), argc, &result) != X3_STATUS_OK) {
      return false;
    }
    out = Value(host_, result, false);
    return true;
  }

  bool Call(const std::vector<Value>& args, Value& out) const {
    return Call(args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), out);
  }

private:
  X3ObjectKind object_kind() const {
    if (host_ == nullptr || host_->value_object_kind == nullptr) return X3_OBJECT_KIND_UNKNOWN;
    return host_->value_object_kind(value_);
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
