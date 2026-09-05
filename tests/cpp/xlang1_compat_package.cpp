/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "xlang3/xlang3.h"

#include <cstring>
#include <mutex>
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

  X::Value append_total(X::Value& list) {
    if (!list.Append(X::Value(total_))) throw X::Error("expected a list");
    return list;
  }

  long long read_value(const X::Value& value) const {
    return value.ToLongLong();
  }

  BEGIN_PACKAGE(xlang1_compat_counter)
    APISET().AddFunc<1>("add", &xlang1_compat_counter::add);
    APISET().AddProp("total", &xlang1_compat_counter::total);
    APISET().AddFunc<1>("append_total", &xlang1_compat_counter::append_total);
    APISET().AddFunc<1>("read_value", &xlang1_compat_counter::read_value);
  END_PACKAGE

private:
  long long total_ = 0;
};

class xlang1_compat_sample {
public:
  X::Value capture(std::vector<X::Value> expressions) {
    auto captured = X::Value::List(Host());
    for (const auto& expr : expressions) {
      if (!expr.IsExpression()) throw X::Error("expected captured expression");
      captured.Append(expr);
    }
    {
      std::lock_guard<std::mutex> lock(captured_mutex_);
      captured_ = std::move(captured);
    }
    return __xlang3_package_->GetLiveValue("identity");
  }

  X::Value identity(X::Value function) { return function; }
  X::Value captured() {
    std::lock_guard<std::mutex> lock(captured_mutex_);
    return captured_;
  }
  X::Value evaluate(X::Value expression, X::Value bindings) {
    X::Value result, reservations;
    if (!expression.Evaluate(bindings, result, reservations))
      throw X::Error(Host()->runtime_last_error(Host()->runtime));
    auto list = X::Value::List(Host());
    list.Append(result);
    list.Append(reservations);
    return list;
  }
  X::Value expression_roundtrip(X::Value expression) {
    X::Value bytes, restored;
    if (!expression.ToBytes(bytes) || !bytes.FromBytes(restored)) throw X::Error("expression serialization failed");
    return restored;
  }
  X::Value expression_info(X::Value expression) {
    X3Value result = x3_value_invalid();
    if (Host()->expression_inspect(Host()->runtime, expression.raw(), &result) != X3_STATUS_OK)
      throw X::Error("expression inspection failed");
    return X::Value(Host(), result, false);
  }
  long long add(long long left, long long right) {
    return left + right;
  }

  X::Value make_list() {
    auto list = X::Value::List(Host());
    list += X::Value(1);
    list += X::Value::String(Host(), "two");
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

  X::Value set_item(X::Value& dict, const X::Value& value) {
    if (!dict.Set("item", value)) throw X::Error("expected a dict");
    return dict;
  }

  long long read_value(const X::Value& value) const {
    return value.ToLongLong();
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
    X::Stream stream(Host());
    if (!source.ToBytes(stream) || !stream.Rewind()) {
      return X::Value::String(Host(), "stream-write-failed");
    }
    X::Value restored;
    if (!restored.FromBytes(stream)) {
      return X::Value::String(Host(), "stream-read-failed");
    }
    return X::Value::String(Host(), check_payload(restored) ? "ok" : "bad-stream-payload");
  }

  X::Value operator_style() {
    auto list = X::Value::List(Host());
    list += X::Value(7);
    list += X::Value::String(Host(), "eight");

    auto dict = X::Value::Dict(Host());
    dict.Set("items", list);
    dict.Set("sum", X::Value(20) + X::Value(22));

    const bool ok = dict["items"][0] == X::Value(7) &&
                    dict["items"].second() == X::Value::String(Host(), "eight") &&
                    dict["sum"] == X::Value(42) &&
                    dict["sum"] != X::Value(43);
    return X::Value::String(Host(), ok ? "ok" : "bad-operators");
  }

  BEGIN_PACKAGE(xlang1_compat_sample)
    APISET().AddExpressionDecorator("Task", &xlang1_compat_sample::capture);
    APISET().AddFunc<1>("identity", &xlang1_compat_sample::identity);
    APISET().AddFunc<1>("snapshot", &xlang1_compat_sample::identity, X3_NATIVE_IPC_ARGS_BY_VALUE);
    APISET().AddFunc<0>("captured", &xlang1_compat_sample::captured);
    APISET().AddFunc<2>("evaluate", &xlang1_compat_sample::evaluate);
    APISET().AddFunc<1>("expression_roundtrip", &xlang1_compat_sample::expression_roundtrip);
    APISET().AddFunc<1>("expression_info", &xlang1_compat_sample::expression_info);
    APISET().AddFunc<2>("add", &xlang1_compat_sample::add);
    APISET().AddFunc<0>("make_list", &xlang1_compat_sample::make_list);
    APISET().AddFunc<0>("make_dict", &xlang1_compat_sample::make_dict);
    APISET().AddFunc<1>("bytes_size", &xlang1_compat_sample::bytes_size);
    APISET().AddFunc<2>("set_item", &xlang1_compat_sample::set_item);
    APISET().AddFunc<1>("read_value", &xlang1_compat_sample::read_value);
    APISET().AddFunc<2>("fire_changed", &xlang1_compat_sample::fire_changed);
    APISET().AddFunc<0>("is_changed_event", &xlang1_compat_sample::is_changed_event);
    APISET().AddFunc<0>("value_bytes_roundtrip", &xlang1_compat_sample::value_bytes_roundtrip);
    APISET().AddFunc<0>("stream_roundtrip", &xlang1_compat_sample::stream_roundtrip);
    APISET().AddFunc<0>("operator_style", &xlang1_compat_sample::operator_style);
    APISET().AddEvent("changed");
    APISET().AddClass<0, xlang1_compat_counter>("Counter");
    APISET().AddConst("name", "compat");
  END_PACKAGE

private:
  X::Value make_payload() {
    auto dict = X::Value::Dict(Host());
    auto list = X::Value::List(Host());
    list += X::Value(1);
    list += X::Value::String(Host(), "two");

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
           items[0].ToLongLong() == 1 &&
           items[1].ToString(false) == "two" &&
           blob_data != nullptr &&
           blob_size == 4 &&
           blob_data[0] == 'a' &&
           blob_data[1] == '\0' &&
           blob_data[2] == 'b' &&
           blob_data[3] == 'c';
  }

  std::string name_ = "compat";
  X::Value captured_;
  std::mutex captured_mutex_;
};

XLANG3_IMPLEMENT_PACKAGE(xlang1_compat_sample)
