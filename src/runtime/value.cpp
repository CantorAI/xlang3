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
#include "xlang3/value.h"

#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cmath>
#include <sstream>

namespace xlang3 {

namespace {

template <typename T>
T* allocate_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

bool is_number(const Value& value) {
  return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
}

double as_double(const Value& value) {
  return value.tag == ValueTag::Int64 ? static_cast<double>(value.as.i64) : value.as.f64;
}

StringObject* as_string(Object* obj) {
  return reinterpret_cast<StringObject*>(obj);
}

TupleObject* as_tuple(Object* obj) {
  return reinterpret_cast<TupleObject*>(obj);
}

CellObject* as_cell(Object* obj) {
  return reinterpret_cast<CellObject*>(obj);
}

FunctionObject* as_function(Object* obj) {
  return reinterpret_cast<FunctionObject*>(obj);
}

NativeFunctionObject* as_native_function(Object* obj) {
  return reinterpret_cast<NativeFunctionObject*>(obj);
}

} // namespace

Value::Value(const Value& other) : tag(other.tag), flags(other.flags), as(other.as) {
  retain(*this);
}

Value::Value(Value&& other) noexcept : tag(other.tag), flags(other.flags), as(other.as) {
  other.tag = ValueTag::Invalid;
  other.as.obj = nullptr;
}

Value& Value::operator=(const Value& other) {
  if (this == &other) {
    return *this;
  }
  release(*this);
  tag = other.tag;
  flags = other.flags;
  as = other.as;
  retain(*this);
  return *this;
}

Value& Value::operator=(Value&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  release(*this);
  tag = other.tag;
  flags = other.flags;
  as = other.as;
  other.tag = ValueTag::Invalid;
  other.as.obj = nullptr;
  return *this;
}

Value::~Value() {
  release(*this);
}

Value Value::invalid() {
  return {};
}

Value Value::none() {
  Value v;
  v.tag = ValueTag::None;
  return v;
}

Value Value::boolean(bool value) {
  Value v;
  v.tag = ValueTag::Bool;
  v.as.b = value;
  return v;
}

Value Value::int64(int64_t value) {
  Value v;
  v.tag = ValueTag::Int64;
  v.as.i64 = value;
  return v;
}

Value Value::number(double value) {
  Value v;
  v.tag = ValueTag::Double;
  v.as.f64 = value;
  return v;
}

Value Value::string(std::string value) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<StringObject>(ObjectKind::String);
  obj->value = std::move(value);
  v.as.obj = &obj->header;
  return v;
}

Value Value::tuple(std::vector<Value> items) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<TupleObject>(ObjectKind::Tuple);
  obj->items = std::move(items);
  v.as.obj = &obj->header;
  return v;
}

Value Value::cell(Value value) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<CellObject>(ObjectKind::Cell);
  obj->value = std::move(value);
  v.as.obj = &obj->header;
  return v;
}

Value Value::function(uint32_t function_id, std::vector<Value> closure) {
  return function(function_id, std::move(closure), Value::invalid());
}

Value Value::function(uint32_t function_id, std::vector<Value> closure, Value globals_module) {
  return function(function_id, std::move(closure), std::move(globals_module), nullptr);
}

Value Value::function(
    uint32_t function_id,
    std::vector<Value> closure,
    Value globals_module,
    std::shared_ptr<const ir::Module> module) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<FunctionObject>(ObjectKind::Function);
  obj->function_id = function_id;
  obj->closure = std::move(closure);
  obj->globals_module = std::move(globals_module);
  obj->module = std::move(module);
  v.as.obj = &obj->header;
  return v;
}

Value Value::native_function(uint32_t native_id, std::string name, NativeFunctionCallback callback) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object<NativeFunctionObject>(ObjectKind::NativeFunction);
  obj->native_id = native_id;
  obj->name = std::move(name);
  obj->callback = callback;
  v.as.obj = &obj->header;
  return v;
}

FunctionObject* value_as_function(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Function) {
    return nullptr;
  }
  return as_function(value.as.obj);
}

NativeFunctionObject* value_as_native_function(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::NativeFunction) {
    return nullptr;
  }
  return as_native_function(value.as.obj);
}

CellObject* value_as_cell(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Cell) {
    return nullptr;
  }
  return as_cell(value.as.obj);
}

void retain(const Value& value) {
  if (value.tag == ValueTag::Object && value.as.obj != nullptr) {
    ++value.as.obj->refcnt;
  }
}

void release(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return;
  }
  if (--value.as.obj->refcnt != 0) {
    return;
  }
  switch (value.as.obj->kind) {
    case ObjectKind::String:
      delete as_string(value.as.obj);
      break;
    case ObjectKind::Tuple:
      delete as_tuple(value.as.obj);
      break;
    case ObjectKind::Dict:
    case ObjectKind::DictIterator:
      mapping_release_object(value.as.obj);
      break;
    case ObjectKind::Set:
    case ObjectKind::SetIterator:
      set_release_object(value.as.obj);
      break;
    case ObjectKind::Module:
      module_release_object(value.as.obj);
      break;
    case ObjectKind::List:
    case ObjectKind::Range:
    case ObjectKind::RangeIterator:
    case ObjectKind::SequenceIterator:
      sequence_release_object(value.as.obj);
      break;
    case ObjectKind::Cell:
      delete as_cell(value.as.obj);
      break;
    case ObjectKind::Function:
      delete as_function(value.as.obj);
      break;
    case ObjectKind::NativeFunction:
      delete as_native_function(value.as.obj);
      break;
    case ObjectKind::Class:
    case ObjectKind::Instance:
    case ObjectKind::BoundMethod:
      object_model_release_object(value.as.obj);
      break;
  }
}

