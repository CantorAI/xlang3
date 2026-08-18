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
#pragma once

#include "xlang3/compiler.h"
#include "xlang3/sequence.h"
#include "xlang3/value.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace xlang3 {

XLANG3_HOT_INLINE const std::string* xlang_vm_string_ref(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    return nullptr;
  }
  return &reinterpret_cast<StringObject*>(value.as.obj)->value;
}

inline bool xlang_vm_fast_string_method(
    const Value& self,
    const std::string& name,
    const Value* regs,
    const std::vector<uint32_t>& arg_regs,
    Value& out,
    bool& handled,
    std::string& error) {
  handled = false;
  const auto* text = xlang_vm_string_ref(self);
  if (text == nullptr) {
    return false;
  }

  if (name == "strip" && arg_regs.empty()) {
    handled = true;
    size_t first = 0;
    while (first < text->size() && std::isspace(static_cast<unsigned char>((*text)[first]))) {
      ++first;
    }
    size_t last = text->size();
    while (last > first && std::isspace(static_cast<unsigned char>((*text)[last - 1]))) {
      --last;
    }
    out = Value::string(text->substr(first, last - first));
    return true;
  }

  if (name == "startswith" && arg_regs.size() == 1) {
    handled = true;
    const auto* prefix = xlang_vm_string_ref(regs[arg_regs[0]]);
    if (prefix == nullptr) {
      error = "str.startswith prefix must be a string";
      return false;
    }
    value_set_bool(out, text->rfind(*prefix, 0) == 0);
    return true;
  }

  if (name == "replace" && arg_regs.size() == 2) {
    handled = true;
    const auto* old_text = xlang_vm_string_ref(regs[arg_regs[0]]);
    const auto* new_text = xlang_vm_string_ref(regs[arg_regs[1]]);
    if (old_text == nullptr || new_text == nullptr) {
      error = "str.replace arguments must be strings";
      return false;
    }
    if (old_text->empty()) {
      out = Value::string(*text);
      return true;
    }
    if (old_text->size() == 1 && new_text->size() == 1) {
      std::string result = *text;
      const char old_ch = (*old_text)[0];
      const char new_ch = (*new_text)[0];
      for (auto& ch : result) {
        if (ch == old_ch) {
          ch = new_ch;
        }
      }
      out = Value::string(std::move(result));
      return true;
    }
    std::string result;
    result.reserve(text->size());
    size_t start = 0;
    for (;;) {
      const auto pos = text->find(*old_text, start);
      if (pos == std::string::npos) {
        break;
      }
      result.append(*text, start, pos - start);
      result += *new_text;
      start = pos + old_text->size();
    }
    result.append(*text, start, std::string::npos);
    out = Value::string(std::move(result));
    return true;
  }

  if (name == "split" && arg_regs.size() == 1) {
    handled = true;
    const auto* sep = xlang_vm_string_ref(regs[arg_regs[0]]);
    if (sep == nullptr) {
      error = "str.split separator must be a string";
      return false;
    }
    if (sep->empty()) {
      error = "empty separator";
      return false;
    }
    std::vector<Value> parts;
    parts.reserve(8);
    size_t start = 0;
    for (;;) {
      const auto pos = text->find(*sep, start);
      if (pos == std::string::npos) {
        parts.push_back(Value::string(text->substr(start)));
        break;
      }
      parts.push_back(Value::string(text->substr(start, pos - start)));
      start = pos + sep->size();
    }
    out = Value::list(std::move(parts));
    return true;
  }

  if (name == "join" && arg_regs.size() == 1) {
    handled = true;
    const auto* list = value_as_list(regs[arg_regs[0]]);
    const auto* tuple = value_as_tuple(regs[arg_regs[0]]);
    const std::vector<Value>* items = nullptr;
    if (list != nullptr) {
      items = &list->items;
    } else if (tuple != nullptr) {
      items = &tuple->items;
    } else {
      error = "str.join argument must be a sequence";
      return false;
    }
    size_t total_size = 0;
    for (const auto& item : *items) {
      const auto* item_text = xlang_vm_string_ref(item);
      if (item_text == nullptr) {
        error = "str.join item must be a string";
        return false;
      }
      total_size += item_text->size();
    }
    if (!items->empty()) {
      total_size += text->size() * (items->size() - 1);
    }
    std::string result;
    result.reserve(total_size);
    for (size_t i = 0; i < items->size(); ++i) {
      if (i != 0) {
        result += *text;
      }
      result += *xlang_vm_string_ref((*items)[i]);
    }
    out = Value::string(std::move(result));
    return true;
  }

  return false;
}

inline bool xlang_vm_string_strip_replace_split(
    const Value& source,
    const std::string& old_text,
    const std::string& new_text,
    const std::string& sep,
    std::vector<Value>& parts,
    std::string& error) {
  const auto* text = xlang_vm_string_ref(source);
  if (text == nullptr) {
    error = "string pipeline target must be a string";
    return false;
  }
  if (sep.empty()) {
    error = "empty separator";
    return false;
  }

  size_t first = 0;
  while (first < text->size() && std::isspace(static_cast<unsigned char>((*text)[first]))) {
    ++first;
  }
  size_t last = text->size();
  while (last > first && std::isspace(static_cast<unsigned char>((*text)[last - 1]))) {
    --last;
  }

  std::string replaced;
  if (old_text.empty()) {
    replaced.assign(*text, first, last - first);
  } else if (old_text.size() == 1 && new_text.size() == 1) {
    replaced.assign(*text, first, last - first);
    const char old_ch = old_text[0];
    const char new_ch = new_text[0];
    for (auto& ch : replaced) {
      if (ch == old_ch) {
        ch = new_ch;
      }
    }
  } else {
    replaced.reserve(last - first);
    size_t start = first;
    for (;;) {
      const auto pos = text->find(old_text, start);
      if (pos == std::string::npos || pos >= last) {
        break;
      }
      replaced.append(*text, start, pos - start);
      replaced += new_text;
      start = pos + old_text.size();
    }
    replaced.append(*text, start, last - start);
  }

  parts.clear();
  parts.reserve(8);
  size_t start = 0;
  for (;;) {
    const auto pos = replaced.find(sep, start);
    if (pos == std::string::npos) {
      parts.push_back(Value::string(replaced.substr(start)));
      break;
    }
    parts.push_back(Value::string(replaced.substr(start, pos - start)));
    start = pos + sep.size();
  }
  return true;
}

inline bool string_strip_replace_split(
    const Value& source,
    const std::string& old_text,
    const std::string& new_text,
    const std::string& sep,
    std::vector<Value>& parts,
    std::string& error) {
  return xlang_vm_string_strip_replace_split(source, old_text, new_text, sep, parts, error);
}

inline bool fast_string_method(
    const Value& self,
    const std::string& name,
    const Value* regs,
    const std::vector<uint32_t>& arg_regs,
    Value& out,
    bool& handled,
    std::string& error) {
  return xlang_vm_fast_string_method(self, name, regs, arg_regs, out, handled, error);
}

} // namespace xlang3
