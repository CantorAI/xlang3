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
#include "xlang3/builtins.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/value.h"

#include <cmath>

namespace xlang3 {
namespace {

using BinaryValueOp = bool (*)(const Value&, const Value&, Value&, std::string&);

bool expect_argc(uint32_t argc, uint32_t expected, const char* name, std::string& error) {
  if (argc == expected) {
    return true;
  }
  error = std::string(name) + "() expected " + std::to_string(expected) + " arguments, got " + std::to_string(argc);
  return false;
}

bool operator_binary(
    const char* name,
    BinaryValueOp op,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error) {
  if (!expect_argc(argc, 2, name, error)) {
    return false;
  }
  return op(args[0], args[1], out, error);
}

bool operator_compare(const char* op, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!expect_argc(argc, 2, op, error)) {
    return false;
  }
  return value_compare(op, args[0], args[1], out, error);
}

bool operator_identity_equal(const Value& lhs, const Value& rhs) {
  if (lhs.tag != rhs.tag) {
    return false;
  }
  switch (lhs.tag) {
    case ValueTag::None:
      return true;
    case ValueTag::Bool:
      return lhs.as.b == rhs.as.b;
    case ValueTag::Object:
      return lhs.as.obj == rhs.as.obj;
    default:
      return false;
  }
}

bool op_lt(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_compare("<", args, argc, out, error);
}

bool op_le(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_compare("<=", args, argc, out, error);
}

bool op_eq(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_compare("==", args, argc, out, error);
}

bool op_ne(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_compare("!=", args, argc, out, error);
}

bool op_ge(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_compare(">=", args, argc, out, error);
}

bool op_gt(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_compare(">", args, argc, out, error);
}

bool op_add(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("add", value_add, args, argc, out, error);
}

bool op_sub(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("sub", value_sub, args, argc, out, error);
}

bool op_mul(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("mul", value_mul, args, argc, out, error);
}

bool op_truediv(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("truediv", value_div, args, argc, out, error);
}

bool op_floordiv(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("floordiv", value_floor_div, args, argc, out, error);
}

bool op_mod(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("mod", value_mod, args, argc, out, error);
}

bool op_pow(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("pow", value_pow, args, argc, out, error);
}

bool op_and(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("and_", value_bit_and, args, argc, out, error);
}

bool op_or(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("or_", value_bit_or, args, argc, out, error);
}

bool op_xor(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("xor", value_bit_xor, args, argc, out, error);
}

bool op_lshift(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("lshift", value_shift_left, args, argc, out, error);
}

bool op_rshift(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("rshift", value_shift_right, args, argc, out, error);
}

bool op_concat(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return operator_binary("concat", value_add, args, argc, out, error);
}

bool op_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 2, "getitem", error)) {
    return false;
  }
  if (value_as_instance(args[0]) != nullptr) {
    Value getitem;
    std::string attr_error;
    if (object_get_attr(args[0], "__getitem__", getitem, attr_error)) {
      return runtime_call_callable(runtime, getitem, &args[1], 1, out, error);
    }
  }
  return sequence_get_item(args[0], args[1], out, error);
}

bool op_setitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 3, "setitem", error)) {
    return false;
  }
  if (value_as_instance(args[0]) != nullptr) {
    Value setitem;
    std::string attr_error;
    if (object_get_attr(args[0], "__setitem__", setitem, attr_error)) {
      Value call_args[2] = {args[1], args[2]};
      Value ignored;
      if (!runtime_call_callable(runtime, setitem, call_args, 2, ignored, error)) {
        return false;
      }
      value_set_none(out);
      return true;
    }
  }
  Value target;
  value_assign_fast(target, args[0]);
  if (!sequence_set_item(target, args[1], args[2], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool op_delitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 2, "delitem", error)) {
    return false;
  }
  if (value_as_instance(args[0]) != nullptr) {
    Value delitem;
    std::string attr_error;
    if (object_get_attr(args[0], "__delitem__", delitem, attr_error)) {
      Value ignored;
      if (!runtime_call_callable(runtime, delitem, &args[1], 1, ignored, error)) {
        return false;
      }
      value_set_none(out);
      return true;
    }
  }
  Value target;
  value_assign_fast(target, args[0]);
  if (!sequence_delete_item(target, args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool op_contains(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 2, "contains", error)) {
    return false;
  }
  if (value_as_instance(args[0]) != nullptr) {
    Value contains;
    std::string attr_error;
    if (object_get_attr(args[0], "__contains__", contains, attr_error)) {
      Value result;
      if (!runtime_call_callable(runtime, contains, &args[1], 1, result, error)) {
        return false;
      }
      out = Value::boolean(value_truthy(result));
      return true;
    }
  }
  bool result = false;
  if (!value_contains(args[0], args[1], result, error)) {
    return false;
  }
  out = Value::boolean(result);
  return true;
}

bool op_truth(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "truth", error)) {
    return false;
  }
  out = Value::boolean(value_truthy(args[0]));
  return true;
}

