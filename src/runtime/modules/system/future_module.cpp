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

Value make_feature_names() {
  return Value::list({
      Value::string("nested_scopes"),
      Value::string("generators"),
      Value::string("division"),
      Value::string("absolute_import"),
      Value::string("with_statement"),
      Value::string("print_function"),
      Value::string("unicode_literals"),
      Value::string("barry_as_FLUFL"),
      Value::string("generator_stop"),
      Value::string("annotations"),
  });
}

Value make_feature(const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("__future__")});
  Value klass = Value::class_object("_Feature", std::move(attrs));
  Value instance = Value::instance(klass);
  std::string error;
  object_set_attr(instance, "__name__", Value::string(name), error);
  return instance;
}

} // namespace

void register_future_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "__future__");
  builder.value("all_feature_names", make_feature_names())
      .value("nested_scopes", make_feature("nested_scopes"))
      .value("generators", make_feature("generators"))
      .value("division", make_feature("division"))
      .value("absolute_import", make_feature("absolute_import"))
      .value("with_statement", make_feature("with_statement"))
      .value("print_function", make_feature("print_function"))
      .value("unicode_literals", make_feature("unicode_literals"))
      .value("barry_as_FLUFL", make_feature("barry_as_FLUFL"))
      .value("generator_stop", make_feature("generator_stop"))
      .value("annotations", make_feature("annotations"));
  runtime.register_module("__future__", builder.finish());
}

} // namespace xlang3
