/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "xlang3/cpp/xpackage.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

class xlang1_compat_counter {
public:
  long long add(long long value) {
    total_ += value;
    return total_;
  }

  long long total() const {
    return total_;
  }

  BEGIN_PACKAGE(xlang1_compat_counter)
    APISET().AddFunc<1>("add", &xlang1_compat_counter::add);
    APISET().AddProp("total", &xlang1_compat_counter::total);
  END_PACKAGE

private:
  long long total_ = 0;
};

class xlang1_compat_sample {
public:
  long long add(long long left, long long right) {
    return left + right;
  }

  X::Value make_list() {
    auto list = X::Value::List(Host());
    list.Append(X::Value(1));
    list.Append(X::Value::String(Host(), "two"));
    return list;
  }

  X::Value make_dict() {
    auto dict = X::Value::Dict(Host());
    dict.Set("answer", X::Value(42));
    dict.Set("name", X::Value::String(Host(), name_));
    return dict;
  }

  long long bytes_size(X::Value value) {
    return value.IsBin() ? static_cast<long long>(value.Size()) : -1;
  }

  X::Value fire_changed(long long left, long long right) {
    X::Value changed = GetEvent("changed");
    X::Value result;
    std::vector<X::Value> args;
    args.emplace_back(left);
    args.emplace_back(right);
    if (!changed.Fire(args, result)) {
      return X::Value::String(Host(), "fire-failed");
    }
    return result;
  }

  bool is_changed_event() {
    return GetEvent("changed").IsEvent();
  }

  X::Value value_bytes_roundtrip() {
    X::Value source = make_payload();
    X::Value bytes;
    if (!source.ToBytes(bytes)) {
      return X::Value::String(Host(), "to-bytes-failed");
    }
    X::Value restored;
    if (!bytes.FromBytes(restored)) {
      return X::Value::String(Host(), "from-bytes-failed");
    }
    return X::Value::String(Host(), check_payload(restored) ? "ok" : "bad-payload");
  }

  X::Value stream_roundtrip() {
    X::Value source = make_payload();
    X::Value bytes;
    if (!source.ToBytes(bytes)) {
      return X::Value::String(Host(), "stream-write-failed");
    }
    X::Value restored;
    if (!bytes.FromBytes(restored)) {
      return X::Value::String(Host(), "stream-read-failed");
    }
    return X::Value::String(Host(), check_payload(restored) ? "ok" : "bad-stream-payload");
  }

  BEGIN_PACKAGE(xlang1_compat_sample)
    APISET().AddFunc<2>("add", &xlang1_compat_sample::add);
    APISET().AddFunc<0>("make_list", &xlang1_compat_sample::make_list);
    APISET().AddFunc<0>("make_dict", &xlang1_compat_sample::make_dict);
    APISET().AddFunc<1>("bytes_size", &xlang1_compat_sample::bytes_size);
    APISET().AddFunc<2>("fire_changed", &xlang1_compat_sample::fire_changed);
    APISET().AddFunc<0>("is_changed_event", &xlang1_compat_sample::is_changed_event);
    APISET().AddFunc<0>("value_bytes_roundtrip", &xlang1_compat_sample::value_bytes_roundtrip);
    APISET().AddFunc<0>("stream_roundtrip", &xlang1_compat_sample::stream_roundtrip);
    APISET().AddEvent("changed");
    APISET().AddClass<0, xlang1_compat_counter>("Counter");
    APISET().AddConst("name", "compat");
  END_PACKAGE

private:
  X::Value make_payload() {
    auto dict = X::Value::Dict(Host());
    auto list = X::Value::List(Host());
    list.Append(X::Value(1));
    list.Append(X::Value::String(Host(), "two"));

    const char raw[] = {'a', '\0', 'b', 'c'};
    dict.Set("items", list);
    dict.Set("blob", X::Value::Bytes(Host(), raw, sizeof(raw)));
    dict.Set("name", X::Value::String(Host(), name_));
    return dict;
  }

  bool check_payload(const X::Value& value) {
    if (!value.IsDict()) return false;
    X::Value items = value.Get("items");
    X::Value blob = value.Get("blob");
    X::Value name = value.Get("name");
    if (!items.IsList() || !blob.IsBin() || name.ToString(false) != name_) return false;

    uint64_t blob_size = 0;
    const auto* blob_data = static_cast<const char*>(blob.BytesData(&blob_size));
    return items.Size() == 2 &&
           items.Get(uint64_t{0}).ToLongLong() == 1 &&
           items.Get(uint64_t{1}).ToString(false) == "two" &&
           blob_data != nullptr &&
           blob_size == 4 &&
           blob_data[0] == 'a' &&
           blob_data[1] == '\0' &&
           blob_data[2] == 'b' &&
           blob_data[3] == 'c';
  }

  std::string name_ = "compat";
};

XLANG3_IMPLEMENT_PACKAGE(xlang1_compat_sample)
