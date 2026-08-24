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

namespace xlang3 {

namespace {

struct PartialState {
  Value callable;
  std::vector<Value> bound_args;
};

struct WrapsState {
  Value wrapped;
};

struct CmpToKeyClassState {
  Value cmp;
};

struct CmpToKeyObjectState {
  Value object;
  Value cmp;
};

enum class TotalOrderingOp : uintptr_t {
  Le = 1,
  Gt = 2,
  Ge = 3,
};

void wraps_cleanup(void* data) {
  delete static_cast<WrapsState*>(data);
}

void partial_cleanup(void* data) {
  delete static_cast<PartialState*>(data);
}

void cmp_to_key_object_cleanup(void* data) {
  delete static_cast<CmpToKeyObjectState*>(data);
}

CmpToKeyObjectState* cmp_to_key_object_state(const Value& self, std::string& error) {
  auto* state = static_cast<CmpToKeyObjectState*>(instance_get_native_data(self, "functools.cmp_to_key.KeyWrapper"));
  if (state == nullptr) {
    error = "invalid cmp_to_key object";
  }
  return state;
}

bool partial_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<PartialState*>(user_data);
  if (state == nullptr) {
    error = "invalid functools.partial object";
    return false;
  }
  std::vector<Value> call_args;
  call_args.reserve(state->bound_args.size() + argc);
  for (const auto& arg : state->bound_args) {
    call_args.push_back(arg);
  }
  for (uint32_t i = 0; i < argc; ++i) {
    call_args.push_back(args[i]);
  }
  return runtime_call_callable(
      runtime,
      state->callable,
      call_args.empty() ? nullptr : call_args.data(),
      static_cast<uint32_t>(call_args.size()),
      out,
      error);
}

bool partial_call_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return partial_call(runtime, args, argc, out, error, user_data);
}

bool partial_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "functools.partial() expected callable";
    return false;
  }
  auto* state = new PartialState();
  state->callable = args[0];
  state->bound_args.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    state->bound_args.push_back(args[i]);
  }
  out = runtime.make_native_function(
      "functools.partial.<call>",
      partial_call,
      state,
      partial_cleanup,
      nullptr,
      false,
      partial_call_kw);
  return true;
}

bool reduce_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "functools.reduce() expected function, iterable, and optional initializer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  bool done = false;
  Value accumulator;
  if (argc == 3) {
    value_assign_fast(accumulator, args[2]);
  } else {
    if (!sequence_iter_next(iterator, done, accumulator, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      error = "reduce() of empty iterable with no initial value";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }

  for (;;) {
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      value_assign_fast(out, accumulator);
      return true;
    }
    Value call_args[2] = {accumulator, item};
    Value next;
    if (!runtime_call_callable(runtime, args[0], call_args, 2, next, error)) {
      return false;
    }
    value_assign_fast(accumulator, next);
  }
}

bool cmp_to_key_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "cmp_to_key key wrapper expected one object";
    return false;
  }
  auto* class_state = static_cast<CmpToKeyClassState*>(user_data);
  if (class_state == nullptr) {
    error = "invalid cmp_to_key class";
    return false;
  }
  auto* state = new CmpToKeyObjectState();
  state->object = args[1];
  state->cmp = class_state->cmp;
  if (!instance_set_native_data(args[0], "functools.cmp_to_key.KeyWrapper", state, cmp_to_key_object_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool cmp_to_key_compare(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "cmp_to_key comparison expected other";
    return false;
  }
  auto* lhs = cmp_to_key_object_state(args[0], error);
  auto* rhs = cmp_to_key_object_state(args[1], error);
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  Value cmp_args[2] = {lhs->object, rhs->object};
  Value result;
  if (!runtime_call_callable(runtime, lhs->cmp, cmp_args, 2, result, error)) {
    return false;
  }
  if (result.tag != ValueTag::Int64) {
    error = "cmp_to_key comparison function must return int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const intptr_t op = reinterpret_cast<intptr_t>(user_data);
  bool ok = false;
  switch (op) {
    case -2: ok = result.as.i64 < 0; break;
    case -1: ok = result.as.i64 <= 0; break;
    case 0: ok = result.as.i64 == 0; break;
    case 1: ok = result.as.i64 > 0; break;
    case 2: ok = result.as.i64 >= 0; break;
    default: ok = result.as.i64 != 0; break;
  }
  value_set_bool(out, ok);
  return true;
}

bool cmp_to_key_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "functools.cmp_to_key() expected a comparison function";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = new CmpToKeyClassState();
  state->cmp = args[0];
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("functools")});
  attrs.push_back({"__init__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__init__", cmp_to_key_init, state)});
  attrs.push_back({"__lt__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__lt__", cmp_to_key_compare, reinterpret_cast<void*>(-2))});
  attrs.push_back({"__le__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__le__", cmp_to_key_compare, reinterpret_cast<void*>(-1))});
  attrs.push_back({"__eq__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__eq__", cmp_to_key_compare, reinterpret_cast<void*>(0))});
  attrs.push_back({"__gt__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__gt__", cmp_to_key_compare, reinterpret_cast<void*>(1))});
  attrs.push_back({"__ge__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__ge__", cmp_to_key_compare, reinterpret_cast<void*>(2))});
  attrs.push_back({"__ne__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__ne__", cmp_to_key_compare, reinterpret_cast<void*>(3))});
  out = Value::class_object("KeyWrapper", std::move(attrs));
  return true;
}

