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

#include "xlang3/value.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

struct EnumerateIteratorObject {
  Object header;
  Value iterator;
  int64_t index = 0;
};

struct ZipIteratorObject {
  Object header;
  std::vector<Value> iterators;
};

struct MapIteratorObject {
  Object header;
  Runtime* runtime = nullptr;
  Value callable;
  std::vector<Value> iterators;
};

struct FilterIteratorObject {
  Object header;
  Runtime* runtime = nullptr;
  Value predicate;
  Value iterator;
};

struct CallableIteratorObject {
  Object header;
  Runtime* runtime = nullptr;
  Value callable;
  Value sentinel;
};

struct ChainIteratorObject {
  Object header;
  Runtime* runtime = nullptr;
  std::vector<Value> iterators;
  Value outer_iterator;
  Value current_iterator;
  size_t index = 0;
  bool from_iterable = false;
};

struct ProtocolIteratorObject {
  Object header;
  Runtime* runtime = nullptr;
  Value iterator;
  bool use_getitem = false;
  uint64_t index = 0;
};

Value functional_enumerate_iterator(Value iterator, int64_t start);
Value functional_zip_iterator(std::vector<Value> iterators);
Value functional_map_iterator(Runtime* runtime, Value callable, std::vector<Value> iterators);
Value functional_filter_iterator(Runtime* runtime, Value predicate, Value iterator);
Value functional_callable_iterator(Runtime* runtime, Value callable, Value sentinel);
Value functional_chain_iterator(std::vector<Value> iterators);
Value functional_chain_from_iterable_iterator(Runtime* runtime, Value outer_iterator);
Value functional_protocol_iterator(Runtime* runtime, Value iterator);
Value functional_getitem_iterator(Runtime* runtime, Value iterable);

bool value_is_functional_iterator(const Value& value);
void functional_iterator_release_object(Object* object);
std::string functional_iterator_to_string(const Value& value);
bool functional_iterator_next(Value& iterator, bool& done, Value& out, std::string& error);

bool runtime_call_callable(
    Runtime& runtime,
    const Value& callable,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error);
bool runtime_call_callable_kw(
    Runtime& runtime,
    const Value& callable,
    const Value* args,
    uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& kwargs,
    Value& out,
    std::string& error);

bool runtime_get_iter(Runtime& runtime, const Value& iterable, Value& out, std::string& error);
bool runtime_collect_iterable(Runtime& runtime, const Value& iterable, std::vector<Value>& out, std::string& error);

} // namespace xlang3
