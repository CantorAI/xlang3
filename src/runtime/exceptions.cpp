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
#include "xlang3/exceptions.h"

#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

namespace xlang3 {

Value& runtime_current_exception_state(const Runtime& runtime);
void runtime_publish_current_exception_state(const Runtime& runtime);

bool is_exception_class_name(const std::string& name) {
  return name == "BaseException" ||
         name == "Exception" ||
         name == "RuntimeError" ||
         name == "TypeError" ||
         name == "ValueError" ||
         name == "ImportError" ||
         name == "BaseExceptionGroup" ||
         name == "ExceptionGroup" ||
         name == "GeneratorExit" ||
         name == "KeyboardInterrupt" ||
         name == "StopAsyncIteration" ||
         name == "StopIteration" ||
         name == "SystemExit" ||
         name == "Error" ||
         name == "DatabaseError" ||
         name == "OperationalError" ||
         name == "ProgrammingError" ||
         (name.size() > 5 && name.compare(name.size() - 5, 5, "Error") == 0) ||
         (name.size() > 9 && name.compare(name.size() - 9, 9, "Exception") == 0);
}

Value Runtime::make_exception(std::string class_name, std::string message) {
  const Value* class_value = find_builtin(class_name);
  if (class_value == nullptr || value_as_class(*class_value) == nullptr) {
    class_value = find_builtin("RuntimeError");
  }
  if (class_value == nullptr || value_as_class(*class_value) == nullptr) {
    return Value::string(std::move(message));
  }
  Value instance = Value::instance(*class_value);
  Value message_value = Value::string(std::move(message));
  std::string ignored;
  object_set_attr(instance, "message", message_value, ignored);
  object_set_attr(instance, "args", Value::tuple({message_value}), ignored);
  object_set_attr(instance, "__traceback__", Value::none(), ignored);
  object_set_attr(instance, "__cause__", Value::none(), ignored);
  object_set_attr(instance, "__context__", Value::none(), ignored);
  object_set_attr(instance, "__suppress_context__", Value::boolean(false), ignored);
  return instance;
}

Value Runtime::make_exception_from_class(Value klass, std::string message) {
  if (value_as_class(klass) == nullptr) {
    return make_exception("RuntimeError", std::move(message));
  }
  Value instance = Value::instance(std::move(klass));
  Value message_value = Value::string(std::move(message));
  std::string ignored;
  object_set_attr(instance, "message", message_value, ignored);
  object_set_attr(instance, "args", Value::tuple({message_value}), ignored);
  object_set_attr(instance, "__traceback__", Value::none(), ignored);
  object_set_attr(instance, "__cause__", Value::none(), ignored);
  object_set_attr(instance, "__context__", Value::none(), ignored);
  object_set_attr(instance, "__suppress_context__", Value::boolean(false), ignored);
  return instance;
}

Value Runtime::exception_type(const Value& exception) {
  if (auto* instance = value_as_instance(exception)) {
    return instance->klass;
  }
  const Value* runtime_error = find_builtin("RuntimeError");
  return runtime_error == nullptr ? Value::none() : *runtime_error;
}

bool Runtime::raise_class_error(std::string class_name, std::string message) {
  pending_exception_ = make_exception(std::move(class_name), std::move(message));
  return true;
}

void Runtime::set_pending_exception(Value exception) {
  pending_exception_ = std::move(exception);
}

bool Runtime::take_pending_exception(Value& out) {
  if (pending_exception_.tag == ValueTag::Invalid) {
    return false;
  }
  value_assign_fast(out, pending_exception_);
  value_set_invalid(pending_exception_);
  return true;
}

void Runtime::set_active_exception(Value exception) {
  runtime_current_exception_state(*this) = std::move(exception);
  runtime_publish_current_exception_state(*this);
}

void Runtime::clear_active_exception() {
  value_set_invalid(runtime_current_exception_state(*this));
  runtime_publish_current_exception_state(*this);
}

const Value& Runtime::active_exception() const {
  return runtime_current_exception_state(*this);
}

} // namespace xlang3
