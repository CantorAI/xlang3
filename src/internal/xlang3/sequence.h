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

#include <string>
#include <vector>

namespace xlang3 {

struct ListObject {
  Object header;
  std::vector<Value> items;
};

struct RangeObject {
  Object header;
  int64_t start = 0;
  int64_t stop = 0;
  int64_t step = 1;
};

struct RangeIteratorObject {
  Object header;
  int64_t current = 0;
  int64_t stop = 0;
  int64_t step = 1;
};

struct SequenceIteratorObject {
  Object header;
  Value source;
  uint64_t index = 0;
};

ListObject* value_as_list(const Value& value);
RangeObject* value_as_range(const Value& value);
RangeIteratorObject* value_as_range_iterator(const Value& value);
SequenceIteratorObject* value_as_sequence_iterator(const Value& value);

void sequence_release_object(Object* object);
std::string sequence_to_string(const Value& value);
bool sequence_truthy(const Value& value);

bool sequence_get_iter(const Value& iterable, Value& out, std::string& error);
bool sequence_iter_next(Value& iterator, bool& done, Value& out, std::string& error);
bool sequence_list_append(Value& list, const Value& item, std::string& error);
bool sequence_get_item(const Value& object, const Value& index, Value& out, std::string& error);
bool sequence_len(const Value& value, Value& out, std::string& error);

} // namespace xlang3
