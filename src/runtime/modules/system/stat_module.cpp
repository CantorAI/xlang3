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

void register_stat_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_stat");
  builder.value("ST_MODE", Value::int64(0))
      .value("ST_INO", Value::int64(1))
      .value("ST_DEV", Value::int64(2))
      .value("ST_NLINK", Value::int64(3))
      .value("ST_UID", Value::int64(4))
      .value("ST_GID", Value::int64(5))
      .value("ST_SIZE", Value::int64(6))
      .value("ST_ATIME", Value::int64(7))
      .value("ST_MTIME", Value::int64(8))
      .value("ST_CTIME", Value::int64(9))
      .value("S_IFMT", Value::int64(0170000))
      .value("S_IFDIR", Value::int64(0040000))
      .value("S_IFREG", Value::int64(0100000))
      .value("S_IFLNK", Value::int64(0120000))
      .value("S_IFCHR", Value::int64(0020000))
      .value("S_IFBLK", Value::int64(0060000))
      .value("S_IFIFO", Value::int64(0010000))
      .value("S_IFSOCK", Value::int64(0140000));
  runtime.register_module("_stat", builder.finish());
}

} // namespace xlang3
