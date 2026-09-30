/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

extern "C" {
#include "llhttp.h"
}

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr const char* kParserType = "httptools.parser.parser.HttpParser";
constexpr std::array<const char*, 9> kCallbackNames = {
    "on_url", "on_status", "on_body", "on_header", "on_headers_complete",
    "on_message_complete", "on_chunk_header", "on_chunk_complete",
    "on_message_begin"};
enum CallbackIndex : size_t {
  kOnUrl, kOnStatus, kOnBody, kOnHeader, kOnHeadersComplete,
  kOnMessageComplete, kOnChunkHeader, kOnChunkComplete, kOnMessageBegin
};
constexpr std::array<const char*, 10> kLeniencyNames = {
    "lenient_headers", "lenient_chunked_length", "lenient_keep_alive",
    "lenient_transfer_encoding", "lenient_version", "lenient_data_after_close",
    "lenient_optional_lf_after_cr", "lenient_optional_cr_before_lf",
    "lenient_optional_crlf_after_chunk", "lenient_spaces_after_chunk_size"};

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value request_class = x3_value_invalid();
  X3Value response_class = x3_value_invalid();
  std::array<X3Value, 6> errors{};

  PackageState() { errors.fill(x3_value_invalid()); }
};

struct ParserState {
  PackageState* package = nullptr;
  llhttp_t parser{};
  llhttp_settings_t settings{};
  std::array<X3Value, 9> callbacks{};
  X3Value last_error = x3_value_invalid();
  X3CallContext* call_context = nullptr;
  X3Runtime* runtime = nullptr;
  std::string header_name;
  std::string header_value;
  bool has_header_name = false;
  bool has_header_value = false;

  explicit ParserState(PackageState* owner) : package(owner) {
    callbacks.fill(x3_value_none());
  }

  ~ParserState() {
    for (X3Value callback : callbacks) package->host->value_release(callback);
    package->host->value_release(last_error);
  }
};

void cleanup_parser(void* data) { delete static_cast<ParserState*>(data); }

void cleanup_package(void* data) {
  auto* state = static_cast<PackageState*>(data);
  if (state == nullptr) return;
  state->host->value_release(state->request_class);
  state->host->value_release(state->response_class);
  for (X3Value error : state->errors) state->host->value_release(error);
  delete state;
}

X3Status type_error(PackageState* state, X3CallContext* context,
                    const char* message) {
  return state->host->raise_class_error(context, "TypeError", message);
}

ParserState* require_parser(PackageState* state, X3CallContext* context,
                            X3Value value) {
  auto* parser = static_cast<ParserState*>(
      state->host->instance_get_native_data(value, kParserType));
  if (parser == nullptr) type_error(state, context, "expected httptools parser");
  return parser;
}

void remember_callback_error(ParserState* state) {
  state->package->host->value_release(state->last_error);
  state->last_error = x3_value_invalid();
  if (state->call_context != nullptr)
    state->package->host->take_exception(state->call_context, &state->last_error);
}

bool call_callback(ParserState* state, CallbackIndex index,
                   const X3Value* args = nullptr, uint32_t argc = 0) {
  if (state->callbacks[index].tag == X3_TAG_NONE) return true;
  X3Value result = x3_value_invalid();
  const X3Status status = state->package->host->call(
      state->runtime, state->callbacks[index], args, argc, &result);
  state->package->host->value_release(result);
  if (status == X3_STATUS_OK) return true;
  remember_callback_error(state);
  return false;
}

int call_data_callback(llhttp_t* parser, CallbackIndex index,
                       const char* data, size_t size, const char* reason) {
  auto* state = static_cast<ParserState*>(parser->data);
  if (state->callbacks[index].tag == X3_TAG_NONE) return 0;
  X3Value arg = state->package->host->value_bytes(state->runtime, data, size);
  if (arg.tag == X3_TAG_INVALID) {
    remember_callback_error(state);
    llhttp_set_error_reason(parser, reason);
    return HPE_USER;
  }
  const bool ok = call_callback(state, index, &arg, 1);
  state->package->host->value_release(arg);
  if (!ok) llhttp_set_error_reason(parser, reason);
  return ok ? 0 : HPE_USER;
}

