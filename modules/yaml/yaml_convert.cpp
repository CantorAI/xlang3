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
#include "yaml_convert.h"

#include <cstdint>
#include <regex>
#include <string>

namespace xlang3_yaml {

namespace {

X3Value scalar_to_value(const X3PackageHost* host, X3Runtime* runtime, const YAML::Node& node) {
  const std::string raw = node.Scalar();
  if (node.Tag() == "!!str" || node.Tag() == "!") {
    return host->value_string(runtime, raw.c_str());
  }
  if (std::regex_match(raw, std::regex("^[-+]?[0-9]+$"))) {
    try {
      return x3_value_int64(std::stoll(raw));
    } catch (...) {
      return host->value_string(runtime, raw.c_str());
    }
  }
  if (std::regex_match(raw, std::regex("^[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$"))) {
    try {
      return x3_value_double(std::stod(raw));
    } catch (...) {
      return host->value_string(runtime, raw.c_str());
    }
  }
  if (raw == "true" || raw == "True" || raw == "TRUE" ||
      raw == "yes" || raw == "Yes" || raw == "YES" ||
      raw == "on" || raw == "On" || raw == "ON") {
    return x3_value_bool(1);
  }
  if (raw == "false" || raw == "False" || raw == "FALSE" ||
      raw == "no" || raw == "No" || raw == "NO" ||
      raw == "off" || raw == "Off" || raw == "OFF") {
    return x3_value_bool(0);
  }
  return host->value_string(runtime, raw.c_str());
}

bool value_to_yaml_list(const X3PackageHost* host, X3Runtime* runtime, X3Value value, YAML::Node& out, const char** error) {
  uint64_t length = 0;
  if (host->len(runtime, value, &length) != X3_STATUS_OK) {
    *error = host->runtime_last_error(runtime);
    return false;
  }
  out = YAML::Node(YAML::NodeType::Sequence);
  for (uint64_t i = 0; i < length; ++i) {
    X3Value item = x3_value_invalid();
    YAML::Node item_node;
    if (host->get_item(runtime, value, x3_value_int64(static_cast<int64_t>(i)), &item) != X3_STATUS_OK) {
      *error = host->runtime_last_error(runtime);
      return false;
    }
    const bool ok = value_to_yaml(host, runtime, item, item_node, error);
    host->value_release(item);
    if (!ok) {
      return false;
    }
    out.push_back(item_node);
  }
  return true;
}

bool value_to_yaml_dict(const X3PackageHost* host, X3Runtime* runtime, X3Value value, YAML::Node& out, const char** error) {
  uint64_t length = 0;
  if (host->len(runtime, value, &length) != X3_STATUS_OK) {
    *error = host->runtime_last_error(runtime);
    return false;
  }
  out = YAML::Node(YAML::NodeType::Map);
  for (uint64_t i = 0; i < length; ++i) {
    X3Value key = x3_value_invalid();
    X3Value item = x3_value_invalid();
    YAML::Node item_node;
    if (host->dict_get_entry(runtime, value, i, &key, &item) != X3_STATUS_OK) {
      *error = host->runtime_last_error(runtime);
      return false;
    }
    if (host->value_object_kind(key) != X3_OBJECT_KIND_STRING) {
      host->value_release(key);
      host->value_release(item);
      *error = "yaml.saves() currently requires string dict keys";
      return false;
    }
    const char* key_text = host->value_to_cstr(runtime, key);
    const bool ok = value_to_yaml(host, runtime, item, item_node, error);
    if (ok) {
      out[key_text] = item_node;
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

X3Value yaml_to_value(const X3PackageHost* host, X3Runtime* runtime, const YAML::Node& node) {
  if (!node.IsDefined() || node.IsNull()) {
    return x3_value_none();
  }
  if (node.IsScalar()) {
    return scalar_to_value(host, runtime, node);
  }
  if (node.IsSequence()) {
    X3Value list = host->value_list(runtime);
    for (const auto& item : node) {
      X3Value value = yaml_to_value(host, runtime, item);
      host->list_append(runtime, list, value);
      host->value_release(value);
    }
    return list;
  }
  if (node.IsMap()) {
    X3Value dict = host->value_dict(runtime);
    for (const auto& item : node) {
      if (!item.first.IsDefined() || !item.first.IsScalar()) {
        continue;
      }
      X3Value key = host->value_string(runtime, item.first.Scalar().c_str());
      X3Value value = yaml_to_value(host, runtime, item.second);
      host->dict_set_item(runtime, dict, key, value);
      host->value_release(key);
      host->value_release(value);
    }
    return dict;
  }
  return x3_value_none();
}

bool value_to_yaml(const X3PackageHost* host, X3Runtime* runtime, X3Value value, YAML::Node& out, const char** error) {
  switch (value.tag) {
    case X3_TAG_INVALID:
      *error = "cannot encode invalid value as YAML";
      return false;
    case X3_TAG_NONE:
      out = YAML::Node(YAML::NodeType::Null);
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
      return value_to_yaml_list(host, runtime, value, out, error);
    case X3_OBJECT_KIND_DICT:
      return value_to_yaml_dict(host, runtime, value, out, error);
    default:
      *error = "object is not YAML serializable";
      return false;
  }
}

} // namespace xlang3_yaml
