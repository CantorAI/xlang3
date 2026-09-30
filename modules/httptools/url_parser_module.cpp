/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

extern "C" {
#include "http_parser.h"
}

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace {

constexpr const char* kUrlType = "httptools.parser.url_parser.URL";
constexpr uint64_t kMaxUrlLength = (1u << 16) - 1;
constexpr std::array<const char*, 7> kFieldNames = {
    "schema", "host", "port", "path", "query", "fragment", "userinfo"};

struct PackageState;
struct FieldSpec {
  PackageState* package = nullptr;
  size_t index = 0;
};

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value url_class = x3_value_invalid();
  X3Value invalid_url_error = x3_value_invalid();
  std::array<FieldSpec, 7> fields{};
};

struct UrlState {
  X3PackageHost* host = nullptr;
  std::array<X3Value, 7> fields{};

  explicit UrlState(X3PackageHost* owner) : host(owner) {
    fields.fill(x3_value_none());
  }

  ~UrlState() {
    for (const X3Value field : fields) host->value_release(field);
  }
};

void cleanup_url(void* data) { delete static_cast<UrlState*>(data); }

void cleanup_package(void* data) {
  auto* state = static_cast<PackageState*>(data);
  if (state == nullptr) return;
  state->host->value_release(state->url_class);
  state->host->value_release(state->invalid_url_error);
  delete state;
}

X3Status type_error(PackageState* state, X3CallContext* context,
                    const char* message) {
  return state->host->raise_class_error(context, "TypeError", message);
}

UrlState* require_url(PackageState* state, X3CallContext* context,
                      X3Value instance) {
  auto* url = static_cast<UrlState*>(
      state->host->instance_get_native_data(instance, kUrlType));
  if (url == nullptr) type_error(state, context, "expected httptools URL");
  return url;
}

X3Status import_error_class(PackageState* state, X3Runtime* runtime) {
  if (state->invalid_url_error.tag != X3_TAG_INVALID) return X3_STATUS_OK;
  X3Value importer = x3_value_invalid();
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  X3Value name = state->host->value_string(runtime, "httptools.parser.errors");
  X3Value fromlist = state->host->value_list(runtime);
  X3Value item = state->host->value_string(runtime, "HttpParserInvalidURLError");
  X3Status status = state->host->list_append(runtime, fromlist, item);
  X3Value module = x3_value_invalid();
  if (status == X3_STATUS_OK) {
    const X3Value args[] = {name, x3_value_none(), x3_value_none(), fromlist};
    status = state->host->call(runtime, importer, args, 4, &module);
  }
  if (status == X3_STATUS_OK) {
    status = state->host->get_attr(runtime, module,
                                   "HttpParserInvalidURLError",
                                   &state->invalid_url_error);
  }
  state->host->value_release(module);
  state->host->value_release(item);
  state->host->value_release(fromlist);
  state->host->value_release(name);
  state->host->value_release(importer);
  return status;
}

X3Status raise_invalid_url(PackageState* state, X3CallContext* context,
                           X3Runtime* runtime, const std::string& message) {
  if (import_error_class(state, runtime) != X3_STATUS_OK) return X3_STATUS_ERROR;
  return state->host->raise_error(context, state->invalid_url_error,
                                  message.c_str());
}

