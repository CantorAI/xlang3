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
#include "json_convert.h"

#include <cstdint>

namespace xlang3_json {

namespace {

bool value_to_json_list(const X3PackageHost* host, X3Runtime* runtime, X3Value value, Json& out, const char** error) {
  uint64_t length = 0;
  if (host->len(runtime, value, &length) != X3_STATUS_OK) {
    *error = host->runtime_last_error(runtime);
    return false;
  }
  out = Json::array();
  for (uint64_t i = 0; i < length; ++i) {
    X3Value item = x3_value_invalid();
    if (host->get_item(runtime, value, x3_value_int64(static_cast<int64_t>(i)), &item) != X3_STATUS_OK) {
      *error = host->runtime_last_error(runtime);
      return false;
    }
    Json item_json;
    const bool ok = value_to_json(host, runtime, item, item_json, error);
    host->value_release(item);
    if (!ok) {
      return false;
    }
    out.push_back(std::move(item_json));
  }
  return true;
}

bool value_to_json_dict(const X3PackageHost* host, X3Runtime* runtime, X3Value value, Json& out, const char** error) {
  uint64_t length = 0;
  if (host->len(runtime, value, &length) != X3_STATUS_OK) {
    *error = host->runtime_last_error(runtime);
    return false;
  }
  out = Json::object();
  for (uint64_t i = 0; i < length; ++i) {
    X3Value key = x3_value_invalid();
    X3Value item = x3_value_invalid();
    if (host->dict_get_entry(runtime, value, i, &key, &item) != X3_STATUS_OK) {
      *error = host->runtime_last_error(runtime);
      return false;
    }
    if (host->value_object_kind(key) != X3_OBJECT_KIND_STRING) {
      host->value_release(key);
      host->value_release(item);
      *error = "json.dumps() currently requires string dict keys";
      return false;
    }
    Json item_json;
    const bool ok = value_to_json(host, runtime, item, item_json, error);
    if (ok) {
      out[host->value_to_cstr(runtime, key)] = std::move(item_json);
    }
    host->value_release(key);
    host->value_release(item);
    if (!ok) {
      return false;
    }
  }
  return true;
}

} // namespace

X3Value json_to_value(const X3PackageHost* host, X3Runtime* runtime, const Json& json) {
  if (json.is_null()) {
    return x3_value_none();
  }
  if (json.is_boolean()) {
    return x3_value_bool(json.get<bool>() ? 1 : 0);
  }
  if (json.is_number_integer()) {
    return x3_value_int64(json.get<int64_t>());
  }
  if (json.is_number_unsigned()) {
    return x3_value_uint64(json.get<uint64_t>());
  }
  if (json.is_number_float()) {
    return x3_value_double(json.get<double>());
  }
  if (json.is_string()) {
    return host->value_string(runtime, json.get<std::string>().c_str());
  }
  if (json.is_array()) {
    X3Value list = host->value_list(runtime);
    for (const auto& item : json) {
      X3Value value = json_to_value(host, runtime, item);
      host->list_append(runtime, list, value);
      host->value_release(value);
    }
    return list;
  }
  if (json.is_object()) {
    X3Value dict = host->value_dict(runtime);
    for (auto it = json.begin(); it != json.end(); ++it) {
      X3Value key = host->value_string(runtime, it.key().c_str());
      X3Value value = json_to_value(host, runtime, it.value());
      host->dict_set_item(runtime, dict, key, value);
      host->value_release(key);
      host->value_release(value);
    }
    return dict;
  }
  return x3_value_none();
}

bool value_to_json(const X3PackageHost* host, X3Runtime* runtime, X3Value value, Json& out, const char** error) {
  switch (value.tag) {
    case X3_TAG_INVALID:
      *error = "cannot encode invalid value as JSON";
      return false;
    case X3_TAG_NONE:
      out = nullptr;
      return true;
    case X3_TAG_BOOL:
      out = value.as.b != 0;
      return true;
    case X3_TAG_INT64:
      out = value.as.i64;
      return true;
    case X3_TAG_UINT64:
      out = value.as.u64;
      return true;
    case X3_TAG_DOUBLE:
      out = value.as.f64;
      return true;
    case X3_TAG_OBJECT:
      break;
    default:
      *error = "unknown value tag";
      return false;
  }

  switch (host->value_object_kind(value)) {
    case X3_OBJECT_KIND_STRING:
      out = host->value_to_cstr(runtime, value);
      return true;
    case X3_OBJECT_KIND_LIST:
    case X3_OBJECT_KIND_TUPLE:
      return value_to_json_list(host, runtime, value, out, error);
    case X3_OBJECT_KIND_DICT:
      return value_to_json_dict(host, runtime, value, out, error);
    default:
      *error = "object is not JSON serializable";
      return false;
  }
}

} // namespace xlang3_json
