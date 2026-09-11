/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <windows.h>

namespace xlang3 {
namespace {

bool tw_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
               std::string& error, void* user_data) {
  const uint32_t offset = argc > 0 && value_as_string(args[0]) == nullptr ? 1 : 0;
  if (argc < offset + 1 || argc > offset + 2) {
    error = "encode() expected input and optional errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* input = value_as_string(args[offset]);
  if (input == nullptr) {
    error = "encode() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string text = string_object_to_string(*input);
  if (text.empty()) {
    out = Value::tuple({Value::bytes({}), Value::int64(0)});
    return true;
  }
  const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(wide_size), L'\0');
  if (wide_size > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), wide.data(), wide_size);
  BOOL used_default = FALSE;
  const int code_page = user_data == nullptr ? 950 : *static_cast<int*>(user_data);
  const bool strict_flags_supported = code_page != 54936 && code_page != 52936 &&
      code_page != 50220 && code_page != 50225;
  const DWORD flags = strict_flags_supported ? WC_NO_BEST_FIT_CHARS : 0;
  BOOL* used_default_ptr = strict_flags_supported ? &used_default : nullptr;
  const int byte_size = WideCharToMultiByte(code_page, flags, wide.data(), wide_size,
                                             nullptr, 0, nullptr, used_default_ptr);
  if (byte_size <= 0 || (strict_flags_supported && used_default)) {
    error = "big5 codec can't encode character";
    runtime.raise_class_error("UnicodeEncodeError", error);
    return false;
  }
  std::string bytes(static_cast<size_t>(byte_size), '\0');
  WideCharToMultiByte(code_page, flags, wide.data(), wide_size, bytes.data(), byte_size,
                      nullptr, used_default_ptr);
  out = Value::tuple({Value::bytes(std::move(bytes)),
                      Value::int64(static_cast<int64_t>(utf8_codepoint_count(text)))});
  return true;
}

bool tw_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
               std::string& error, void* user_data) {
  const uint32_t offset = argc > 0 && value_as_bytes(args[0]) == nullptr ? 1 : 0;
  if (argc < offset + 1 || argc > offset + 3) {
    error = "decode() expected input, optional errors, and optional final";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* input = value_as_bytes(args[offset]);
  if (input == nullptr) {
    error = "decode() argument must be bytes-like";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string bytes(bytes_object_view(*input));
  const int code_page = user_data == nullptr ? 950 : *static_cast<int*>(user_data);
  const DWORD decode_flags = (code_page == 52936 || code_page == 50220 || code_page == 50225) ? 0 : MB_ERR_INVALID_CHARS;
  const int wide_size = MultiByteToWideChar(code_page, decode_flags, bytes.data(),
                                             static_cast<int>(bytes.size()), nullptr, 0);
  if (wide_size <= 0 && !bytes.empty()) {
    error = "big5 codec can't decode byte";
    runtime.raise_class_error("UnicodeDecodeError", error);
    return false;
  }
  std::wstring wide(static_cast<size_t>(wide_size), L'\0');
  if (wide_size > 0) MultiByteToWideChar(code_page, decode_flags, bytes.data(),
                                         static_cast<int>(bytes.size()), wide.data(), wide_size);
  const int text_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, nullptr, 0, nullptr, nullptr);
  std::string text(static_cast<size_t>(text_size), '\0');
  if (text_size > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, text.data(), text_size,
                                         nullptr, nullptr);
  out = Value::tuple({Value::string(std::move(text)), Value::int64(static_cast<int64_t>(bytes.size()))});
  return true;
}

bool tw_getcodec(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                 std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "getcodec() expected an encoding name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string name = string_object_to_string(*value_as_string(args[0]));
  int code_page = 0;
  if (name == "big5" || name == "big5hkscs" || name == "cp950") code_page = 950;
  else if (name == "cp932" || name == "shift_jis" || name == "shift_jis_2004" ||
           name == "shift_jisx0213") code_page = 932;
  else if (name == "euc_jp" || name == "euc_jis_2004" || name == "euc_jisx0213") code_page = 20932;
  else if (name == "euc_kr" || name == "cp949") code_page = 949;
  else if (name == "johab") code_page = 1361;
  else if (name == "gb2312" || name == "gbk") code_page = 936;
  else if (name == "gb18030") code_page = 54936;
  else if (name == "hz") code_page = 52936;
  else if (name.rfind("iso2022_jp", 0) == 0) code_page = 50220;
  else if (name == "iso2022_kr") code_page = 50225;
  if (code_page == 0) {
    error = "no such codec is supported";
    runtime.raise_class_error("LookupError", error);
    return false;
  }
  out = Value::instance(Value::class_object("MultibyteCodec", {}));
  object_set_attr(out, "encode", runtime.make_native_function(
      "_multibytecodec.MultibyteCodec.encode", tw_encode, new int(code_page),
      [](void* data) { delete static_cast<int*>(data); }), error);
  object_set_attr(out, "decode", runtime.make_native_function(
      "_multibytecodec.MultibyteCodec.decode", tw_decode, new int(code_page),
      [](void* data) { delete static_cast<int*>(data); }), error);
  return true;
}

Value empty_codec_class(const char* name) {
  return Value::class_object(name, {{"__module__", Value::string("_multibytecodec")}});
}

bool multibyte_delegate(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                        std::string& error, bool encode, bool incremental) {
  if (argc < 2) {
    error = encode ? "encode() missing input" : "decode() missing input";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value codec;
  if (!object_get_attr(args[0], "codec", codec, error)) return false;
  Value callable;
  if (!object_get_attr(codec, encode ? "encode" : "decode", callable, error)) return false;
  Value errors = Value::string("strict");
  if (argc >= 3 && value_as_string(args[2]) != nullptr) {
    errors = args[2];
  } else {
    std::string ignored;
    object_get_attr(args[0], "errors", errors, ignored);
  }
  Value call_args[2] = {args[1], errors};
  Value result;
  if (!runtime_call_callable(runtime, callable, call_args, 2, result, error)) return false;
  if (incremental) {
    auto* tuple = value_as_tuple(result);
    if (tuple == nullptr || tuple->items.empty()) {
      error = "multibyte codec returned invalid result";
      return false;
    }
    out = tuple->items[0];
  } else {
    out = std::move(result);
  }
  return true;
}

bool multibyte_incremental_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                                  std::string& error, void*) {
  return multibyte_delegate(runtime, args, argc, out, error, true, true);
}

bool multibyte_incremental_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                                  std::string& error, void*) {
  return multibyte_delegate(runtime, args, argc, out, error, false, true);
}

