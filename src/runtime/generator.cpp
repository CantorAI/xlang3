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
#include "xlang3/generator.h"

#include "xlang3/interpreter.h"

namespace xlang3 {

namespace {

template <typename T>
T* allocate_generator_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

} // namespace

Value Value::generator(Runtime* runtime, Value function, std::vector<Value> args) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_generator_object<GeneratorObject>(ObjectKind::Generator);
  obj->runtime = runtime;
  obj->function = std::move(function);
  obj->args = std::move(args);
  v.as.obj = &obj->header;
  return v;
}

void generator_release_object(Object* object) {
  delete reinterpret_cast<GeneratorObject*>(object);
}

std::string generator_to_string(const Value&) {
  return "<generator object>";
}

bool generator_truthy(const Value&) {
  return true;
}

bool generator_get_iter(const Value& generator, Value& out, std::string& error) {
  if (value_as_generator(generator) == nullptr) {
    error = "object is not a generator";
    return false;
  }
  value_assign_fast(out, generator);
  return true;
}

bool generator_iter_next(Value& generator, bool& done, Value& out, std::string& error) {
  auto* obj = value_as_generator(generator);
  if (obj == nullptr) {
    error = "invalid generator";
    return false;
  }
  if (!obj->materialized) {
    auto* function = value_as_function(obj->function);
    if (function == nullptr || obj->runtime == nullptr) {
      error = "generator has invalid function";
      return false;
    }
    CallArgsView args;
    args.leading = obj->args.data();
    args.leading_count = static_cast<uint32_t>(obj->args.size());
    Interpreter interpreter(*obj->runtime);
    RuntimeResult result = interpreter.collect_generator_values(function, args, obj->yielded);
    obj->materialized = true;
    if (!result.errors.empty()) {
      error = result.errors.front();
      return false;
    }
  }
  if (obj->index >= obj->yielded.size()) {
    done = true;
    obj->done = true;
    value_set_none(out);
    return true;
  }
  value_assign_fast(out, obj->yielded[obj->index++]);
  done = false;
  return true;
}

} // namespace xlang3
