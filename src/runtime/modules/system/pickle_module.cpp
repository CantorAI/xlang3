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

#include "xlang3/module_object.h"

namespace xlang3 {

namespace {

bool picklebuffer_init(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool pickle_not_implemented(Runtime&, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "_pickle accelerator operation is not implemented yet";
  return false;
}

} // namespace

void register_pickle_module(Runtime& runtime) {
  Value exception_base = runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid();
  Value pickle_error = Value::class_object("PickleError", {}, exception_base);
  Value pickling_error = Value::class_object("PicklingError", {}, pickle_error);
  Value unpickling_error = Value::class_object("UnpicklingError", {}, pickle_error);

  std::vector<std::pair<std::string, Value>> buffer_attrs;
  buffer_attrs.push_back({"__init__", runtime.make_native_function("_pickle.PickleBuffer.__init__", picklebuffer_init)});

  NativeModuleBuilder builder(runtime, "_pickle");
  builder.value("PickleError", pickle_error)
      .value("PicklingError", pickling_error)
      .value("UnpicklingError", unpickling_error)
      .value("Pickler", Value::class_object("Pickler", {}))
      .value("Unpickler", Value::class_object("Unpickler", {}))
      .value("PickleBuffer", Value::class_object("PickleBuffer", std::move(buffer_attrs)))
      .function("dump", pickle_not_implemented)
      .function("dumps", pickle_not_implemented)
      .function("load", pickle_not_implemented)
      .function("loads", pickle_not_implemented);
  runtime.register_module("_pickle", builder.finish());
}

} // namespace xlang3
