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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace xlang3 {

namespace {

bool int_arg(const Value& value, int64_t& out) {
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  return false;
}

bool collect_iterable(const Value& iterable, std::vector<Value>& values, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(iterable, iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    values.push_back(std::move(item));
  }
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
  int64_t start = 0;
  int64_t stop = 0;
  int64_t step = 1;
  if (argc == 2) {
    if (!int_arg(args[1], stop)) {
      error = "islice stop must be int";
      return false;
    }
  } else {
    if (!int_arg(args[1], start) || !int_arg(args[2], stop)) {
      error = "islice start/stop must be int";
      return false;
    }
    if (argc == 4 && (!int_arg(args[3], step) || step < 1)) {
      error = "islice step must be a positive int";
      return false;
    }
  }
  if (start < 0 || stop < 0) {
    error = "islice indices must be non-negative";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    return false;
  }
  std::vector<Value> values;
  for (int64_t index = 0; index < stop;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    if (index >= start && ((index - start) % step) == 0) {
      values.push_back(std::move(item));
    }
    ++index;
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_takewhile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.takewhile() expected predicate and iterable";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    return false;
  }
  std::vector<Value> values;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    Value predicate_result;
    if (!runtime_call_callable(runtime, args[0], &item, 1, predicate_result, error)) {
      return false;
    }
    if (!value_truthy(predicate_result)) {
      break;
    }
    values.push_back(std::move(item));
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_dropwhile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.dropwhile() expected predicate and iterable";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    return false;
  }
  std::vector<Value> values;
  bool dropping = true;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    if (dropping) {
      Value predicate_result;
      if (!runtime_call_callable(runtime, args[0], &item, 1, predicate_result, error)) {
        return false;
      }
      dropping = value_truthy(predicate_result);
      if (dropping) {
        continue;
      }
    }
    values.push_back(std::move(item));
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_filterfalse(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.filterfalse() expected predicate and iterable";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    return false;
  }
  std::vector<Value> values;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    bool keep = !value_truthy(item);
    if (args[0].tag != ValueTag::None) {
      Value predicate_result;
      if (!runtime_call_callable(runtime, args[0], &item, 1, predicate_result, error)) {
        return false;
      }
      keep = !value_truthy(predicate_result);
    }
    if (keep) {
      values.push_back(std::move(item));
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_compress(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.compress() expected data and selectors";
    return false;
  }
  std::vector<Value> data;
  std::vector<Value> selectors;
  if (!collect_iterable(args[0], data, error) || !collect_iterable(args[1], selectors, error)) {
    return false;
  }
  std::vector<Value> values;
  const size_t count = data.size() < selectors.size() ? data.size() : selectors.size();
  values.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (value_truthy(selectors[i])) {
      values.push_back(data[i]);
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_repeat(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "itertools.repeat() expected object and optional times";
    return false;
  }
  if (argc == 1) {
    error = "infinite itertools.repeat() is not materialized by this foundation";
    return false;
  }
  int64_t times = 0;
  if (!int_arg(args[1], times) || times < 0) {
    error = "repeat times must be non-negative int";
    return false;
  }
  std::vector<Value> values;
  values.reserve(static_cast<size_t>(times));
  for (int64_t i = 0; i < times; ++i) {
    values.push_back(args[0]);
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_chain(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  std::vector<Value> values;
  for (uint32_t i = 0; i < argc; ++i) {
    if (!collect_iterable(args[i], values, error)) {
      return false;
    }
  }
  out = Value::list(std::move(values));
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

void product_visit(
    const std::vector<std::vector<Value>>& pools,
    size_t depth,
    std::vector<Value>& current,
    std::vector<Value>& out) {
  if (depth == pools.size()) {
    out.push_back(Value::tuple(current));
    return;
  }
  for (const auto& item : pools[depth]) {
    current.push_back(item);
    product_visit(pools, depth + 1, current, out);
    current.pop_back();
  }
}

bool itertools_product(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    out = Value::list({Value::tuple({})});
    return true;
  }
  std::vector<std::vector<Value>> pools;
  pools.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    pools.emplace_back();
    if (!collect_iterable(args[i], pools.back(), error)) {
      return false;
    }
  }
  std::vector<Value> values;
  std::vector<Value> current;
  product_visit(pools, 0, current, values);
  out = Value::list(std::move(values));
  return true;
}

void combinations_visit(
    const std::vector<Value>& pool,
    size_t start,
    size_t remaining,
    std::vector<Value>& current,
    std::vector<Value>& out) {
  if (remaining == 0) {
    out.push_back(Value::tuple(current));
    return;
  }
  for (size_t i = start; i + remaining <= pool.size(); ++i) {
    current.push_back(pool[i]);
    combinations_visit(pool, i + 1, remaining - 1, current, out);
    current.pop_back();
  }
}

bool itertools_combinations(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.combinations() expected iterable and r";
    return false;
  }
  int64_t r = 0;
  if (!int_arg(args[1], r) || r < 0) {
    error = "combinations r must be non-negative int";
    return false;
  }
  std::vector<Value> pool;
  if (!collect_iterable(args[0], pool, error)) {
    return false;
  }
  std::vector<Value> values;
  std::vector<Value> current;
  if (static_cast<size_t>(r) <= pool.size()) {
    combinations_visit(pool, 0, static_cast<size_t>(r), current, values);
  }
  out = Value::list(std::move(values));
  return true;
}

void combinations_with_replacement_visit(
    const std::vector<Value>& pool,
    size_t start,
    size_t remaining,
    std::vector<Value>& current,
    std::vector<Value>& out) {
  if (remaining == 0) {
    out.push_back(Value::tuple(current));
    return;
  }
  for (size_t i = start; i < pool.size(); ++i) {
    current.push_back(pool[i]);
    combinations_with_replacement_visit(pool, i, remaining - 1, current, out);
    current.pop_back();
  }
}

bool itertools_combinations_with_replacement(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.combinations_with_replacement() expected iterable and r";
    return false;
  }
  int64_t r = 0;
  if (!int_arg(args[1], r) || r < 0) {
    error = "combinations_with_replacement r must be non-negative int";
    return false;
  }
  std::vector<Value> pool;
  if (!collect_iterable(args[0], pool, error)) {
    return false;
  }
  std::vector<Value> values;
  std::vector<Value> current;
  if (!pool.empty() || r == 0) {
    combinations_with_replacement_visit(pool, 0, static_cast<size_t>(r), current, values);
  }
  out = Value::list(std::move(values));
  return true;
}

void permutations_visit(
    const std::vector<Value>& pool,
    size_t remaining,
    std::vector<uint8_t>& used,
    std::vector<Value>& current,
    std::vector<Value>& out) {
  if (remaining == 0) {
    out.push_back(Value::tuple(current));
    return;
  }
  for (size_t i = 0; i < pool.size(); ++i) {
    if (used[i] != 0) {
      continue;
    }
    used[i] = 1;
    current.push_back(pool[i]);
    permutations_visit(pool, remaining - 1, used, current, out);
    current.pop_back();
    used[i] = 0;
  }
}

bool itertools_permutations(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "itertools.permutations() expected iterable and optional r";
    return false;
  }
  std::vector<Value> pool;
  if (!collect_iterable(args[0], pool, error)) {
    return false;
  }
  int64_t r = static_cast<int64_t>(pool.size());
  if (argc == 2 && (!int_arg(args[1], r) || r < 0)) {
    error = "permutations r must be non-negative int";
    return false;
  }
  std::vector<Value> values;
  if (static_cast<size_t>(r) <= pool.size()) {
    std::vector<uint8_t> used(pool.size(), 0);
    std::vector<Value> current;
    permutations_visit(pool, static_cast<size_t>(r), used, current, values);
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_accumulate(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "itertools.accumulate() expected iterable and optional function";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    return false;
  }
  std::vector<Value> values;
  bool done = false;
  Value total;
  if (!sequence_iter_next(iterator, done, total, error)) {
    return false;
  }
  if (done) {
    out = Value::list({});
    return true;
  }
  values.push_back(total);
  for (;;) {
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    Value next;
    if (argc == 2) {
      Value call_args[2] = {total, item};
      if (!runtime_call_callable(runtime, args[1], call_args, 2, next, error)) {
        return false;
      }
    } else if (!value_add(total, item, next, error)) {
      return false;
    }
    value_assign_fast(total, next);
    values.push_back(total);
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_starmap(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "itertools.starmap() expected function and iterable";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    return false;
  }
  std::vector<Value> values;
  for (;;) {
    bool done = false;
    Value packed;
    if (!sequence_iter_next(iterator, done, packed, error)) {
      return false;
    }
    if (done) {
      break;
    }
    std::vector<Value> call_args;
    if (!collect_iterable(packed, call_args, error)) {
      return false;
    }
    Value mapped;
    if (!runtime_call_callable(runtime, args[0], call_args.empty() ? nullptr : call_args.data(), static_cast<uint32_t>(call_args.size()), mapped, error)) {
      return false;
    }
    values.push_back(std::move(mapped));
  }
  out = Value::list(std::move(values));
  return true;
}

bool itertools_zip_longest(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    out = Value::list({});
    return true;
  }
  std::vector<std::vector<Value>> pools;
  pools.reserve(argc);
  size_t max_size = 0;
  for (uint32_t i = 0; i < argc; ++i) {
    pools.emplace_back();
    if (!collect_iterable(args[i], pools.back(), error)) {
      return false;
    }
    max_size = std::max(max_size, pools.back().size());
  }
  std::vector<Value> values;
  values.reserve(max_size);
  for (size_t row_index = 0; row_index < max_size; ++row_index) {
    std::vector<Value> row;
    row.reserve(pools.size());
    for (const auto& pool : pools) {
      row.push_back(row_index < pool.size() ? pool[row_index] : Value::none());
    }
    values.push_back(Value::tuple(std::move(row)));
  }
  out = Value::list(std::move(values));
  return true;
}

} // namespace

void register_itertools_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "itertools");
  builder.function("count", itertools_count)
      .function("islice", itertools_islice)
      .function("takewhile", itertools_takewhile)
      .function("dropwhile", itertools_dropwhile)
      .function("filterfalse", itertools_filterfalse)
      .function("compress", itertools_compress)
      .function("repeat", itertools_repeat)
      .function("chain", itertools_chain)
      .function("batched", itertools_batched)
      .function("product", itertools_product)
      .function("combinations", itertools_combinations)
      .function("combinations_with_replacement", itertools_combinations_with_replacement)
      .function("permutations", itertools_permutations)
      .function("accumulate", itertools_accumulate)
      .function("starmap", itertools_starmap)
      .function("zip_longest", itertools_zip_longest);
  runtime.register_module("itertools", builder.finish());
}

} // namespace xlang3
