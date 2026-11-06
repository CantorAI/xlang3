// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// Licensed under the Apache License, Version 2.0.
// Included inside os_module.cpp's anonymous namespace on POSIX platforms.

constexpr const char* kStatvfsFields[] = {
    "f_bsize", "f_frsize", "f_blocks", "f_bfree", "f_bavail",
    "f_files", "f_ffree", "f_favail", "f_flag", "f_namemax"};

Value statvfs_integer(uint64_t value) {
  return value <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())
      ? Value::int64(static_cast<int64_t>(value)) : value_bigint_from_u64(value);
}

Value make_statvfs_result(const Value& klass, std::vector<Value> fields, Value fsid) {
  Value result = Value::instance(klass);
  auto* instance = value_as_instance(result);
  instance->attrs.push_back({"__xlang3_tuple_value__", Value::tuple(std::move(fields))});
  instance->attrs.push_back({"f_fsid", std::move(fsid)});
  return result;
}

bool os_statvfs_result_new(Runtime& runtime, const Value* args, uint32_t argc,
                           Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3 || value_as_class(args[0]) == nullptr ||
      (argc == 3 && !mapping_is_dict(args[2]))) {
    error = "os.statvfs_result() expected a sequence and optional dict";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> fields;
  if (!runtime_collect_iterable(runtime, args[1], fields, error)) return false;
  if (fields.size() < 10 || fields.size() > 11) {
    error = "os.statvfs_result() takes a sequence of 10 or 11 items";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value fsid = fields.size() == 11 ? fields.back() : Value::none();
  if (fields.size() == 10 && argc == 3) {
    for (const auto& entry : mapping_dict_storage(args[2])->entries) {
      auto* key = value_as_string(entry.first);
      if (key != nullptr && string_object_to_string(*key) == "f_fsid") {
        fsid = entry.second;
        break;
      }
    }
  }
  fields.resize(10);
  out = make_statvfs_result(args[0], std::move(fields), std::move(fsid));
  return true;
}

bool os_statvfs_result_repr(Runtime&, const Value* args, uint32_t argc,
                            Value& out, std::string& error, void*) {
  Value values;
  if (argc != 1 || !object_get_attr(args[0], "__xlang3_tuple_value__", values, error)) return false;
  auto* tuple = value_as_tuple(values);
  if (tuple == nullptr || tuple->items.size() != 10) return false;
  std::string text = "os.statvfs_result(";
  for (size_t i = 0; i < 10; ++i) {
    if (i) text += ", ";
    text += std::string(kStatvfsFields[i]) + "=" + value_to_repr(tuple->items[i]);
  }
  out = Value::string(text + ")");
  return true;
}

bool os_statvfs_result_reduce(Runtime& runtime, const Value* args, uint32_t argc,
                              Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    runtime.raise_class_error("TypeError", "statvfs_result reduction expected optional protocol");
    return false;
  }
  Value klass, fields, fsid;
  if (!object_get_attr(args[0], "__class__", klass, error) ||
      !object_get_attr(args[0], "__xlang3_tuple_value__", fields, error) ||
      !object_get_attr(args[0], "f_fsid", fsid, error)) return false;
  out = Value::tuple({klass, Value::tuple({fields,
      Value::dict({{Value::string("f_fsid"), fsid}})})});
  return true;
}

Value make_statvfs_result_class(Runtime& runtime) {
  const Value* tuple_base = runtime.find_builtin("tuple");
  std::vector<std::pair<std::string, Value>> attrs = {
      {"__module__", Value::string("os")}, {"__qualname__", Value::string("statvfs_result")},
      {"__new__", runtime.make_native_function("os.statvfs_result.__new__", os_statvfs_result_new)},
      {"__repr__", runtime.make_native_function("os.statvfs_result.__repr__", os_statvfs_result_repr)},
      {"__str__", runtime.make_native_function("os.statvfs_result.__str__", os_statvfs_result_repr)},
      {"__setattr__", runtime.make_native_function("os.statvfs_result.__setattr__", immutable_structseq_setattr)},
      {"__reduce__", runtime.make_native_function("os.statvfs_result.__reduce__", os_statvfs_result_reduce)},
      {"__reduce_ex__", runtime.make_native_function("os.statvfs_result.__reduce_ex__", os_statvfs_result_reduce)},
      {"__eq__", runtime.make_native_function("os.statvfs_result.__eq__", os_stat_result_equal)},
      {"__ne__", runtime.make_native_function("os.statvfs_result.__ne__", os_stat_result_equal, reinterpret_cast<void*>(1))},
      {"n_sequence_fields", Value::int64(10)}, {"n_fields", Value::int64(11)},
      {"n_unnamed_fields", Value::int64(0)}, {"f_fsid", Value::none()},
  };
  std::vector<Value> names;
  for (size_t i = 0; i < 10; ++i) {
    attrs.push_back({kStatvfsFields[i], slot_descriptor("os.statvfs_result", kStatvfsFields[i], i)});
    names.push_back(Value::string(kStatvfsFields[i]));
  }
  attrs.push_back({"__match_args__", Value::tuple(std::move(names))});
  Value result = Value::class_object("statvfs_result", std::move(attrs),
                                   tuple_base != nullptr ? *tuple_base : Value::invalid());
  auto* klass = value_as_class(result);
  klass->restrict_instance_attrs = true;
  klass->allow_instance_dict = false;
  klass->has_setattr_hook = true;
  return result;
}

