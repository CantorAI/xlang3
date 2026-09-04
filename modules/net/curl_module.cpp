/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "curl_module.h"

#include <cstring>

namespace xlang_net {
namespace {

bool is_string_value(const X::Value& value) {
  return value.raw().tag == X3_TAG_OBJECT && value.host() != nullptr &&
         value.host()->value_object_kind != nullptr &&
         value.host()->value_object_kind(value.raw()) == X3_OBJECT_KIND_STRING;
}

long numeric_value(const X::Value& value) {
  return static_cast<long>(value.ToLongLong());
}

} // namespace

Curl::Curl() : curl_(curl_easy_init()) {
  if (curl_ != nullptr) {
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, &Curl::WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_buffer_);
  }
}

Curl::~Curl() {
  if (headers_ != nullptr) {
    curl_slist_free_all(headers_);
    headers_ = nullptr;
  }
  if (curl_ != nullptr) {
    curl_easy_cleanup(curl_);
    curl_ = nullptr;
  }
}

size_t Curl::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  auto* self = static_cast<Curl*>(userp);
  const size_t total = size * nmemb;
  if (self != nullptr && contents != nullptr) {
    self->response_.append(static_cast<const char*>(contents), total);
  }
  return total;
}

long Curl::OptionCode(const X::Value& option) const {
  if (!is_string_value(option)) {
    return numeric_value(option);
  }
  const std::string name = option.ToString(false);
  if (name == "URL" || name == "CURLOPT_URL") return CURLOPT_URL;
  if (name == "PORT" || name == "CURLOPT_PORT") return CURLOPT_PORT;
  if (name == "POST" || name == "CURLOPT_POST") return CURLOPT_POST;
  if (name == "POSTFIELDS" || name == "CURLOPT_POSTFIELDS") return CURLOPT_POSTFIELDS;
  if (name == "HTTPHEADER" || name == "CURLOPT_HTTPHEADER") return CURLOPT_HTTPHEADER;
  if (name == "FOLLOWLOCATION" || name == "CURLOPT_FOLLOWLOCATION") return CURLOPT_FOLLOWLOCATION;
  if (name == "TIMEOUT" || name == "CURLOPT_TIMEOUT") return CURLOPT_TIMEOUT;
  if (name == "USERAGENT" || name == "CURLOPT_USERAGENT") return CURLOPT_USERAGENT;
  if (name == "VERBOSE" || name == "CURLOPT_VERBOSE") return CURLOPT_VERBOSE;
  if (name == "CUSTOMREQUEST" || name == "CURLOPT_CUSTOMREQUEST") return CURLOPT_CUSTOMREQUEST;
  if (name == "PROXY" || name == "CURLOPT_PROXY") return CURLOPT_PROXY;
  if (name == "PROXYPORT" || name == "CURLOPT_PROXYPORT") return CURLOPT_PROXYPORT;
  if (name == "COOKIE" || name == "CURLOPT_COOKIE") return CURLOPT_COOKIE;
  if (name == "SSL_VERIFYPEER" || name == "CURLOPT_SSL_VERIFYPEER") return CURLOPT_SSL_VERIFYPEER;
  if (name == "SSL_VERIFYHOST" || name == "CURLOPT_SSL_VERIFYHOST") return CURLOPT_SSL_VERIFYHOST;
  return -1;
}

long Curl::InfoCode(const X::Value& info) const {
  if (!is_string_value(info)) {
    return numeric_value(info);
  }
  const std::string name = info.ToString(false);
  if (name == "RESPONSE_CODE" || name == "CURLINFO_RESPONSE_CODE") return CURLINFO_RESPONSE_CODE;
  if (name == "TOTAL_TIME" || name == "CURLINFO_TOTAL_TIME") return CURLINFO_TOTAL_TIME;
  if (name == "CONTENT_TYPE" || name == "CURLINFO_CONTENT_TYPE") return CURLINFO_CONTENT_TYPE;
  if (name == "EFFECTIVE_URL" || name == "CURLINFO_EFFECTIVE_URL") return CURLINFO_EFFECTIVE_URL;
  return -1;
}

bool Curl::SetHeaders(const X::Value& value) {
  if (headers_ != nullptr) {
    curl_slist_free_all(headers_);
    headers_ = nullptr;
  }
  if (value.IsList()) {
    const uint64_t count = value.Size();
    for (uint64_t i = 0; i < count; ++i) {
      const std::string header = value.Get(i).ToString(false);
      headers_ = curl_slist_append(headers_, header.c_str());
    }
  } else if (value.IsDict()) {
    const uint64_t count = value.Size();
    for (uint64_t i = 0; i < count; ++i) {
      X::Value key;
      X::Value item;
      if (value.DictEntry(i, key, item)) {
        const std::string header = key.ToString(false) + ": " + item.ToString(false);
        headers_ = curl_slist_append(headers_, header.c_str());
      }
    }
  } else {
    const std::string header = value.ToString(false);
    headers_ = curl_slist_append(headers_, header.c_str());
  }
  return curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_) == CURLE_OK;
}

