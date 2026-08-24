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

#include "xlang3/value.h"

#include <string>
#include <vector>

namespace xlang3 {

struct GeneratorObject {
  Object header;
  Runtime* runtime = nullptr;
  Value function;
  std::vector<Value> args;
  void* vm_state = nullptr;
  void (*vm_state_cleanup)(void*) = nullptr;
  Value pending_send;
  Value pending_throw;
  Value return_value;
  bool has_pending_send = false;
  bool has_pending_throw = false;
  bool started = false;
  bool is_async = false;
  bool done = false;
};

enum class AsyncGenAwaitableKind : uint8_t {
  ANext,
  ASend,
  AThrow,
  AClose,
};

struct AsyncGenAwaitableObject {
  Object header;
  AsyncGenAwaitableKind kind = AsyncGenAwaitableKind::ANext;
  Value generator;
  std::vector<Value> args;
  bool consumed = false;
};

XLANG3_HOT_INLINE GeneratorObject* value_as_generator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Generator) {
    return nullptr;
  }
  return reinterpret_cast<GeneratorObject*>(value.as.obj);
}

XLANG3_HOT_INLINE AsyncGenAwaitableObject* value_as_async_generator_awaitable(const Value& value) {
  if (value.tag != ValueTag::Object ||
      value.as.obj == nullptr ||
      value.as.obj->kind != ObjectKind::AsyncGeneratorAwaitable) {
    return nullptr;
  }
  return reinterpret_cast<AsyncGenAwaitableObject*>(value.as.obj);
}

void generator_release_object(Object* object);
std::string generator_to_string(const Value& value);
bool generator_truthy(const Value& value);
bool generator_get_iter(const Value& generator, Value& out, std::string& error);
bool generator_iter_next(Value& generator, bool& done, Value& out, std::string& error);
bool generator_send(Value& generator, Value value, bool& done, Value& out, std::string& error);
bool generator_close(Value& generator, Value& out, std::string& error);
bool generator_throw(Value& generator, const Value* args, uint32_t argc, Value& out, std::string& error);
bool generator_get_method(const Value& object, const std::string& name, Value& out);
bool async_generator_awaitable_await(Runtime& runtime, const Value& value, Value& out, std::string& error);

} // namespace xlang3