bool os_statvfs_impl(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void* user_data, bool fd_only) {
  if (argc != 1) {
    error = "statvfs() expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value index_method;
  std::string ignored;
  const bool is_fd = fd_only || args[0].tag == ValueTag::Int64 || args[0].tag == ValueTag::Bool ||
      value_as_bigint(args[0]) != nullptr || object_get_attr(args[0], "__index__", index_method, ignored);
  int fd = -1;
  std::string native_path;
  PathArg path;
  if (is_fd) {
    if (!os_fd_arg(runtime, args[0], fd, error)) return false;
  } else {
    if (!get_path_arg(runtime, args[0], "statvfs path", path, error)) return false;
    if (path.text.empty()) return raise_os_path_error_with_errno(runtime, ENOENT, path, error);
    ResolvedPath resolved;
    if (!runtime.vfs().resolve(path.text, resolved, error)) return raise_path_not_found(runtime, error, &args[0]);
    if (!resolved.fs->native_path(resolved.path, native_path))
      return raise_os_path_error_with_errno(runtime, ENOTSUP, path, error);
  }
#if defined(__APPLE__)
  // macOS statvfs truncates block counts on large filesystems; use statfs.
  struct statfs info{};
#else
  struct statvfs info{};
#endif
  int result;
  do {
    if (is_fd && fd == -1 && !fd_only) {
      // The Python path converter reserves -1 as its non-descriptor sentinel.
      errno = EFAULT;
      result = -1;
    } else {
#if defined(__APPLE__)
      result = is_fd ? ::fstatfs(fd, &info) : ::statfs(native_path.c_str(), &info);
#else
      result = is_fd ? ::fstatvfs(fd, &info) : ::statvfs(native_path.c_str(), &info);
#endif
    }
  } while (result != 0 && errno == EINTR);
  if (result != 0) {
    const int code = errno;
    if (!is_fd) {
      return raise_os_path_error_with_errno(runtime, code, path, error);
    }
    error = std::strerror(code);
    Value exception = runtime.make_exception("OSError", error);
    object_set_attr(exception, "errno", Value::int64(code), ignored);
    object_set_attr(exception, "strerror", Value::string(error), ignored);
    if (!fd_only) object_set_attr(exception, "filename", args[0], ignored);
    runtime.set_pending_exception(std::move(exception));
    return false;
  }
#if defined(__APPLE__)
  const uint64_t flags = ((info.f_flags & MNT_RDONLY) ? ST_RDONLY : 0) |
      ((info.f_flags & MNT_NOSUID) ? ST_NOSUID : 0);
  const uint64_t bsize = info.f_iosize, frsize = info.f_bsize,
      favail = info.f_ffree, namemax = NAME_MAX,
      fsid = static_cast<unsigned long>(info.f_fsid.val[0]);
#else
  const uint64_t flags = info.f_flag, bsize = info.f_bsize, frsize = info.f_frsize,
      favail = info.f_favail, namemax = info.f_namemax, fsid = info.f_fsid;
#endif
  auto* state = static_cast<OsModuleState*>(user_data);
  out = make_statvfs_result(state->statvfs_result_class, {
      statvfs_integer(bsize), statvfs_integer(frsize), statvfs_integer(info.f_blocks),
      statvfs_integer(info.f_bfree), statvfs_integer(info.f_bavail), statvfs_integer(info.f_files),
      statvfs_integer(info.f_ffree), statvfs_integer(favail), statvfs_integer(flags),
      statvfs_integer(namemax)}, statvfs_integer(fsid));
  return true;
}

bool os_statvfs(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void* user_data) {
  return os_statvfs_impl(runtime, args, argc, out, error, user_data, false);
}

bool os_fstatvfs(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* user_data) {
  return os_statvfs_impl(runtime, args, argc, out, error, user_data, true);
}

bool os_statvfs_kw(Runtime& runtime, const Value* args, uint32_t argc,
                   const NativeKeywordArg* kwargs, uint32_t kwargc,
                   Value& out, std::string& error, void* user_data) {
  if (kwargc == 0) return os_statvfs(runtime, args, argc, out, error, user_data);
  if (argc == 0 && kwargc == 1 && kwargs[0].name != nullptr &&
      std::string(kwargs[0].name) == "path" && kwargs[0].value != nullptr)
    return os_statvfs(runtime, kwargs[0].value, 1, out, error, user_data);
  error = "statvfs() expected one path argument";
  runtime.raise_class_error("TypeError", error);
  return false;
}
