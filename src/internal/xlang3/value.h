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

#include "xlang3/compiler.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

namespace ir {
struct Module;
}

class Runtime;
class FileSystem;
struct Value;

enum class ValueTag : uint32_t {
  Invalid = 0,
  None,
  Bool,
  Int64,
  Double,
  Object,
};

enum class ObjectKind : uint32_t {
  String = 1,
  Tuple,
  List,
  Dict,
  Set,
  DictIterator,
  SetIterator,
  Range,
  RangeIterator,
  SequenceIterator,
  Module,
  Cell,
  Function,
  NativeFunction,
  Class,
  Instance,
  BoundMethod,
  File,
};

struct Object {
  ObjectKind kind;
  std::atomic_uint32_t refcnt;
};

using NativeFunctionCallback = bool (*)(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data);

using NativeFastCallCallback = bool (*)(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void* user_data);

struct NativeFunctionObject {
  Object header;
  uint32_t native_id = 0;
  std::string name;
  NativeFunctionCallback callback = nullptr;
  NativeFastCallCallback fast_callback = nullptr;
  bool fast_releases_vm_lock = false;
  void* user_data = nullptr;
  void (*user_data_cleanup)(void*) = nullptr;
};

struct StringObject {
  Object header;
  std::string value;
};

struct Value {
  ValueTag tag = ValueTag::Invalid;
  uint32_t flags = 0;
  union {
    bool b;
    int64_t i64;
    double f64;
    Object* obj;
  } as{};

  Value() = default;
  Value(const Value& other);
  Value(Value&& other) noexcept;
  Value& operator=(const Value& other);
  Value& operator=(Value&& other) noexcept;
  ~Value();

  static Value invalid();
  static Value none();
  static Value boolean(bool value);
  static Value int64(int64_t value);
  static Value number(double value);
  static Value string(std::string value);
  static Value tuple(std::vector<Value> items);
  static Value list(std::vector<Value> items);
  static Value dict(std::vector<std::pair<Value, Value>> entries);
  static Value set(std::vector<Value> items);
  static Value range(int64_t start, int64_t stop, int64_t step);
  static Value range_iterator(int64_t current, int64_t stop, int64_t step);
  static Value sequence_iterator(Value source, uint64_t index);
  static Value module(std::string name);
  static Value cell(Value value);
  static Value function(uint32_t function_id, std::vector<Value> closure);
  static Value function(uint32_t function_id, std::vector<Value> closure, Value globals_module);
  static Value function(
      uint32_t function_id,
      std::vector<Value> closure,
      Value globals_module,
      std::shared_ptr<const ir::Module> module);
  static Value native_function(
      uint32_t native_id,
      std::string name,
      NativeFunctionCallback callback,
      void* user_data = nullptr,
      void (*user_data_cleanup)(void*) = nullptr,
      NativeFastCallCallback fast_callback = nullptr,
      bool fast_releases_vm_lock = false);
  static Value file(FileSystem* fs, std::string path, std::string mode, std::string buffer, bool writable);
  static Value class_object(
      std::string name,
      std::vector<std::pair<std::string, Value>> attrs,
      Value base = Value::invalid(),
      std::vector<std::string> instance_slots = {});
  static Value instance(Value klass);
  static Value bound_method(Value self, Value function);
};

XLANG3_HOT_INLINE Value Value::invalid() {
  return {};
}

XLANG3_HOT_INLINE Value Value::none() {
  Value v;
  v.tag = ValueTag::None;
  return v;
}

XLANG3_HOT_INLINE Value Value::boolean(bool value) {
  Value v;
  v.tag = ValueTag::Bool;
  v.as.b = value;
  return v;
}

XLANG3_HOT_INLINE Value Value::int64(int64_t value) {
  Value v;
  v.tag = ValueTag::Int64;
  v.as.i64 = value;
  return v;
}

XLANG3_HOT_INLINE Value Value::number(double value) {
  Value v;
  v.tag = ValueTag::Double;
  v.as.f64 = value;
  return v;
}

struct TupleObject {
  Object header;
  std::vector<Value> items;
};

struct CellObject {
  Object header;
  Value value;
};

struct FunctionObject {
  Object header;
  uint32_t function_id = 0;
  std::vector<Value> closure;
  Value globals_module;
  std::shared_ptr<const ir::Module> module;
};

struct FileObject {
  Object header;
  FileSystem* fs = nullptr;
  std::string path;
  std::string mode;
  std::string buffer;
  std::size_t cursor = 0;
  bool writable = false;
  bool closed = false;
};

XLANG3_HOT_INLINE FunctionObject* value_as_function(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Function) {
    return nullptr;
  }
  return reinterpret_cast<FunctionObject*>(value.as.obj);
}

XLANG3_HOT_INLINE NativeFunctionObject* value_as_native_function(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::NativeFunction) {
    return nullptr;
  }
  return reinterpret_cast<NativeFunctionObject*>(value.as.obj);
}

XLANG3_HOT_INLINE CellObject* value_as_cell(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Cell) {
    return nullptr;
  }
  return reinterpret_cast<CellObject*>(value.as.obj);
}

void retain(const Value& value);
void release(const Value& value);

XLANG3_HOT_INLINE void value_release_if_object(Value& value) {
  if (value.tag == ValueTag::Object && value.as.obj != nullptr) {
    release(value);
  }
}

XLANG3_HOT_INLINE void value_assign_fast(Value& out, const Value& value) {
  if (&out == &value) {
    return;
  }
  if (value.tag == ValueTag::Object) {
    out = value;
    return;
  }
  value_release_if_object(out);
  out.tag = value.tag;
  out.flags = value.flags;
  out.as = value.as;
}

XLANG3_HOT_INLINE void value_set_invalid(Value& out) {
  value_release_if_object(out);
  out.tag = ValueTag::Invalid;
  out.flags = 0;
  out.as.obj = nullptr;
}

XLANG3_HOT_INLINE void value_set_none(Value& out) {
  value_release_if_object(out);
  out.tag = ValueTag::None;
  out.flags = 0;
  out.as.obj = nullptr;
}

XLANG3_HOT_INLINE void value_set_bool(Value& out, bool value) {
  value_release_if_object(out);
  out.tag = ValueTag::Bool;
  out.flags = 0;
  out.as.b = value;
}

XLANG3_HOT_INLINE void value_set_int64(Value& out, int64_t value) {
  value_release_if_object(out);
  out.tag = ValueTag::Int64;
  out.flags = 0;
  out.as.i64 = value;
}

XLANG3_HOT_INLINE void value_set_number(Value& out, double value) {
  value_release_if_object(out);
  out.tag = ValueTag::Double;
  out.flags = 0;
  out.as.f64 = value;
}

std::string value_to_string(const Value& value);
bool value_truthy(const Value& value);

bool value_add(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_sub(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mul(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_div(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mod(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_compare(const std::string& op, const Value& lhs, const Value& rhs, Value& out, std::string& error);

} // namespace xlang3