X3Status value_repr(PackageState* state, X3Runtime* runtime, X3Value value,
                    std::string& output) {
  X3Value callable = x3_value_invalid();
  if (state->host->builtin_value(state->host, "repr", &callable) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value rendered = x3_value_invalid();
  const X3Status status = state->host->call(runtime, callable, &value, 1,
                                             &rendered);
  if (status == X3_STATUS_OK) {
    const char* text = state->host->value_to_cstr(runtime, rendered);
    if (text != nullptr) output = text;
  }
  state->host->value_release(rendered);
  state->host->value_release(callable);
  return status;
}

X3Status url_init(X3CallContext* context, X3Runtime*, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 8) return type_error(state, context, "URL() requires seven fields");
  auto url = std::make_unique<UrlState>(state->host);
  for (size_t index = 0; index < url->fields.size(); ++index) {
    url->fields[index] = args[index + 1];
    state->host->value_retain(url->fields[index]);
  }
  if (state->host->instance_set_native_data(args[0], kUrlType, url.get(),
                                            cleanup_url) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  url.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status url_property(X3CallContext* context, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* spec = static_cast<FieldSpec*>(user_data);
  if (argc != 1) return type_error(spec->package, context, "invalid URL property access");
  UrlState* url = require_url(spec->package, context, args[0]);
  if (url == nullptr) return X3_STATUS_ERROR;
  *result = url->fields[spec->index];
  spec->package->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status url_property_set(X3CallContext* context, X3Runtime*, void* user_data,
                          const X3Value*, uint32_t, X3Value*) {
  auto* spec = static_cast<FieldSpec*>(user_data);
  const std::string message = std::string("attribute '") +
                              kFieldNames[spec->index] + "' is not writable";
  return spec->package->host->raise_class_error(context, "AttributeError",
                                                 message.c_str());
}

X3Status url_repr(X3CallContext* context, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "URL.__repr__() takes no arguments");
  UrlState* url = require_url(state, context, args[0]);
  if (url == nullptr) return X3_STATUS_ERROR;
  std::string text = "<URL ";
  for (size_t index = 0; index < url->fields.size(); ++index) {
    if (index != 0) text += ", ";
    std::string rendered;
    if (value_repr(state, runtime, url->fields[index], rendered) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    text += kFieldNames[index];
    text += ": ";
    text += rendered;
  }
  text += ">";
  *result = state->host->value_string(runtime, text.c_str());
  return X3_STATUS_OK;
}

X3Status parse_url(X3CallContext* context, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return type_error(state, context, "parse_url() takes one argument");

  const X3ObjectKind kind = state->host->value_object_kind(args[0]);
  if (kind != X3_OBJECT_KIND_BYTES && kind != X3_OBJECT_KIND_BYTEARRAY &&
      kind != X3_OBJECT_KIND_MEMORYVIEW)
    return type_error(state, context, "a bytes-like object is required");

  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &buffer, &info) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (info.size > kMaxUrlLength) {
    state->host->buffer_release(buffer);
    return raise_invalid_url(
        state, context, runtime,
        "url is too long: url length of " + std::to_string(info.size) +
            " bytes exceeds the maximum of " + std::to_string(kMaxUrlLength) +
            " bytes");
  }

  http_parser_url parsed{};
  http_parser_url_init(&parsed);
  const char* data = static_cast<const char*>(info.data);
  const int status = http_parser_parse_url(data, static_cast<size_t>(info.size),
                                           0, &parsed);
  if (status != 0) {
    state->host->buffer_release(buffer);
    std::string rendered;
    if (value_repr(state, runtime, args[0], rendered) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    return raise_invalid_url(state, context, runtime, "invalid url " + rendered);
  }

  auto url = std::make_unique<UrlState>(state->host);
  const struct { http_parser_url_fields kind; size_t index; } mapping[] = {
      {UF_SCHEMA, 0}, {UF_HOST, 1}, {UF_PATH, 3}, {UF_QUERY, 4},
      {UF_FRAGMENT, 5}, {UF_USERINFO, 6}};
  for (const auto& entry : mapping) {
    if ((parsed.field_set & (1u << entry.kind)) == 0) continue;
    const auto& field = parsed.field_data[entry.kind];
    url->fields[entry.index] = state->host->value_bytes(
        runtime, data + field.off, field.len);
    if (url->fields[entry.index].tag == X3_TAG_INVALID) {
      state->host->buffer_release(buffer);
      return X3_STATUS_ERROR;
    }
  }
  if ((parsed.field_set & (1u << UF_PORT)) != 0)
    url->fields[2] = x3_value_int64(parsed.port);
  state->host->buffer_release(buffer);

  X3Value instance = state->host->value_instance(runtime, state->url_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kUrlType, url.get(),
                                            cleanup_url) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  url.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "httptools.parser.url_parser", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef methods[2]{};
  methods[0] = {sizeof(X3NativeFunctionDef), "__init__", url_init, state, 0, 0, 0, nullptr};
  methods[1] = {sizeof(X3NativeFunctionDef), "__repr__", url_repr, state, 0, 0, 0, nullptr};
  if (host->module_add_class(module, "URL", methods, 2, &state->url_class) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  for (size_t index = 0; index < state->fields.size(); ++index) {
    state->fields[index] = {state, index};
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, kFieldNames[index], url_property,
                              url_property_set, &state->fields[index], &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(state->url_class,
                                                   kFieldNames[index], property);
    host->value_release(property);
    if (status != X3_STATUS_OK) return status;
  }
  const X3NativeFunctionDef function = {
      sizeof(X3NativeFunctionDef), "parse_url", parse_url, state, 0, 0, 0, nullptr};
  return host->module_add_function(module, &function);
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (host == nullptr || host->abi_version != X3_ABI_VERSION)
    return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "httptools.parser.url_parser");
  host->package_set_metadata(host, "version", "0.8.0+xlang3");
  return register_module(host);
}
