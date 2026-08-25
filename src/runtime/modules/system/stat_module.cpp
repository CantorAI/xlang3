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

#include <string>

namespace xlang3 {

namespace {

bool stat_mode_arg(const Value* args, uint32_t argc, int64_t& mode, std::string& error, const char* name) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = std::string(name) + "() expected integer mode";
    return false;
  }
  mode = args[0].as.i64;
  return true;
}

bool stat_file_type(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  int64_t mode = 0;
  if (!stat_mode_arg(args, argc, mode, error, static_cast<const char*>(user_data))) {
    return false;
  }
  const int64_t expected = std::string(static_cast<const char*>(user_data)) == "S_ISDIR" ? 0040000 :
      std::string(static_cast<const char*>(user_data)) == "S_ISREG" ? 0100000 :
      std::string(static_cast<const char*>(user_data)) == "S_ISLNK" ? 0120000 :
      std::string(static_cast<const char*>(user_data)) == "S_ISCHR" ? 0020000 :
      std::string(static_cast<const char*>(user_data)) == "S_ISBLK" ? 0060000 :
      std::string(static_cast<const char*>(user_data)) == "S_ISFIFO" ? 0010000 : 0140000;
  value_set_bool(out, (mode & 0170000) == expected);
  return true;
}

} // namespace

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
      .value("S_IFSOCK", Value::int64(0140000))
      .value("S_IRUSR", Value::int64(0400))
      .value("S_IWUSR", Value::int64(0200))
      .value("S_IXUSR", Value::int64(0100))
      .value("S_IRGRP", Value::int64(0040))
      .value("S_IWGRP", Value::int64(0020))
      .value("S_IXGRP", Value::int64(0010))
      .value("S_IROTH", Value::int64(0004))
      .value("S_IWOTH", Value::int64(0002))
      .value("S_IXOTH", Value::int64(0001))
      .value("S_ISDIR", runtime.make_native_function("_stat.S_ISDIR", stat_file_type, const_cast<char*>("S_ISDIR")))
      .value("S_ISREG", runtime.make_native_function("_stat.S_ISREG", stat_file_type, const_cast<char*>("S_ISREG")))
      .value("S_ISLNK", runtime.make_native_function("_stat.S_ISLNK", stat_file_type, const_cast<char*>("S_ISLNK")))
      .value("S_ISCHR", runtime.make_native_function("_stat.S_ISCHR", stat_file_type, const_cast<char*>("S_ISCHR")))
      .value("S_ISBLK", runtime.make_native_function("_stat.S_ISBLK", stat_file_type, const_cast<char*>("S_ISBLK")))
      .value("S_ISFIFO", runtime.make_native_function("_stat.S_ISFIFO", stat_file_type, const_cast<char*>("S_ISFIFO")))
      .value("S_ISSOCK", runtime.make_native_function("_stat.S_ISSOCK", stat_file_type, const_cast<char*>("S_ISSOCK")));
  runtime.register_module("_stat", builder.finish());
}

} // namespace xlang3
