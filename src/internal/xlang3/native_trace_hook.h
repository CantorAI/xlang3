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
#include <cstdint>
#include <string>

namespace xlang3 {
constexpr uint8_t kNativeTraceResume = 1;
constexpr uint8_t kNativeTraceSuspended = 2;
// Only the VM's trace emitter invokes this entry point. Ordinary __call__
// remains ordinary callable dispatch, including calls made from a callback.
using NativeInstanceTraceHook = bool (*)(
    Runtime&, const Value& self, const Value* args, uint32_t argc,
    uint8_t delivery_flags, Value& out, std::string& error);
bool instance_set_native_trace_hook(Value instance, NativeInstanceTraceHook hook,
                                     std::string& error);
bool runtime_call_trace_hook(Runtime& runtime, const Value& hook,
                              const Value* args, uint32_t argc,
                              bool direct_delivery, uint8_t delivery_flags, Value& out,
                              std::string& error);
} // namespace xlang3
