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
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/sequence.h"

#include <limits>

namespace xlang3 {

namespace {

bool int_arg(const Value& value, int64_t& out) {
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  return false;
}

bool itertools_count(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "itertools.count() expected at most 2 arguments";
    return false;
  }
  int64_t start = 0;
  int64_t step = 1;
  if (argc >= 1 && !int_arg(args[0], start)) {
    error = "itertools.count() start must be int";
    return false;
  }
  if (argc >= 2 && !int_arg(args[1], step)) {
    error = "itertools.count() step must be int";
    return false;
  }
  out = Value::range_iterator(start, std::numeric_limits<int64_t>::max(), step == 0 ? 1 : step);
  return true;
}

bool itertools_islice(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "itertools.islice() expected iterable and slice bounds";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool itertools_takewhile(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.takewhile() expected predicate and iterable";
    return false;
  }
  value_assign_fast(out, args[1]);
  return true;
}

bool itertools_batched(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "itertools.batched() expected iterable, n, and optional strict";
    return false;
  }
  int64_t count = 0;
  if (!int_arg(args[1], count) || count < 1) {
    error = "itertools.batched() n must be at least one";
    return false;
  }

  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    return false;
  }

  std::vector<Value> batches;
  bool done = false;
  while (!done) {
    std::vector<Value> batch;
    batch.reserve(static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
      Value item;
      if (!sequence_iter_next(iterator, done, item, error)) {
        return false;
      }
      if (done) {
        break;
      }
      batch.push_back(std::move(item));
    }
    if (!batch.empty()) {
      batches.push_back(Value::tuple(std::move(batch)));
    }
  }

  out = Value::list(std::move(batches));
  return true;
}

} // namespace

void register_itertools_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "itertools");
  builder.function("count", itertools_count)
      .function("islice", itertools_islice)
      .function("takewhile", itertools_takewhile)
      .function("batched", itertools_batched);
  runtime.register_module("itertools", builder.finish());
}

} // namespace xlang3
