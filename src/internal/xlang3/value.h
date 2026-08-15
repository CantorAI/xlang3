#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3 {

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
  Function,
};

struct Object {
  ObjectKind kind;
  uint32_t refcnt;
};

struct FunctionObject {
  Object header;
  uint32_t function_id = 0;
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
  static Value function(uint32_t function_id);
};

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
