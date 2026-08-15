#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3 {

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
  Function,
  NativeFunction,
};

struct Object {
  ObjectKind kind;
  uint32_t refcnt;
};

struct FunctionObject {
  Object header;
  uint32_t function_id = 0;
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
  static Value function(uint32_t function_id);
  static Value native_function(uint32_t native_id, std::string name, NativeFunctionCallback callback);
};

struct TupleObject {
  Object header;
  std::vector<Value> items;
};

FunctionObject* value_as_function(const Value& value);
NativeFunctionObject* value_as_native_function(const Value& value);

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
