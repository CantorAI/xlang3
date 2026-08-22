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
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

bool codecs_lookup(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "codecs.lookup() expected encoding name";
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("codecs")});
  Value klass = Value::class_object("CodecInfo", std::move(attrs));
  out = Value::instance(klass);
  object_set_attr(out, "name", args[0], error);
  return true;
}

bool codecs_encode_decode(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "codecs encode/decode expected object and optional encoding/errors";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

} // namespace

void register_codecs_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "codecs");
  builder.function("lookup", codecs_lookup)
      .function("encode", codecs_encode_decode)
      .function("decode", codecs_encode_decode)
      .value("BOM_UTF8", Value::bytes(std::string("\xEF\xBB\xBF", 3)))
      .value("BOM", Value::bytes({}));
  runtime.register_module("codecs", builder.finish());
}

} // namespace xlang3
