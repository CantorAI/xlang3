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
#include "xlang3/native_trace_hook.h"
#include "xlang3/object_model.h"
#include "xlang3/functional_iterators.h"

namespace xlang3 {
bool instance_set_native_trace_hook(Value instance, NativeInstanceTraceHook hook,
                                     std::string& error) {
  auto* object = value_as_instance(instance);
  if (object == nullptr) { error = "object is not an instance"; return false; }
  object->native_trace_hook = hook;
  return true;
}
bool runtime_call_trace_hook(Runtime& runtime, const Value& hook,
                              const Value* args, uint32_t argc,
                              bool direct_delivery, uint8_t delivery_flags, Value& out,
                              std::string& error) {
  Value owner(hook);
  auto* object = value_as_instance(owner);
  const auto direct = !direct_delivery || object == nullptr ? nullptr : object->native_trace_hook;
  // A TLS installation-mode bit, not the instance's type alone, distinguishes
  // native start() from explicit sys.settrace(tracer)'s callable trampoline.
  // A native-hook error preserves the pending exception AND installed hook,
  // as the observed installed coverage CTracer does. No automatic uninstall.
  if (direct != nullptr)
    return direct(runtime, owner, args, argc, delivery_flags, out, error);
  return runtime_call_callable(runtime, owner, args, argc, out, error);
}
} // namespace xlang3