int flush_header(ParserState* state) {
  if (!state->has_header_value) return 0;
  if (state->callbacks[kOnHeader].tag != X3_TAG_NONE) {
    X3Value args[] = {
        state->package->host->value_bytes(state->runtime,
                                           state->header_name.data(),
                                           state->header_name.size()),
        state->package->host->value_bytes(state->runtime,
                                           state->header_value.data(),
                                           state->header_value.size())};
    if (args[0].tag == X3_TAG_INVALID || args[1].tag == X3_TAG_INVALID) {
      state->package->host->value_release(args[0]);
      state->package->host->value_release(args[1]);
      remember_callback_error(state);
      return HPE_USER;
    }
    const bool ok = call_callback(state, kOnHeader, args, 2);
    state->package->host->value_release(args[0]);
    state->package->host->value_release(args[1]);
    if (!ok) {
      llhttp_set_error_reason(&state->parser, "`on_header` callback error");
      return HPE_USER;
    }
  }
  state->header_name.clear();
  state->header_value.clear();
  state->has_header_name = false;
  state->has_header_value = false;
  return 0;
}

int on_message_begin(llhttp_t* parser) {
  auto* state = static_cast<ParserState*>(parser->data);
  return call_callback(state, kOnMessageBegin) ? 0 : -1;
}

int on_url(llhttp_t* parser, const char* data, size_t size) {
  return call_data_callback(parser, kOnUrl, data, size, "`on_url` callback error");
}

int on_status(llhttp_t* parser, const char* data, size_t size) {
  return call_data_callback(parser, kOnStatus, data, size,
                            "`on_status` callback error");
}

int on_body(llhttp_t* parser, const char* data, size_t size) {
  return call_data_callback(parser, kOnBody, data, size,
                            "`on_body` callback error");
}

int on_header_field(llhttp_t* parser, const char* data, size_t size) {
  auto* state = static_cast<ParserState*>(parser->data);
  const int status = flush_header(state);
  if (status != 0) return status;
  state->header_name.append(data, size);
  state->has_header_name = true;
  return 0;
}

int on_header_value(llhttp_t* parser, const char* data, size_t size) {
  auto* state = static_cast<ParserState*>(parser->data);
  state->header_value.append(data, size);
  state->has_header_value = true;
  return 0;
}

int on_headers_complete(llhttp_t* parser) {
  auto* state = static_cast<ParserState*>(parser->data);
  const int status = flush_header(state);
  if (status != 0) return status;
  if (!call_callback(state, kOnHeadersComplete)) return -1;
  return parser->upgrade ? 1 : 0;
}

int on_message_complete(llhttp_t* parser) {
  auto* state = static_cast<ParserState*>(parser->data);
  return call_callback(state, kOnMessageComplete) ? 0 : -1;
}

int on_chunk_header(llhttp_t* parser) {
  auto* state = static_cast<ParserState*>(parser->data);
  if (state->has_header_value || state->has_header_name) {
    llhttp_set_error_reason(parser, "invalid headers state");
    return -1;
  }
  return call_callback(state, kOnChunkHeader) ? 0 : -1;
}

int on_chunk_complete(llhttp_t* parser) {
  auto* state = static_cast<ParserState*>(parser->data);
  const int status = flush_header(state);
  if (status != 0) return status;
  return call_callback(state, kOnChunkComplete) ? 0 : -1;
}

