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

namespace xlang3 {

struct PythonNames {
  static constexpr const char* init_method = "__init__";
  static constexpr const char* type_error = "TypeError";
  static constexpr const char* runtime_error = "RuntimeError";
  static constexpr const char* object_type = "object";

  static constexpr const char* builtin_type = "type";
  static constexpr const char* builtin_str = "str";
  static constexpr const char* builtin_bool = "bool";
  static constexpr const char* builtin_int = "int";
  static constexpr const char* builtin_float = "float";
  static constexpr const char* builtin_range = "range";
  static constexpr const char* builtin_list = "list";
  static constexpr const char* builtin_tuple = "tuple";
  static constexpr const char* builtin_set = "set";
  static constexpr const char* builtin_dict = "dict";
  static constexpr const char* builtin_bytes = "bytes";
  static constexpr const char* builtin_bytearray = "bytearray";
  static constexpr const char* builtin_memoryview = "memoryview";
  static constexpr const char* builtin_property = "property";
  static constexpr const char* builtin_classmethod = "classmethod";
  static constexpr const char* builtin_staticmethod = "staticmethod";
  static constexpr const char* builtin_super = "super";

  static constexpr const char* builtin_len = "len";

  static constexpr const char* list_append = "append";

  static constexpr const char* str_strip = "strip";
  static constexpr const char* str_startswith = "startswith";
  static constexpr const char* str_replace = "replace";
  static constexpr const char* str_split = "split";
  static constexpr const char* str_join = "join";
};

} // namespace xlang3
