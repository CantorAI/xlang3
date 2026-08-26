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

bool tokenizer_iter_init(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool tokenizer_iter_self(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "TokenizerIter.__iter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool tokenizer_iter_next(Runtime&, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "TokenizerIter token generation is not implemented";
  return false;
}

Value make_tokenizer_iter_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_tokenize.TokenizerIter.__init__", tokenizer_iter_init)});
  attrs.push_back({"__iter__", runtime.make_native_function("_tokenize.TokenizerIter.__iter__", tokenizer_iter_self)});
  attrs.push_back({"__next__", runtime.make_native_function("_tokenize.TokenizerIter.__next__", tokenizer_iter_next)});
  return Value::class_object("TokenizerIter", std::move(attrs));
}

} // namespace

void register_tokenize_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_tokenize");
  builder.value("TokenizerIter", make_tokenizer_iter_class(runtime));
  runtime.register_module("_tokenize", builder.finish());
}

} // namespace xlang3
