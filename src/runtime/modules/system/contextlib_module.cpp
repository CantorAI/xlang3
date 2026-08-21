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
#include "xlang3/generator.h"
#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <string>

namespace xlang3 {

namespace {

constexpr const char* kGeneratorContextType = "contextlib._GeneratorContextManager";
constexpr const char* kNullContextType = "contextlib.nullcontext";

struct GeneratorContextState {
  Value generator;
  bool entered = false;
};

struct ContextManagerFactoryState {
  Value function;
  Value context_class;
};

struct NullContextState {
  Value enter_result;
};

void generator_context_cleanup(void* data) {
  delete static_cast<GeneratorContextState*>(data);
}

void context_factory_cleanup(void* data) {
  delete static_cast<ContextManagerFactoryState*>(data);
}

void null_context_cleanup(void* data) {
  delete static_cast<NullContextState*>(data);
}

bool return_self(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "context manager method expected self";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool generator_context_enter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "_GeneratorContextManager.__enter__() expected self";
    return false;
  }
  auto* state = static_cast<GeneratorContextState*>(instance_get_native_data(args[0], kGeneratorContextType));
  if (state == nullptr) {
    error = "invalid generator context manager";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  auto* generator = value_as_generator(state->generator);
  if (generator == nullptr) {
    error = "contextmanager function did not return a generator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool done = false;
  Interpreter interpreter(runtime);
  RuntimeResult result = interpreter.resume_generator(*generator, out, done);
  if (!result.errors.empty()) {
    error = result.errors.front();
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  if (done) {
    error = "generator didn't yield";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  state->entered = true;
  return true;
}

bool generator_context_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "_GeneratorContextManager.__exit__() expected self";
    return false;
  }
  auto* state = static_cast<GeneratorContextState*>(instance_get_native_data(args[0], kGeneratorContextType));
  if (state == nullptr) {
    error = "invalid generator context manager";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  (void)runtime;
  state->entered = false;
  out = Value::boolean(false);
  return true;
}

bool generator_context_factory_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<ContextManagerFactoryState*>(user_data);
  if (state == nullptr) {
    error = "invalid contextmanager factory";
    return false;
  }
  Value generator;
  if (!runtime_call_callable(runtime, state->function, args, argc, generator, error)) {
    return false;
  }
  if (value_as_generator(generator) == nullptr) {
    error = "contextmanager function did not return a generator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!object_construct(state->context_class, nullptr, 0, out, error)) {
    return false;
  }
  auto* context_state = new GeneratorContextState();
  context_state->generator = std::move(generator);
  if (!instance_set_native_data(out, kGeneratorContextType, context_state, generator_context_cleanup, error)) {
    delete context_state;
    return false;
  }
  return true;
}

bool contextmanager_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* context_class = static_cast<Value*>(user_data);
  if (context_class == nullptr || argc != 1) {
    error = "contextmanager() expected one function";
    return false;
  }
  auto* state = new ContextManagerFactoryState();
  state->function = args[0];
  state->context_class = *context_class;
  out = runtime.make_native_function(
      "contextlib.contextmanager.<factory>",
      generator_context_factory_call,
      state,
      context_factory_cleanup);
  return true;
}

bool nullcontext_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "nullcontext() expected optional enter_result";
    return false;
  }
  auto* state = new NullContextState();
  state->enter_result = argc == 2 ? args[1] : Value::none();
  if (!instance_set_native_data(args[0], kNullContextType, state, null_context_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool nullcontext_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "nullcontext.__enter__() expected self";
    return false;
  }
  auto* state = static_cast<NullContextState*>(instance_get_native_data(args[0], kNullContextType));
  if (state == nullptr) {
    value_set_none(out);
    return true;
  }
  value_assign_fast(out, state->enter_result);
  return true;
}

bool nullcontext_exit(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::boolean(false);
  return true;
}

Value make_generator_context_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__enter__", runtime.make_native_function("contextlib._GeneratorContextManager.__enter__", generator_context_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("contextlib._GeneratorContextManager.__exit__", generator_context_exit)});
  return Value::class_object("_GeneratorContextManager", std::move(attrs));
}

Value make_nullcontext_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("contextlib.nullcontext.__init__", nullcontext_init)});
  attrs.push_back({"__enter__", runtime.make_native_function("contextlib.nullcontext.__enter__", nullcontext_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("contextlib.nullcontext.__exit__", nullcontext_exit)});
  return Value::class_object("nullcontext", std::move(attrs));
}

} // namespace

void register_contextlib_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "contextlib");
  Value context_class = make_generator_context_class(runtime);
  auto* context_class_data = new Value(context_class);
  builder.value("_GeneratorContextManager", context_class)
      .value(
          "contextmanager",
          runtime.make_native_function(
              "contextlib.contextmanager",
              contextmanager_entry,
              context_class_data,
              [](void* data) { delete static_cast<Value*>(data); }))
      .value("nullcontext", make_nullcontext_class(runtime))
      .function("closing", return_self)
      .function("suppress", return_self);
  runtime.register_module("contextlib", builder.finish());
}

} // namespace xlang3