X3Status optional_callback(PackageState* state, X3Runtime* runtime,
                           X3Value protocol, const char* name, X3Value* output) {
  X3Value getattr_fn = x3_value_invalid();
  if (state->host->builtin_value(state->host, "getattr", &getattr_fn) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  X3Value attr_name = state->host->value_string(runtime, name);
  const X3Value args[] = {protocol, attr_name, x3_value_none()};
  const X3Status status = state->host->call(runtime, getattr_fn, args, 3, output);
  state->host->value_release(attr_name);
  state->host->value_release(getattr_fn);
  return status;
}

X3Status parser_init(PackageState* state, X3CallContext* context,
                     X3Runtime* runtime, const X3Value* args, uint32_t argc,
                     X3Value* result, llhttp_type_t mode) {
  if (argc != 2) return type_error(state, context, "HTTP parser requires a protocol");
  auto native = std::make_unique<ParserState>(state);
  llhttp_settings_init(&native->settings);
  llhttp_init(&native->parser, mode, &native->settings);
  native->parser.data = native.get();
  for (size_t index = 0; index < native->callbacks.size(); ++index) {
    X3Value callback = x3_value_invalid();
    if (optional_callback(state, runtime, args[1], kCallbackNames[index],
                          &callback) != X3_STATUS_OK) return X3_STATUS_ERROR;
    native->callbacks[index] = callback;
  }
  native->settings.on_headers_complete = on_headers_complete;
  native->settings.on_chunk_header = on_chunk_header;
  native->settings.on_chunk_complete = on_chunk_complete;
  if (native->callbacks[kOnMessageBegin].tag != X3_TAG_NONE)
    native->settings.on_message_begin = on_message_begin;
  if (native->callbacks[kOnUrl].tag != X3_TAG_NONE && mode == HTTP_REQUEST)
    native->settings.on_url = on_url;
  if (native->callbacks[kOnStatus].tag != X3_TAG_NONE && mode == HTTP_RESPONSE)
    native->settings.on_status = on_status;
  if (native->callbacks[kOnHeader].tag != X3_TAG_NONE) {
    native->settings.on_header_field = on_header_field;
    native->settings.on_header_value = on_header_value;
  }
  if (native->callbacks[kOnBody].tag != X3_TAG_NONE)
    native->settings.on_body = on_body;
  if (native->callbacks[kOnMessageComplete].tag != X3_TAG_NONE)
    native->settings.on_message_complete = on_message_complete;
  if (state->host->instance_set_native_data(args[0], kParserType, native.get(),
                                            cleanup_parser) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  native.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status request_init(X3CallContext* context, X3Runtime* runtime,
                      void* user_data, const X3Value* args, uint32_t argc,
                      X3Value* result) {
  return parser_init(static_cast<PackageState*>(user_data), context, runtime,
                     args, argc, result, HTTP_REQUEST);
}

X3Status response_init(X3CallContext* context, X3Runtime* runtime,
                       void* user_data, const X3Value* args, uint32_t argc,
                       X3Value* result) {
  return parser_init(static_cast<PackageState*>(user_data), context, runtime,
                     args, argc, result, HTTP_RESPONSE);
}

X3Status import_error_class(PackageState* state, X3Runtime* runtime,
                            size_t index, const char* name) {
  if (state->errors[index].tag != X3_TAG_INVALID) return X3_STATUS_OK;
  X3Value importer = x3_value_invalid();
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  X3Value module_name = state->host->value_string(runtime,
                                                   "httptools.parser.errors");
  X3Value fromlist = state->host->value_list(runtime);
  X3Value item = state->host->value_string(runtime, name);
  X3Status status = state->host->list_append(runtime, fromlist, item);
  X3Value module = x3_value_invalid();
  if (status == X3_STATUS_OK) {
    const X3Value args[] = {module_name, x3_value_none(), x3_value_none(), fromlist};
    status = state->host->call(runtime, importer, args, 4, &module);
  }
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, module, name, &state->errors[index]);
  state->host->value_release(module);
  state->host->value_release(item);
  state->host->value_release(fromlist);
  state->host->value_release(module_name);
  state->host->value_release(importer);
  return status;
}

X3Status raise_parser_error(ParserState* native, X3CallContext* context,
                            X3Runtime* runtime, llhttp_errno_t error) {
  const char* name = "HttpParserError";
  size_t index = 0;
  if (error == HPE_CB_MESSAGE_BEGIN || error == HPE_CB_HEADERS_COMPLETE ||
      error == HPE_CB_MESSAGE_COMPLETE || error == HPE_CB_CHUNK_HEADER ||
      error == HPE_CB_CHUNK_COMPLETE || error == HPE_USER) {
    name = "HttpParserCallbackError";
    index = 1;
  } else if (error == HPE_INVALID_STATUS) {
    name = "HttpParserInvalidStatusError";
    index = 2;
  } else if (error == HPE_INVALID_METHOD) {
    name = "HttpParserInvalidMethodError";
    index = 3;
  } else if (error == HPE_INVALID_URL) {
    name = "HttpParserInvalidURLError";
    index = 4;
  }
  if (import_error_class(native->package, runtime, index, name) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* reason = llhttp_get_error_reason(&native->parser);
  if (reason == nullptr) reason = llhttp_errno_name(error);
  if (index != 1 || native->last_error.tag == X3_TAG_INVALID)
    return native->package->host->raise_error(context,
                                               native->package->errors[index],
                                               reason);
  X3Value message = native->package->host->value_string(runtime, reason);
  X3Value exception = x3_value_invalid();
  X3Status status = native->package->host->call(
      runtime, native->package->errors[index], &message, 1, &exception);
  native->package->host->value_release(message);
  if (status == X3_STATUS_OK)
    status = native->package->host->set_attr(runtime, exception, "__context__",
                                             native->last_error);
  if (status == X3_STATUS_OK)
    status = native->package->host->raise_exception(context, exception);
  if (status == X3_STATUS_OK) status = X3_STATUS_ERROR;
  native->package->host->value_release(exception);
  native->package->host->value_release(native->last_error);
  native->last_error = x3_value_invalid();
  return status;
}

X3Status feed_data(X3CallContext* context, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 2) return type_error(package, context, "feed_data() takes one argument");
  ParserState* native = require_parser(package, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const X3ObjectKind kind = package->host->value_object_kind(args[1]);
  if (kind != X3_OBJECT_KIND_BYTES && kind != X3_OBJECT_KIND_BYTEARRAY &&
      kind != X3_OBJECT_KIND_MEMORYVIEW)
    return type_error(package, context, "a bytes-like object is required");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (package->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  const char* data = static_cast<const char*>(info.data);
  native->runtime = runtime;
  native->call_context = context;
  const llhttp_errno_t status = llhttp_execute(&native->parser, data,
                                                static_cast<size_t>(info.size));
  native->call_context = nullptr;
  native->runtime = nullptr;
  if (native->parser.upgrade == 1 && status == HPE_PAUSED_UPGRADE) {
    const char* position = llhttp_get_error_pos(&native->parser);
    const int64_t offset = position == nullptr ? 0 : position - data;
    llhttp_resume_after_upgrade(&native->parser);
    package->host->buffer_release(buffer);
    if (import_error_class(package, runtime, 5, "HttpParserUpgrade") !=
        X3_STATUS_OK) return X3_STATUS_ERROR;
    X3Value argument = x3_value_int64(offset);
    X3Value exception = x3_value_invalid();
    X3Status raised = package->host->call(
        runtime, package->errors[5], &argument, 1, &exception);
    if (raised == X3_STATUS_OK)
      raised = package->host->raise_exception(context, exception);
    package->host->value_release(exception);
    return raised == X3_STATUS_OK ? X3_STATUS_ERROR : raised;
  }
  package->host->buffer_release(buffer);
  if (status != HPE_OK) return raise_parser_error(native, context, runtime, status);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status get_http_version(X3CallContext* context, X3Runtime* runtime,
                          void* user_data, const X3Value* args, uint32_t argc,
                          X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, context, "get_http_version() takes no arguments");
  ParserState* native = require_parser(package, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const std::string version = std::to_string(native->parser.http_major) + "." +
                              std::to_string(native->parser.http_minor);
  *result = package->host->value_string(runtime, version.c_str());
  return X3_STATUS_OK;
}

X3Status should_keep_alive(X3CallContext* context, X3Runtime*, void* user_data,
                           const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, context, "should_keep_alive() takes no arguments");
  ParserState* native = require_parser(package, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(llhttp_should_keep_alive(&native->parser));
  return X3_STATUS_OK;
}

X3Status should_upgrade(X3CallContext* context, X3Runtime*, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, context, "should_upgrade() takes no arguments");
  ParserState* native = require_parser(package, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(native->parser.upgrade != 0);
  return X3_STATUS_OK;
}

X3Status get_method(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, context, "get_method() takes no arguments");
  ParserState* native = require_parser(package, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const char* method = llhttp_method_name(static_cast<llhttp_method_t>(native->parser.method));
  *result = method == nullptr ? x3_value_none() :
             package->host->value_bytes(runtime, method, std::strlen(method));
  return X3_STATUS_OK;
}

X3Status get_status_code(X3CallContext* context, X3Runtime*, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(package, context, "get_status_code() takes no arguments");
  ParserState* native = require_parser(package, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(native->parser.status_code);
  return X3_STATUS_OK;
}

void set_leniency(llhttp_t* parser, size_t index, bool enabled) {
  const int flag = enabled ? 1 : 0;
  switch (index) {
    case 0: llhttp_set_lenient_headers(parser, flag); break;
    case 1: llhttp_set_lenient_chunked_length(parser, flag); break;
    case 2: llhttp_set_lenient_keep_alive(parser, flag); break;
    case 3: llhttp_set_lenient_transfer_encoding(parser, flag); break;
    case 4: llhttp_set_lenient_version(parser, flag); break;
    case 5: llhttp_set_lenient_data_after_close(parser, flag); break;
    case 6: llhttp_set_lenient_optional_lf_after_cr(parser, flag); break;
    case 7: llhttp_set_lenient_optional_cr_before_lf(parser, flag); break;
    case 8: llhttp_set_lenient_optional_crlf_after_chunk(parser, flag); break;
    case 9: llhttp_set_lenient_spaces_after_chunk_size(parser, flag); break;
  }
}

X3Status set_dangerous_leniencies_kw(X3CallContext* context, X3Runtime*,
                                     void* user_data, const X3Value* args,
                                     uint32_t argc, const X3KeywordArg* kwargs,
                                     uint32_t kwargc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 11)
    return type_error(package, context, "set_dangerous_leniencies() accepts ten options");
  ParserState* native = require_parser(package, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  std::array<X3Value, 10> options{};
  options.fill(x3_value_none());
  for (uint32_t index = 1; index < argc; ++index) options[index - 1] = args[index];
  for (uint32_t index = 0; index < kwargc; ++index) {
    size_t slot = kLeniencyNames.size();
    for (size_t candidate = 0; candidate < kLeniencyNames.size(); ++candidate) {
      if (std::strcmp(kwargs[index].name, kLeniencyNames[candidate]) == 0) {
        slot = candidate;
        break;
      }
    }
    if (slot == kLeniencyNames.size())
      return type_error(package, context, "unknown leniency option");
    if (slot < argc - 1)
      return type_error(package, context, "duplicate leniency option");
    options[slot] = kwargs[index].value;
  }
  for (size_t index = 0; index < options.size(); ++index) {
    const X3Value option = options[index];
    if (option.tag == X3_TAG_NONE) continue;
    bool enabled = false;
    if (option.tag == X3_TAG_BOOL) enabled = option.as.b != 0;
    else if (option.tag == X3_TAG_INT64) enabled = option.as.i64 != 0;
    else return type_error(package, context, "leniency option must be a bool");
    set_leniency(&native->parser, index, enabled);
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status set_dangerous_leniencies(X3CallContext* context, X3Runtime* runtime,
                                  void* user_data, const X3Value* args,
                                  uint32_t argc, X3Value* result) {
  return set_dangerous_leniencies_kw(context, runtime, user_data, args, argc,
                                     nullptr, 0, result);
}

void define_method(X3NativeFunctionDef& method, const char* name,
                   X3NativeFn callback, PackageState* state,
                   X3NativeKeywordFn keyword_callback = nullptr) {
  method = {sizeof(X3NativeFunctionDef), name, callback, state, 0, 0, 0,
            keyword_callback};
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "httptools.parser.parser", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef request_methods[7]{};
  define_method(request_methods[0], "__init__", request_init, state);
  define_method(request_methods[1], "feed_data", feed_data, state);
  define_method(request_methods[2], "get_http_version", get_http_version, state);
  define_method(request_methods[3], "should_keep_alive", should_keep_alive, state);
  define_method(request_methods[4], "should_upgrade", should_upgrade, state);
  define_method(request_methods[5], "set_dangerous_leniencies",
                set_dangerous_leniencies, state, set_dangerous_leniencies_kw);
  define_method(request_methods[6], "get_method", get_method, state);
  if (host->module_add_class(module, "HttpRequestParser", request_methods, 7,
                              &state->request_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef response_methods[7]{};
  define_method(response_methods[0], "__init__", response_init, state);
  define_method(response_methods[1], "feed_data", feed_data, state);
  define_method(response_methods[2], "get_http_version", get_http_version, state);
  define_method(response_methods[3], "should_keep_alive", should_keep_alive, state);
  define_method(response_methods[4], "should_upgrade", should_upgrade, state);
  define_method(response_methods[5], "set_dangerous_leniencies",
                set_dangerous_leniencies, state, set_dangerous_leniencies_kw);
  define_method(response_methods[6], "get_status_code", get_status_code, state);
  return host->module_add_class(module, "HttpResponseParser", response_methods,
                                 7, &state->response_class);
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (host == nullptr || host->abi_version != X3_ABI_VERSION)
    return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "httptools.parser.parser");
  host->package_set_metadata(host, "version", "0.8.0+xlang3");
  return register_module(host);
}
