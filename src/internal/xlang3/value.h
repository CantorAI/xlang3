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
};

struct Object {
  ObjectKind kind;
  uint32_t refcnt;
};

using NativeFunctionCallback = bool (*)(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error);

struct NativeFunctionObject {
  Object header;
  uint32_t native_id = 0;
  std::string name;
  NativeFunctionCallback callback = nullptr;
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
  static Value native_function(uint32_t native_id, std::string name, NativeFunctionCallback callback);
};

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

FunctionObject* value_as_function(const Value& value);
NativeFunctionObject* value_as_native_function(const Value& value);
CellObject* value_as_cell(const Value& value);

void retain(const Value& value);
void release(const Value& value);
std::string value_to_string(const Value& value);
bool value_truthy(const Value& value);

bool value_add(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_sub(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_mul(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_div(const Value& lhs, const Value& rhs, Value& out, std::string& error);
bool value_compare(const std::string& op, const Value& lhs, const Value& rhs, Value& out, std::string& error);

} // namespace xlang3
