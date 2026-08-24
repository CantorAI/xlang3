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
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3 {

namespace {

struct GetterState {
  std::vector<Value> items;
};

struct MethodCallerState {
  std::string name;
  std::vector<Value> args;
};

enum class BinaryKind : uintptr_t {
  Add = 1,
  Sub,
  Mul,
  Div,
  FloorDiv,
  Mod,
  Pow,
  BitAnd,
  BitOr,
  BitXor,
  ShiftLeft,
  ShiftRight,
};

void getter_cleanup(void* data) {
  delete static_cast<GetterState*>(data);
}

void methodcaller_cleanup(void* data) {
  delete static_cast<MethodCallerState*>(data);
}

bool operator_index(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "'operator.index' expected int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool operator_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.getitem() expected object and key";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sequence_get_item(args[0], args[1], out, error)) {
    if (value_as_instance(args[0]) != nullptr) {
      Value getitem;
      std::string attr_error;
      if (object_get_attr(args[0], "__getitem__", getitem, attr_error)) {
        if (runtime_call_callable(runtime, getitem, &args[1], 1, out, error)) {
          return true;
        }
      }
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool operator_setitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "operator.setitem() expected object, key, and value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target = args[0];
  if (!sequence_set_item(target, args[1], args[2], error)) {
    if (value_as_instance(args[0]) != nullptr) {
      Value setitem;
      std::string attr_error;
      if (object_get_attr(args[0], "__setitem__", setitem, attr_error)) {
        Value call_args[2] = {args[1], args[2]};
        Value ignored;
        if (runtime_call_callable(runtime, setitem, call_args, 2, ignored, error)) {
          value_set_none(out);
          return true;
        }
      }
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool operator_delitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.delitem() expected object and key";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target = args[0];
  if (!sequence_delete_item(target, args[1], error)) {
    if (value_as_instance(args[0]) != nullptr) {
      Value delitem;
      std::string attr_error;
      if (object_get_attr(args[0], "__delitem__", delitem, attr_error)) {
        Value ignored;
        if (runtime_call_callable(runtime, delitem, &args[1], 1, ignored, error)) {
          value_set_none(out);
          return true;
        }
      }
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_none(out);
  return true;
}

bool operator_truth(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "operator.truth() expected one argument";
    return false;
  }
  value_set_bool(out, value_truthy(args[0]));
  return true;
}

bool operator_not(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "operator.not_() expected one argument";
    return false;
  }
  value_set_bool(out, !value_truthy(args[0]));
  return true;
}

bool operator_is(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.is_() expected two arguments";
    return false;
  }
  value_set_bool(out, value_is(args[0], args[1]));
  return true;
}

bool operator_is_not(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.is_not() expected two arguments";
    return false;
  }
  value_set_bool(out, !value_is(args[0], args[1]));
  return true;
}

bool operator_contains(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.contains() expected container and item";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool contains = false;
  if (!value_contains(args[0], args[1], contains, error)) {
    if (value_as_instance(args[0]) != nullptr) {
      Value contains_method;
      std::string attr_error;
      if (object_get_attr(args[0], "__contains__", contains_method, attr_error)) {
        Value result;
        if (runtime_call_callable(runtime, contains_method, &args[1], 1, result, error)) {
          value_set_bool(out, value_truthy(result));
          return true;
        }
      }
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_bool(out, contains);
  return true;
}

bool operator_length_hint(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "operator.length_hint() expected object and optional default";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (sequence_len(args[0], out, error)) {
    return true;
  }
  if (value_as_instance(args[0]) != nullptr) {
    Value len_method;
    std::string attr_error;
    if (object_get_attr(args[0], "__len__", len_method, attr_error) &&
        runtime_call_callable(runtime, len_method, nullptr, 0, out, error)) {
      return true;
    }
  }
  if (argc == 2) {
    value_assign_fast(out, args[1]);
    return true;
  }
  value_set_int64(out, 0);
  return true;
}

bool operator_count_of(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.countOf() expected sequence and item";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t count = 0;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      break;
    }
    Value equal;
    if (!value_compare("==", item, args[1], equal, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (value_truthy(equal)) {
      ++count;
    }
  }
  value_set_int64(out, count);
  return true;
}

bool operator_index_of(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.indexOf() expected sequence and item";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t index = 0;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      error = "sequence.index(x): x not in sequence";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    Value equal;
    if (!value_compare("==", item, args[1], equal, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (value_truthy(equal)) {
      value_set_int64(out, index);
      return true;
    }
    ++index;
  }
}

bool binary_value_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "operator binary function expected two arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool ok = false;
  switch (static_cast<BinaryKind>(reinterpret_cast<uintptr_t>(user_data))) {
    case BinaryKind::Add: ok = value_add(args[0], args[1], out, error); break;
    case BinaryKind::Sub: ok = value_sub(args[0], args[1], out, error); break;
    case BinaryKind::Mul: ok = value_mul(args[0], args[1], out, error); break;
    case BinaryKind::Div: ok = value_div(args[0], args[1], out, error); break;
    case BinaryKind::FloorDiv: ok = value_floor_div(args[0], args[1], out, error); break;
    case BinaryKind::Mod: ok = value_mod(args[0], args[1], out, error); break;
    case BinaryKind::Pow: ok = value_pow(args[0], args[1], out, error); break;
    case BinaryKind::BitAnd: ok = value_bit_and(args[0], args[1], out, error); break;
    case BinaryKind::BitOr: ok = value_bit_or(args[0], args[1], out, error); break;
    case BinaryKind::BitXor: ok = value_bit_xor(args[0], args[1], out, error); break;
    case BinaryKind::ShiftLeft: ok = value_shift_left(args[0], args[1], out, error); break;
    case BinaryKind::ShiftRight: ok = value_shift_right(args[0], args[1], out, error); break;
    default: error = "operator binary function is not implemented"; break;
  }
  if (!ok) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool operator_neg(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || !value_sub(Value::int64(0), args[0], out, error)) {
    error = error.empty() ? "operator.neg() expected one numeric argument" : error;
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool operator_pos(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "operator.pos() expected one numeric argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool operator_invert_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || !value_invert(args[0], out, error)) {
    error = error.empty() ? "operator.invert() expected one integer argument" : error;
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool compare_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "operator comparison expected two arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const char* op = static_cast<const char*>(user_data);
  if (!value_compare(op == nullptr ? "==" : op, args[0], args[1], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool attrgetter_call(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<GetterState*>(user_data);
  if (state == nullptr || argc != 1) {
    error = "attrgetter expected one object";
    return false;
  }
  std::vector<Value> values;
  values.reserve(state->items.size());
  for (const auto& name_value : state->items) {
    auto* name = value_as_string(name_value);
    if (name == nullptr) {
      error = "attribute name must be str";
      return false;
    }
    Value current = args[0];
    std::string remaining = string_object_to_string(*name);
    size_t start = 0;
    for (;;) {
      const size_t dot = remaining.find('.', start);
      const std::string part = remaining.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
      Value next;
      if (!object_get_attr(current, part, next, error)) {
        return false;
      }
      current = std::move(next);
      if (dot == std::string::npos) {
        break;
      }
      start = dot + 1;
    }
    values.push_back(std::move(current));
  }
  if (values.size() == 1) {
    value_assign_fast(out, values[0]);
  } else {
    out = Value::tuple(std::move(values));
  }
  return true;
}

bool attrgetter_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "attrgetter expected at least one attribute name";
    return false;
  }
  auto* state = new GetterState();
  for (uint32_t i = 0; i < argc; ++i) {
    if (value_as_string(args[i]) == nullptr) {
      delete state;
      error = "attribute name must be str";
      return false;
    }
    state->items.push_back(args[i]);
  }
  out = runtime.make_native_function("operator.attrgetter.<call>", attrgetter_call, state, getter_cleanup);
  return true;
}

bool itemgetter_call(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<GetterState*>(user_data);
  if (state == nullptr || argc != 1) {
    error = "itemgetter expected one object";
    return false;
  }
  std::vector<Value> values;
  values.reserve(state->items.size());
  for (const auto& key : state->items) {
    Value item;
    if (!sequence_get_item(args[0], key, item, error)) {
      return false;
    }
    values.push_back(std::move(item));
  }
  if (values.size() == 1) {
    value_assign_fast(out, values[0]);
  } else {
    out = Value::tuple(std::move(values));
  }
  return true;
}

bool itemgetter_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "itemgetter expected at least one item";
    return false;
  }
  auto* state = new GetterState();
  for (uint32_t i = 0; i < argc; ++i) {
    state->items.push_back(args[i]);
  }
  out = runtime.make_native_function("operator.itemgetter.<call>", itemgetter_call, state, getter_cleanup);
  return true;
}

bool methodcaller_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<MethodCallerState*>(user_data);
  if (state == nullptr || argc != 1) {
    error = "methodcaller expected one object";
    return false;
  }
  Value method;
  if (!object_get_attr(args[0], state->name, method, error)) {
    return false;
  }
  return runtime_call_callable(
      runtime,
      method,
      state->args.empty() ? nullptr : state->args.data(),
      static_cast<uint32_t>(state->args.size()),
      out,
      error);
}

bool methodcaller_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "methodcaller expected a method name";
    return false;
  }
  auto* name = value_as_string(args[0]);
  if (name == nullptr) {
    error = "method name must be str";
    return false;
  }
  auto* state = new MethodCallerState();
  state->name = string_object_to_string(*name);
  state->args.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    state->args.push_back(args[i]);
  }
  out = runtime.make_native_function("operator.methodcaller.<call>", methodcaller_call, state, methodcaller_cleanup);
  return true;
}

} // namespace

void register_operator_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "operator");
  auto binary_tag = [](BinaryKind kind) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(kind));
  };
  builder.function("index", operator_index)
      .function("getitem", operator_getitem)
      .function("setitem", operator_setitem)
      .function("delitem", operator_delitem)
      .function("truth", operator_truth)
      .function("not_", operator_not)
      .function("is_", operator_is)
      .function("is_not", operator_is_not)
      .function("contains", operator_contains)
      .function("length_hint", operator_length_hint)
      .function("countOf", operator_count_of)
      .function("indexOf", operator_index_of)
      .value("add", runtime.make_native_function("operator.add", binary_value_entry, binary_tag(BinaryKind::Add)))
      .value("__add__", runtime.make_native_function("operator.__add__", binary_value_entry, binary_tag(BinaryKind::Add)))
      .value("concat", runtime.make_native_function("operator.concat", binary_value_entry, binary_tag(BinaryKind::Add)))
      .value("iadd", runtime.make_native_function("operator.iadd", binary_value_entry, binary_tag(BinaryKind::Add)))
      .value("iconcat", runtime.make_native_function("operator.iconcat", binary_value_entry, binary_tag(BinaryKind::Add)))
      .value("sub", runtime.make_native_function("operator.sub", binary_value_entry, binary_tag(BinaryKind::Sub)))
      .value("isub", runtime.make_native_function("operator.isub", binary_value_entry, binary_tag(BinaryKind::Sub)))
      .value("mul", runtime.make_native_function("operator.mul", binary_value_entry, binary_tag(BinaryKind::Mul)))
      .value("imul", runtime.make_native_function("operator.imul", binary_value_entry, binary_tag(BinaryKind::Mul)))
      .value("truediv", runtime.make_native_function("operator.truediv", binary_value_entry, binary_tag(BinaryKind::Div)))
      .value("itruediv", runtime.make_native_function("operator.itruediv", binary_value_entry, binary_tag(BinaryKind::Div)))
      .value("floordiv", runtime.make_native_function("operator.floordiv", binary_value_entry, binary_tag(BinaryKind::FloorDiv)))
      .value("ifloordiv", runtime.make_native_function("operator.ifloordiv", binary_value_entry, binary_tag(BinaryKind::FloorDiv)))
      .value("mod", runtime.make_native_function("operator.mod", binary_value_entry, binary_tag(BinaryKind::Mod)))
      .value("imod", runtime.make_native_function("operator.imod", binary_value_entry, binary_tag(BinaryKind::Mod)))
      .value("pow", runtime.make_native_function("operator.pow", binary_value_entry, binary_tag(BinaryKind::Pow)))
      .value("ipow", runtime.make_native_function("operator.ipow", binary_value_entry, binary_tag(BinaryKind::Pow)))
      .value("and_", runtime.make_native_function("operator.and_", binary_value_entry, binary_tag(BinaryKind::BitAnd)))
      .value("iand", runtime.make_native_function("operator.iand", binary_value_entry, binary_tag(BinaryKind::BitAnd)))
      .value("or_", runtime.make_native_function("operator.or_", binary_value_entry, binary_tag(BinaryKind::BitOr)))
      .value("ior", runtime.make_native_function("operator.ior", binary_value_entry, binary_tag(BinaryKind::BitOr)))
      .value("xor", runtime.make_native_function("operator.xor", binary_value_entry, binary_tag(BinaryKind::BitXor)))
      .value("ixor", runtime.make_native_function("operator.ixor", binary_value_entry, binary_tag(BinaryKind::BitXor)))
      .value("lshift", runtime.make_native_function("operator.lshift", binary_value_entry, binary_tag(BinaryKind::ShiftLeft)))
      .value("ilshift", runtime.make_native_function("operator.ilshift", binary_value_entry, binary_tag(BinaryKind::ShiftLeft)))
      .value("rshift", runtime.make_native_function("operator.rshift", binary_value_entry, binary_tag(BinaryKind::ShiftRight)))
      .value("irshift", runtime.make_native_function("operator.irshift", binary_value_entry, binary_tag(BinaryKind::ShiftRight)))
      .function("neg", operator_neg)
      .function("pos", operator_pos)
      .function("invert", operator_invert_entry)
      .function("inv", operator_invert_entry)
      .value("eq", runtime.make_native_function("operator.eq", compare_entry, const_cast<char*>("==")))
      .value("ne", runtime.make_native_function("operator.ne", compare_entry, const_cast<char*>("!=")))
      .value("lt", runtime.make_native_function("operator.lt", compare_entry, const_cast<char*>("<")))
      .value("le", runtime.make_native_function("operator.le", compare_entry, const_cast<char*>("<=")))
      .value("gt", runtime.make_native_function("operator.gt", compare_entry, const_cast<char*>(">")))
      .value("ge", runtime.make_native_function("operator.ge", compare_entry, const_cast<char*>(">=")))
      .function("attrgetter", attrgetter_entry)
      .function("itemgetter", itemgetter_entry)
      .function("methodcaller", methodcaller_entry);
  runtime.register_module("operator", builder.finish());
}

} // namespace xlang3