bool Curl::SetOpt(X::Value option, X::Value value) {
  if (curl_ == nullptr) return false;
  const long option_code = OptionCode(option);
  if (option_code < 0) {
    error_ = "Unsupported curl option";
    return false;
  }

  CURLcode result = CURLE_OK;
  switch (option_code) {
    case CURLOPT_URL:
    case CURLOPT_POSTFIELDS:
    case CURLOPT_USERAGENT:
    case CURLOPT_CUSTOMREQUEST:
    case CURLOPT_PROXY:
    case CURLOPT_COOKIE: {
      string_options_.push_back(value.ToString(false));
      result = curl_easy_setopt(curl_, static_cast<CURLoption>(option_code), string_options_.back().c_str());
      break;
    }
    case CURLOPT_HTTPHEADER:
      return SetHeaders(value);
    case CURLOPT_FOLLOWLOCATION:
    case CURLOPT_POST:
    case CURLOPT_VERBOSE:
    case CURLOPT_SSL_VERIFYPEER:
      result = curl_easy_setopt(curl_, static_cast<CURLoption>(option_code), value.ToLongLong() != 0 ? 1L : 0L);
      break;
    case CURLOPT_SSL_VERIFYHOST:
      result = curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, value.ToLongLong() != 0 ? 2L : 0L);
      break;
    case CURLOPT_PORT:
    case CURLOPT_TIMEOUT:
    case CURLOPT_PROXYPORT:
      result = curl_easy_setopt(curl_, static_cast<CURLoption>(option_code), numeric_value(value));
      break;
    default:
      error_ = "Unsupported curl option";
      return false;
  }

  if (result != CURLE_OK) {
    error_ = error_buffer_[0] != '\0' ? error_buffer_ : curl_easy_strerror(result);
    return false;
  }
  return true;
}

bool Curl::Perform() {
  if (curl_ == nullptr) return false;
  response_.clear();
  error_.clear();
  std::memset(error_buffer_, 0, sizeof(error_buffer_));
  const CURLcode result = curl_easy_perform(curl_);
  if (result != CURLE_OK) {
    error_ = error_buffer_[0] != '\0' ? error_buffer_ : curl_easy_strerror(result);
    return false;
  }
  return true;
}

X::Value Curl::GetInfo(X::Value info) const {
  if (curl_ == nullptr) return X::Value(false);
  const long info_code = InfoCode(info);
  if (info_code == CURLINFO_RESPONSE_CODE) {
    long value = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &value);
    return X::Value(static_cast<long long>(value));
  }
  if (info_code == CURLINFO_TOTAL_TIME) {
    double value = 0;
    curl_easy_getinfo(curl_, CURLINFO_TOTAL_TIME, &value);
    return X::Value(value);
  }
  if (info_code == CURLINFO_CONTENT_TYPE) {
    char* value = nullptr;
    curl_easy_getinfo(curl_, CURLINFO_CONTENT_TYPE, &value);
    return X::Value::String(Host(), value == nullptr ? "" : value);
  }
  if (info_code == CURLINFO_EFFECTIVE_URL) {
    char* value = nullptr;
    curl_easy_getinfo(curl_, CURLINFO_EFFECTIVE_URL, &value);
    return X::Value::String(Host(), value == nullptr ? "" : value);
  }
  return X::Value(false);
}

bool Curl::Reset() {
  if (curl_ == nullptr) return false;
  curl_easy_reset(curl_);
  if (headers_ != nullptr) {
    curl_slist_free_all(headers_);
    headers_ = nullptr;
  }
  string_options_.clear();
  response_.clear();
  error_.clear();
  std::memset(error_buffer_, 0, sizeof(error_buffer_));
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, &Curl::WriteCallback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, this);
  curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_buffer_);
  return true;
}

X::Value Curl::Escape(std::string value) const {
  if (curl_ == nullptr) return X::Value::String(Host(), "");
  char* escaped = curl_easy_escape(curl_, value.c_str(), static_cast<int>(value.size()));
  const std::string result = escaped == nullptr ? std::string() : std::string(escaped);
  if (escaped != nullptr) curl_free(escaped);
  return X::Value::String(Host(), result);
}

X::Value Curl::Unescape(std::string value) const {
  if (curl_ == nullptr) return X::Value::String(Host(), "");
  int size = 0;
  char* unescaped = curl_easy_unescape(curl_, value.c_str(), static_cast<int>(value.size()), &size);
  const std::string result = unescaped == nullptr ? std::string() : std::string(unescaped, static_cast<size_t>(size));
  if (unescaped != nullptr) curl_free(unescaped);
  return X::Value::String(Host(), result);
}

X::Value Curl::Response() const {
  return X::Value::String(Host(), response_);
}

X::Value Curl::Error() const {
  return X::Value::String(Host(), error_);
}

} // namespace xlang_net
