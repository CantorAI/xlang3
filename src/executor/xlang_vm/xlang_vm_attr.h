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

#include "xlang_frame.h"

#include <string>

namespace xlang3 {

XLANG3_NOINLINE bool xlang_vm_load_attr_cached(
    const Value& object,
    const std::string& name,
    AttrSiteCache& cache,
    Value& out,
    std::string& error);

XLANG3_NOINLINE bool xlang_vm_store_attr_cached(
    Value& object,
    const std::string& name,
    const Value& value,
    AttrSiteCache& cache,
    std::string& error);

inline bool load_attr_cached(
    const Value& object,
    const std::string& name,
    AttrSiteCache& cache,
    Value& out,
    std::string& error) {
  return xlang_vm_load_attr_cached(object, name, cache, out, error);
}

inline bool store_attr_cached(
    Value& object,
    const std::string& name,
    const Value& value,
    AttrSiteCache& cache,
    std::string& error) {
  return xlang_vm_store_attr_cached(object, name, value, cache, error);
}

} // namespace xlang3
