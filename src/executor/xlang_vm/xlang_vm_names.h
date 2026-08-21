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

#include "xlang3/compiler.h"
#include "xlang3/object_model.h"
#include "xlang3/python_names.h"
#include "xlang3/runtime.h"
#include "xlang3/value.h"

#include <string>

namespace xlang3 {

struct XlangVMNames : PythonNames {};

XLANG3_HOT_INLINE bool xlang_vm_get_init_attr(const Value& callee, Value& out, std::string& error) {
  return object_get_attr(callee, XlangVMNames::init_method, out, error);
}

XLANG3_HOT_INLINE void xlang_vm_raise_type_error(Runtime& runtime, std::string message) {
  runtime.raise_class_error(XlangVMNames::type_error, std::move(message));
}

} // namespace xlang3
