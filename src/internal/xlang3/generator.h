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

#include <functional>
#include <string>
#include <vector>

namespace xlang3 {

struct GeneratorObject {
  Object header;
  Runtime* runtime = nullptr;
  Value function;
  std::vector<Value> args;
  // Non-owning parent link for a coroutine whose fresh frame is executing on
  // its caller's VM stack. The caller's frame register owns the child Value.
  GeneratorObject* inline_parent = nullptr;
  void* vm_state = nullptr;
  void (*vm_state_cleanup)(void*) = nullptr;
  // Keep a yielded VM continuation allocation attached while the generator is
  // running again, so the next yield can reuse it instead of allocating one
  // state object per produced item.
  void* vm_state_reuse = nullptr;
  void (*vm_state_reuse_cleanup)(void*) = nullptr;
  Value pending_send;
  Value pending_throw;
  Value return_value;
  Value awaiting;
  Value origin;
  bool has_pending_send = false;
  bool has_pending_throw = false;
  bool delegated_result_ready = false;
  bool args_bound = false;
  bool started = false;
  bool running = false;
  bool is_async = false;
  bool is_coroutine = false;
  bool is_await_iterator = false;
  // Native any() may keep an unobservable generator running across false
  // yields. This flag is set only for the duration of that guarded consume.
  bool consume_for_any = false;
  // Native sum() can absorb exact-int yields into its active accumulator and
  // keep the generator frame running. The pointer is valid only inside sum().
  int64_t* consume_int_sum = nullptr;
  // A saved continuation may carry per-frame trace/monitoring hooks even
  // after the runtime-wide hooks are disabled. Keep delegation trampolining
  // off for such a generator so every observable yield still runs normally.
  bool has_observed_continuation = false;
  bool has_active_suspended_exception_handlers = false;
  // A trampoline injects a completed child's StopIteration.value into this
  // saved yield-from frame before resuming it.
  bool delegation_trampoline_result_ready = false;
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
  bool started = false;
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
bool generator_begin_any_consume(GeneratorObject& generator);
void generator_end_any_consume(GeneratorObject& generator);
bool generator_begin_int_sum_consume(GeneratorObject& generator, int64_t* accumulator);
void generator_end_int_sum_consume(GeneratorObject& generator);
bool generator_iter_next(Value& generator, bool& done, Value& out, std::string& error);
bool generator_send(Value& generator, Value value, bool& done, Value& out, std::string& error);
bool generator_close(Value& generator, Value& out, std::string& error);
bool generator_throw(Value& generator, const Value* args, uint32_t argc, Value& out, std::string& error);
bool generator_vm_frame_snapshot(const GeneratorObject& generator, Value& out);
void generator_vm_visit_references(
    const GeneratorObject& generator,
    const std::function<void(const Value&)>& visit);
void frame_set_generator_owner(Runtime& runtime, Value& frame,
                               const GeneratorObject& generator);
bool generator_get_method(const Value& object, const std::string& name, Value& out);
bool async_generator_awaitable_await(Runtime& runtime, const Value& value, Value& out, std::string& error);
bool async_generator_awaitable_send(
    Runtime& runtime,
    const Value& value,
    Value send_value,
    bool& done,
    Value& out,
    std::string& error);

} // namespace xlang3