bool op_not(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "not_", error)) {
    return false;
  }
  out = Value::boolean(!value_truthy(args[0]));
  return true;
}

bool op_is(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 2, "is_", error)) {
    return false;
  }
  out = Value::boolean(operator_identity_equal(args[0], args[1]));
  return true;
}

bool op_is_not(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 2, "is_not", error)) {
    return false;
  }
  out = Value::boolean(!operator_identity_equal(args[0], args[1]));
  return true;
}

bool op_is_none(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "is_none", error)) {
    return false;
  }
  out = Value::boolean(args[0].tag == ValueTag::None);
  return true;
}

bool op_is_not_none(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "is_not_none", error)) {
    return false;
  }
  out = Value::boolean(args[0].tag != ValueTag::None);
  return true;
}

bool op_abs(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "abs", error)) {
    return false;
  }
  if (args[0].tag == ValueTag::Int64) {
    out = Value::int64(args[0].as.i64 < 0 ? -args[0].as.i64 : args[0].as.i64);
    return true;
  }
  if (args[0].tag == ValueTag::Double) {
    out = Value::number(std::fabs(args[0].as.f64));
    return true;
  }
  error = "bad operand type for abs()";
  return false;
}

bool op_neg(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "neg", error)) {
    return false;
  }
  if (args[0].tag == ValueTag::Int64) {
    out = Value::int64(-args[0].as.i64);
    return true;
  }
  if (args[0].tag == ValueTag::Double) {
    out = Value::number(-args[0].as.f64);
    return true;
  }
  error = "bad operand type for unary -";
  return false;
}

bool op_pos(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "pos", error)) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool op_invert(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "invert", error)) {
    return false;
  }
  return value_invert(args[0], out, error);
}

bool op_index(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!expect_argc(argc, 1, "index", error)) {
    return false;
  }
  if (args[0].tag == ValueTag::Int64 || args[0].tag == ValueTag::Bool || value_as_bigint(args[0]) != nullptr) {
    value_assign_fast(out, args[0]);
    return true;
  }
  error = "'object' cannot be interpreted as an integer";
  return false;
}

bool op_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "call() expected at least one argument";
    return false;
  }
  return runtime_call_callable(runtime, args[0], args + 1, argc - 1, out, error);
}

bool op_length_hint(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "length_hint() expected object and optional default";
    return false;
  }
  std::string ignored;
  if (sequence_len(args[0], out, ignored)) {
    return true;
  }
  if (argc == 2) {
    value_assign_fast(out, args[1]);
  } else {
    out = Value::int64(0);
  }
  return true;
}

void add_alias(NativeModuleBuilder& builder, Runtime& runtime, const char* name, NativeFunctionCallback callback) {
  builder.value(name, runtime.make_native_function(name, callback));
}

} // namespace

void register_operator_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_operator");
  builder.value("__doc__", Value::string("Operator interface native primitives."))
      .function("lt", op_lt)
      .function("le", op_le)
      .function("eq", op_eq)
      .function("ne", op_ne)
      .function("ge", op_ge)
      .function("gt", op_gt)
      .function("add", op_add)
      .function("sub", op_sub)
      .function("mul", op_mul)
      .function("truediv", op_truediv)
      .function("floordiv", op_floordiv)
      .function("mod", op_mod)
      .function("pow", op_pow)
      .function("and_", op_and)
      .function("or_", op_or)
      .function("xor", op_xor)
      .function("lshift", op_lshift)
      .function("rshift", op_rshift)
      .function("concat", op_concat)
      .function("getitem", op_getitem)
      .function("setitem", op_setitem)
      .function("delitem", op_delitem)
      .function("contains", op_contains)
      .function("truth", op_truth)
      .function("not_", op_not)
      .function("is_", op_is)
      .function("is_not", op_is_not)
      .function("is_none", op_is_none)
      .function("is_not_none", op_is_not_none)
      .function("abs", op_abs)
      .function("neg", op_neg)
      .function("pos", op_pos)
      .function("inv", op_invert)
      .function("invert", op_invert)
      .function("index", op_index)
      .function("call", op_call)
      .function("length_hint", op_length_hint);

  add_alias(builder, runtime, "iadd", op_add);
  add_alias(builder, runtime, "isub", op_sub);
  add_alias(builder, runtime, "imul", op_mul);
  add_alias(builder, runtime, "itruediv", op_truediv);
  add_alias(builder, runtime, "ifloordiv", op_floordiv);
  add_alias(builder, runtime, "imod", op_mod);
  add_alias(builder, runtime, "ipow", op_pow);
  add_alias(builder, runtime, "iand", op_and);
  add_alias(builder, runtime, "ior", op_or);
  add_alias(builder, runtime, "ixor", op_xor);
  add_alias(builder, runtime, "ilshift", op_lshift);
  add_alias(builder, runtime, "irshift", op_rshift);
  add_alias(builder, runtime, "iconcat", op_concat);

  runtime.register_module("_operator", builder.finish());
}

} // namespace xlang3
