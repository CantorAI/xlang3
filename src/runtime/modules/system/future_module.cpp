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

#include <cstdint>

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

bool feature_get_attr(const Value& self, const char* name, Value& out, std::string& error) {
  return object_get_attr(self, name, out, error);
}

bool feature_get_optional(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_Feature.getOptionalRelease() expected no arguments";
    return false;
  }
  return feature_get_attr(args[0], "optional", out, error);
}

bool feature_get_mandatory(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_Feature.getMandatoryRelease() expected no arguments";
    return false;
  }
  return feature_get_attr(args[0], "mandatory", out, error);
}

Value release_tuple(int64_t major, int64_t minor, int64_t micro, const char* level, int64_t serial) {
  return Value::tuple({
      Value::int64(major),
      Value::int64(minor),
      Value::int64(micro),
      Value::string(level),
      Value::int64(serial),
  });
}

Value make_feature_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("__future__")});
  attrs.push_back({"getOptionalRelease", runtime.make_native_function("__future__._Feature.getOptionalRelease", feature_get_optional)});
  attrs.push_back({"getMandatoryRelease", runtime.make_native_function("__future__._Feature.getMandatoryRelease", feature_get_mandatory)});
  return Value::class_object("_Feature", std::move(attrs));
}

Value make_feature(Value klass, const char* name, Value optional, Value mandatory, int64_t compiler_flag) {
  Value instance = Value::instance(klass);
  std::string error;
  object_set_attr(instance, "__name__", Value::string(name), error);
  object_set_attr(instance, "optional", optional, error);
  object_set_attr(instance, "mandatory", mandatory, error);
  object_set_attr(instance, "compiler_flag", Value::int64(compiler_flag), error);
  return instance;
}

} // namespace

void register_future_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "__future__");
  Value feature_class = make_feature_class(runtime);
  Value never = Value::none();
  builder.value("all_feature_names", make_feature_names())
      .value("__all__", Value::list({
                            Value::string("all_feature_names"),
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
                        }))
      .value("CO_NESTED", Value::int64(16))
      .value("CO_FUTURE_NESTED_SCOPES", Value::int64(16))
      .value("CO_FUTURE_DIVISION", Value::int64(131072))
      .value("CO_FUTURE_ABSOLUTE_IMPORT", Value::int64(262144))
      .value("CO_FUTURE_WITH_STATEMENT", Value::int64(524288))
      .value("CO_FUTURE_PRINT_FUNCTION", Value::int64(1048576))
      .value("CO_FUTURE_UNICODE_LITERALS", Value::int64(2097152))
      .value("CO_FUTURE_BARRY_AS_BDFL", Value::int64(4194304))
      .value("CO_FUTURE_GENERATOR_STOP", Value::int64(8388608))
      .value("CO_FUTURE_ANNOTATIONS", Value::int64(16777216))
      .value("_Feature", feature_class)
      .value("nested_scopes", make_feature(feature_class, "nested_scopes", release_tuple(2, 1, 0, "beta", 1), release_tuple(2, 2, 0, "final", 0), 16))
      .value("generators", make_feature(feature_class, "generators", release_tuple(2, 2, 0, "alpha", 1), release_tuple(2, 3, 0, "final", 0), 0))
      .value("division", make_feature(feature_class, "division", release_tuple(2, 2, 0, "alpha", 2), release_tuple(3, 0, 0, "alpha", 0), 131072))
      .value("absolute_import", make_feature(feature_class, "absolute_import", release_tuple(2, 5, 0, "alpha", 1), release_tuple(3, 0, 0, "alpha", 0), 262144))
      .value("with_statement", make_feature(feature_class, "with_statement", release_tuple(2, 5, 0, "alpha", 1), release_tuple(2, 6, 0, "alpha", 0), 524288))
      .value("print_function", make_feature(feature_class, "print_function", release_tuple(2, 6, 0, "alpha", 2), release_tuple(3, 0, 0, "alpha", 0), 1048576))
      .value("unicode_literals", make_feature(feature_class, "unicode_literals", release_tuple(2, 6, 0, "alpha", 2), release_tuple(3, 0, 0, "alpha", 0), 2097152))
      .value("barry_as_FLUFL", make_feature(feature_class, "barry_as_FLUFL", release_tuple(3, 1, 0, "alpha", 2), release_tuple(4, 0, 0, "alpha", 0), 4194304))
      .value("generator_stop", make_feature(feature_class, "generator_stop", release_tuple(3, 5, 0, "beta", 1), release_tuple(3, 7, 0, "alpha", 0), 8388608))
      .value("annotations", make_feature(feature_class, "annotations", release_tuple(3, 7, 0, "beta", 1), never, 16777216));
  runtime.register_module("__future__", builder.finish());
}

} // namespace xlang3