bool multibyte_stream_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                             std::string& error, void*) {
  return multibyte_delegate(runtime, args, argc, out, error, true, false);
}

bool multibyte_stream_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                             std::string& error, void*) {
  return multibyte_delegate(runtime, args, argc, out, error, false, false);
}

Value codec_base_class(Runtime& runtime, const char* name, const char* method,
                       NativeFunctionCallback callback) {
  return Value::class_object(name, {
      {"__module__", Value::string("_multibytecodec")},
      {method, runtime.make_native_function(
          std::string("_multibytecodec.") + name + "." + method, callback)},
  });
}

}  // namespace

void register_multibytecodec_module(Runtime& runtime) {
  NativeModuleBuilder mbc(runtime, "_multibytecodec");
  mbc.value("MultibyteIncrementalEncoder", codec_base_class(
          runtime, "MultibyteIncrementalEncoder", "encode", multibyte_incremental_encode))
      .value("MultibyteIncrementalDecoder", codec_base_class(
          runtime, "MultibyteIncrementalDecoder", "decode", multibyte_incremental_decode))
      .value("MultibyteStreamReader", codec_base_class(
          runtime, "MultibyteStreamReader", "decode", multibyte_stream_decode))
      .value("MultibyteStreamWriter", codec_base_class(
          runtime, "MultibyteStreamWriter", "encode", multibyte_stream_encode));
  runtime.register_module("_multibytecodec", mbc.finish());

  NativeModuleBuilder tw(runtime, "_codecs_tw");
  tw.function("getcodec", tw_getcodec);
  runtime.register_module("_codecs_tw", tw.finish());

  NativeModuleBuilder hk(runtime, "_codecs_hk");
  hk.function("getcodec", tw_getcodec);
  runtime.register_module("_codecs_hk", hk.finish());

  for (const char* module_name : {"_codecs_jp", "_codecs_cn", "_codecs_kr", "_codecs_iso2022"}) {
    NativeModuleBuilder regional(runtime, module_name);
    regional.function("getcodec", tw_getcodec);
    runtime.register_module(module_name, regional.finish());
  }
}

}  // namespace xlang3
