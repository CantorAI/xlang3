/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "net_common.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>

namespace xlang_net {


std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string file_extension(const std::string& path) {
  auto dot = path.find_last_of('.');
  return dot == std::string::npos ? std::string() : lower(path.substr(dot + 1));
}

std::pair<std::string, bool> mime_type_and_binary(const std::string& extension) {
  static const std::map<std::string, std::pair<std::string, bool>> map = {
      {"txt", {"text/plain", false}}, {"html", {"text/html", false}}, {"htm", {"text/html", false}},
      {"css", {"text/css", false}}, {"csv", {"text/csv", false}}, {"xml", {"application/xml", false}},
      {"json", {"application/json", false}}, {"js", {"application/javascript", false}},
      {"mjs", {"application/javascript", false}}, {"md", {"text/markdown", false}},
      {"png", {"image/png", true}}, {"jpg", {"image/jpeg", true}}, {"jpeg", {"image/jpeg", true}},
      {"gif", {"image/gif", true}}, {"svg", {"image/svg+xml", true}}, {"webp", {"image/webp", true}},
      {"ico", {"image/x-icon", true}}, {"pdf", {"application/pdf", true}}, {"zip", {"application/zip", true}},
      {"bin", {"application/octet-stream", true}}, {"wasm", {"application/wasm", true}}};
  auto it = map.find(lower(extension));
  return it == map.end() ? std::make_pair(std::string("application/octet-stream"), true) : it->second;
}

X::Value headers_to_dict(X3PackageHost* host, const httplib::Headers& headers) {
  auto dict = X::Value::Dict(host);
  for (const auto& item : headers) {
    dict.Set(item.first.c_str(), X::Value::String(host, item.second));
  }
  return dict;
}

httplib::Headers dict_to_headers(const X::Value& value) {
  httplib::Headers headers;
  if (!value.IsDict()) {
    return headers;
  }
  const uint64_t size = value.Size();
  for (uint64_t i = 0; i < size; ++i) {
    X::Value key;
    X::Value item;
    if (value.DictEntry(i, key, item)) {
      headers.emplace(key.ToString(false), item.ToString(false));
    }
  }
  return headers;
}

bool is_text_type(const httplib::Headers& headers) {
  auto it = headers.find("Content-Type");
  if (it == headers.end()) {
    return false;
  }
  const std::string content_type = lower(it->second);
  return content_type.rfind("text/", 0) == 0 ||
         content_type.find("json") != std::string::npos ||
         content_type.find("xml") != std::string::npos ||
         content_type.find("javascript") != std::string::npos ||
         content_type.find("x-www-form-urlencoded") != std::string::npos;
}

std::string translate_route_pattern(const std::string& url) {
  if (url == "/") {
    return "^/$";
  }
  static const std::regex placeholder("<[^>]*>");
  return "^" + std::regex_replace(url, placeholder, "([^/&?]*)") + "(?:\\?.*)?$";
}

bool InlineTaskQueue::enqueue(std::function<void()> fn) {
  if (shutdown_) return false;
  fn();
  return true;
}

void InlineTaskQueue::shutdown() {
  shutdown_ = true;
}

} // namespace xlang_net
