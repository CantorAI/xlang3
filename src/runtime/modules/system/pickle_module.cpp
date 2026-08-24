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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <string>
#include <string_view>

namespace xlang3 {

namespace {

bool picklebuffer_init(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool marshal_function(Runtime& runtime, const char* name, Value& out, std::string& error) {
  Value marshal_module;
  if (!mapping_get_item(runtime.module_registry_dict(), Value::string("marshal"), marshal_module, error)) {
    error = "marshal module is not registered";
    return false;
  }
  return module_get_attr(marshal_module, name, out, error);
}

bool get_bytes_view(const Value& value, std::string_view& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  error = "pickle.loads() expected bytes-like object";
  return false;
}

bool pickle_dumps(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "pickle.dumps() expected object and optional protocol";
    return false;
  }
  Value dumps;
  if (!marshal_function(runtime, "dumps", dumps, error)) {
    return false;
  }
  Value marshaled;
  if (!runtime_call_callable(runtime, dumps, args, 1, marshaled, error)) {
    return false;
  }
  auto* bytes = value_as_bytes(marshaled);
  if (bytes == nullptr) {
    error = "marshal.dumps() did not return bytes";
    return false;
  }
  std::string payload = "X3P1";
  payload += bytes_object_view(*bytes);
  out = Value::bytes(std::move(payload));
  return true;
}

bool pickle_loads(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pickle.loads() expected data";
    return false;
  }
  std::string_view payload;
  if (!get_bytes_view(args[0], payload, error)) {
    return false;
  }
  if (payload.size() < 4 || payload.substr(0, 4) != "X3P1") {
    error = "invalid pickle data";
    return false;
  }
  Value loads;
  if (!marshal_function(runtime, "loads", loads, error)) {
    return false;
  }
  Value marshaled = Value::bytes(std::string(payload.substr(4)));
  return runtime_call_callable(runtime, loads, &marshaled, 1, out, error);
}

bool pickle_dump(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "pickle.dump() expected object, file, and optional protocol";
    return false;
  }
  Value data;
  if (!pickle_dumps(runtime, args, argc == 3 ? 2 : 1, data, error, nullptr)) {
    return false;
  }
  Value write;
  if (!attribute_get(args[1], "write", write, error)) {
    return false;
  }
  Value ignored;
  if (!runtime_call_callable(runtime, write, &data, 1, ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool pickle_load(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pickle.load() expected file";
    return false;
  }
  Value read;
  if (!attribute_get(args[0], "read", read, error)) {
    return false;
  }
  Value data;
  if (!runtime_call_callable(runtime, read, nullptr, 0, data, error)) {
    return false;
  }
  return pickle_loads(runtime, &data, 1, out, error, nullptr);
}

Value make_pickle_module(Runtime& runtime, const char* name) {
  Value exception_base = runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid();
  Value pickle_error = Value::class_object("PickleError", {}, exception_base);
  Value pickling_error = Value::class_object("PicklingError", {}, pickle_error);
  Value unpickling_error = Value::class_object("UnpicklingError", {}, pickle_error);

  std::vector<std::pair<std::string, Value>> buffer_attrs;
  buffer_attrs.push_back({"__init__", runtime.make_native_function(std::string(name) + ".PickleBuffer.__init__", picklebuffer_init)});

  NativeModuleBuilder builder(runtime, name);
  builder.value("HIGHEST_PROTOCOL", Value::int64(5))
      .value("DEFAULT_PROTOCOL", Value::int64(5))
      .value("PickleError", pickle_error)
      .value("PicklingError", pickling_error)
      .value("UnpicklingError", unpickling_error)
      .value("Pickler", Value::class_object("Pickler", {}))
      .value("Unpickler", Value::class_object("Unpickler", {}))
      .value("PickleBuffer", Value::class_object("PickleBuffer", std::move(buffer_attrs)))
      .function("dump", pickle_dump)
      .function("dumps", pickle_dumps)
      .function("load", pickle_load)
      .function("loads", pickle_loads);
  return builder.finish();
}

} // namespace

void register_pickle_module(Runtime& runtime) {
  runtime.register_module("_pickle", make_pickle_module(runtime, "_pickle"));
  runtime.register_module("pickle", make_pickle_module(runtime, "pickle"));
}

} // namespace xlang3