std::string value_to_string(const Value& value) {
  switch (value.tag) {
    case ValueTag::Invalid:
      return "<invalid>";
    case ValueTag::None:
      return "None";
    case ValueTag::Bool:
      return value.as.b ? "True" : "False";
    case ValueTag::Int64:
      return std::to_string(value.as.i64);
    case ValueTag::Double: {
      std::ostringstream os;
      os << value.as.f64;
      return os.str();
    }
    case ValueTag::Object:
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
        return as_string(value.as.obj)->value;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
        const auto& items = as_tuple(value.as.obj)->items;
        std::string text = "(";
        for (size_t i = 0; i < items.size(); ++i) {
          if (i != 0) {
            text += ", ";
          }
          text += value_to_string(items[i]);
        }
        if (items.size() == 1) {
          text += ",";
        }
        text += ")";
        return text;
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::List ||
           value.as.obj->kind == ObjectKind::Range ||
           value.as.obj->kind == ObjectKind::RangeIterator ||
           value.as.obj->kind == ObjectKind::SequenceIterator)) {
        return sequence_to_string(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Dict ||
           value.as.obj->kind == ObjectKind::DictIterator)) {
        return mapping_to_string(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Set ||
           value.as.obj->kind == ObjectKind::SetIterator)) {
        return set_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Module) {
        return module_to_string(value);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Cell) {
        return "<cell>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Function) {
        return "<function>";
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::NativeFunction) {
        return "<built-in function " + as_native_function(value.as.obj)->name + ">";
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Class ||
           value.as.obj->kind == ObjectKind::Instance ||
           value.as.obj->kind == ObjectKind::BoundMethod)) {
        return object_model_to_string(value);
      }
      return "<object>";
  }
  return "<unknown>";
}

bool value_truthy(const Value& value) {
  switch (value.tag) {
    case ValueTag::Invalid:
    case ValueTag::None:
      return false;
    case ValueTag::Bool:
      return value.as.b;
    case ValueTag::Int64:
      return value.as.i64 != 0;
    case ValueTag::Double:
      return value.as.f64 != 0.0;
    case ValueTag::Object:
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
        return !as_string(value.as.obj)->value.empty();
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
        return !as_tuple(value.as.obj)->items.empty();
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::List ||
           value.as.obj->kind == ObjectKind::Range ||
           value.as.obj->kind == ObjectKind::RangeIterator ||
           value.as.obj->kind == ObjectKind::SequenceIterator)) {
        return sequence_truthy(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Dict ||
           value.as.obj->kind == ObjectKind::DictIterator)) {
        return mapping_truthy(value);
      }
      if (value.as.obj != nullptr &&
          (value.as.obj->kind == ObjectKind::Set ||
           value.as.obj->kind == ObjectKind::SetIterator)) {
        return set_truthy(value);
      }
      return true;
  }
  return false;
}

bool value_add(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    out = Value::int64(lhs.as.i64 + rhs.as.i64);
    return true;
  }
  if (is_number(lhs) && is_number(rhs)) {
    out = Value::number(as_double(lhs) + as_double(rhs));
    return true;
  }
  if (lhs.tag == ValueTag::Object && rhs.tag == ValueTag::Object &&
      lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
      lhs.as.obj->kind == ObjectKind::String && rhs.as.obj->kind == ObjectKind::String) {
    out = Value::string(as_string(lhs.as.obj)->value + as_string(rhs.as.obj)->value);
    return true;
  }
  error = "unsupported operands for +";
  return false;
}

bool value_sub(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    out = Value::int64(lhs.as.i64 - rhs.as.i64);
    return true;
  }
  if (is_number(lhs) && is_number(rhs)) {
    out = Value::number(as_double(lhs) - as_double(rhs));
    return true;
  }
  error = "unsupported operands for -";
  return false;
}

bool value_mul(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    out = Value::int64(lhs.as.i64 * rhs.as.i64);
    return true;
  }
  if (is_number(lhs) && is_number(rhs)) {
    out = Value::number(as_double(lhs) * as_double(rhs));
    return true;
  }
  error = "unsupported operands for *";
  return false;
}

bool value_div(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  if (!is_number(lhs) || !is_number(rhs)) {
    error = "unsupported operands for /";
    return false;
  }
  const double divisor = as_double(rhs);
  if (divisor == 0.0) {
    error = "division by zero";
    return false;
  }
  out = Value::number(as_double(lhs) / divisor);
  return true;
}

bool value_compare(const std::string& op, const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  bool result = false;
  if (is_number(lhs) && is_number(rhs)) {
    const double a = as_double(lhs);
    const double b = as_double(rhs);
    if (op == "==") result = a == b;
    else if (op == "!=") result = a != b;
    else if (op == "<") result = a < b;
    else if (op == "<=") result = a <= b;
    else if (op == ">") result = a > b;
    else if (op == ">=") result = a >= b;
    else {
      error = "unknown comparison operator";
      return false;
    }
    out = Value::boolean(result);
    return true;
  }
  if (op == "==" || op == "!=") {
    result = value_to_string(lhs) == value_to_string(rhs);
    out = Value::boolean(op == "==" ? result : !result);
    return true;
  }
  error = "unsupported comparison";
  return false;
}

} // namespace xlang3
