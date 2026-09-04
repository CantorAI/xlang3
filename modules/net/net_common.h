/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "httplib.h"
#include "xlang3/cpp/xpackage.h"

#include <functional>
#include <string>
#include <utility>

namespace xlang_net {

std::string lower(std::string value);
std::string file_extension(const std::string& path);
std::pair<std::string, bool> mime_type_and_binary(const std::string& extension);
X::Value headers_to_dict(X3PackageHost* host, const httplib::Headers& headers);
httplib::Headers dict_to_headers(const X::Value& value);
bool is_text_type(const httplib::Headers& headers);
std::string translate_route_pattern(const std::string& url);

class InlineTaskQueue final : public httplib::TaskQueue {
public:
  bool enqueue(std::function<void()> fn) override;
  void shutdown() override;

private:
  bool shutdown_ = false;
};

extern X::Value g_request_class;
extern X::Value g_response_class;

template <typename T>
X::Value create_native_instance(X3PackageHost* host, const X::Value& klass, T* object) {
  if (host == nullptr || !klass.IsValid() || object == nullptr || host->value_instance == nullptr ||
      host->instance_set_native_data == nullptr) {
    delete object;
    return {};
  }
  X3Value raw_instance = host->value_instance(host->runtime, klass.raw());
  if (raw_instance.tag == X3_TAG_INVALID) {
    delete object;
    return {};
  }
  object->__xlang3_host_ = host;
  if (host->instance_set_native_data(
          raw_instance,
          X::detail::native_type_name<T>(),
          object,
          X::detail::cleanup_native_instance<T>) != X3_STATUS_OK) {
    if (host->value_release != nullptr) host->value_release(raw_instance);
    delete object;
    return {};
  }
  return X::Value(host, raw_instance, false);
}

} // namespace xlang_net