bool call_self_compare(
    Runtime& runtime,
    const Value& self,
    const char* method,
    const Value& other,
    Value& out,
    std::string& error) {
  Value callable;
  if (!object_get_attr(self, method, callable, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return runtime_call_callable(runtime, callable, &other, 1, out, error);
}

bool total_ordering_compare(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "total_ordering comparison expected other";
    return false;
  }
  Value lt;
  if (!call_self_compare(runtime, args[0], "__lt__", args[1], lt, error)) {
    return false;
  }
  const auto op = static_cast<TotalOrderingOp>(reinterpret_cast<uintptr_t>(user_data));
  if (op == TotalOrderingOp::Gt) {
    if (value_truthy(lt)) {
      value_set_bool(out, false);
      return true;
    }
    Value eq;
    if (!call_self_compare(runtime, args[0], "__eq__", args[1], eq, error)) {
      return false;
    }
    value_set_bool(out, !value_truthy(eq));
    return true;
  }
  if (op == TotalOrderingOp::Le) {
    if (value_truthy(lt)) {
      value_set_bool(out, true);
      return true;
    }
    Value eq;
    if (!call_self_compare(runtime, args[0], "__eq__", args[1], eq, error)) {
      return false;
    }
    value_set_bool(out, value_truthy(eq));
    return true;
  }
  Value eq;
  if (!call_self_compare(runtime, args[0], "__eq__", args[1], eq, error)) {
    return false;
  }
  value_set_bool(out, value_truthy(lt) || value_truthy(eq));
  return true;
}

bool class_has_local_attr(const Value& klass, const char* name) {
  Value ignored;
  std::string error;
  return object_get_attr(klass, name, ignored, error);
}

bool total_ordering_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_class(args[0]) == nullptr) {
    error = "functools.total_ordering() expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value klass = args[0];
  if (!class_has_local_attr(klass, "__le__")) {
    if (!object_set_attr(
            klass,
            "__le__",
            runtime.make_native_function("functools.total_ordering.__le__", total_ordering_compare, reinterpret_cast<void*>(static_cast<uintptr_t>(TotalOrderingOp::Le))),
            error)) {
      return false;
    }
  }
  if (!class_has_local_attr(klass, "__gt__")) {
    if (!object_set_attr(
            klass,
            "__gt__",
            runtime.make_native_function("functools.total_ordering.__gt__", total_ordering_compare, reinterpret_cast<void*>(static_cast<uintptr_t>(TotalOrderingOp::Gt))),
            error)) {
      return false;
    }
  }
  if (!class_has_local_attr(klass, "__ge__")) {
    if (!object_set_attr(
            klass,
            "__ge__",
            runtime.make_native_function("functools.total_ordering.__ge__", total_ordering_compare, reinterpret_cast<void*>(static_cast<uintptr_t>(TotalOrderingOp::Ge))),
            error)) {
      return false;
    }
  }
  value_assign_fast(out, klass);
  return true;
}

bool copy_attr_if_present(Value& wrapper, const Value& wrapped, const char* name, std::string& error) {
  Value attr;
  std::string ignored;
  if (!object_get_attr(wrapped, name, attr, ignored)) {
    return true;
  }
  return object_set_attr(wrapper, name, attr, error);
}

bool update_wrapper_common(Value wrapper, const Value& wrapped, Value& out, std::string& error) {
  if (!copy_attr_if_present(wrapper, wrapped, "__module__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__name__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__qualname__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__doc__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__annotations__", error)) {
    return false;
  }
  if (!object_set_attr(wrapper, "__wrapped__", wrapped, error)) {
    return false;
  }
  value_assign_fast(out, wrapper);
  return true;
}

bool update_wrapper_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "update_wrapper() expected wrapper and wrapped";
    return false;
  }
  return update_wrapper_common(args[0], args[1], out, error);
}

bool wraps_apply(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "wraps decorator expected one function";
    return false;
  }
  auto* state = static_cast<WrapsState*>(user_data);
  if (state == nullptr) {
    error = "invalid functools.wraps decorator";
    return false;
  }
  return update_wrapper_common(args[0], state->wrapped, out, error);
}

bool wraps_entry(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "wraps() expected wrapped function";
    return false;
  }
  auto* state = new WrapsState();
  state->wrapped = args[0];
  out = runtime.make_native_function("functools.wraps.<decorator>", wraps_apply, state, wraps_cleanup);
  return true;
}

bool wraps_entry_positional(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return wraps_entry(runtime, args, argc, nullptr, 0, out, error, nullptr);
}

bool identity_decorator_factory(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 1 && (value_as_function(args[0]) != nullptr || value_as_native_function(args[0]) != nullptr)) {
    value_assign_fast(out, args[0]);
    return true;
  }
  if (argc > 1) {
    error = "decorator expected optional function";
    return false;
  }
  out = runtime.make_native_function("functools.identity_decorator.<decorator>", wraps_apply);
  return true;
}

} // namespace

void register_functools_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "functools");
  builder.function("update_wrapper", update_wrapper_entry)
      .value(
          "wraps",
          runtime.make_native_function(
              "functools.wraps",
              wraps_entry_positional,
              nullptr,
              nullptr,
              nullptr,
              false,
              wraps_entry))
      .function("lru_cache", identity_decorator_factory)
      .function("cache", identity_decorator_factory)
      .function("partial", partial_entry)
      .function("reduce", reduce_entry)
      .function("cmp_to_key", cmp_to_key_entry)
      .function("total_ordering", total_ordering_entry);
  runtime.register_module("functools", builder.finish());
}

} // namespace xlang3
