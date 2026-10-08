// Prepared replacement; do not apply while the current benchmark is live.
bool minmax_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    bool want_min,
    const Value* key_callable = nullptr,
    const Value* default_value = nullptr) {
  if (argc == 0) {
    return raise_type_error(runtime, want_min ? "min() expected at least 1 argument" : "max() expected at least 1 argument", error);
  }
  if (default_value != nullptr && argc != 1) {
    return raise_type_error(runtime, want_min ? "min() default only valid with a single iterable" : "max() default only valid with a single iterable", error);
  }
  auto preserve_iteration_error = [&]() {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
    } else {
      runtime.raise_class_error("TypeError", error);
    }
    return false;
  };
  Value iterator;
  if (argc == 1 && !runtime_get_iter(runtime, args[0], iterator, error)) {
    return preserve_iteration_error();
  }
  // Stream items through their key and comparison before advancing again.
  // This retains O(1) temporary owners instead of two O(n) owning vectors,
  // and preserves callback mutation, early errors and first-item ties.
  // In particular, a key callback may grow the list being iterated.
  Value best_item;
  Value best_key;
  bool have_best = false;
  uint32_t positional_index = 0;
  const bool use_key = key_callable != nullptr && key_callable->tag != ValueTag::None;
  for (;;) {
    Value item;
    Value key;
    if (argc == 1) {
      bool done = false;
      if (!sequence_iter_next(iterator, done, item, error)) {
        return preserve_iteration_error();
      }
      if (done) break;
    } else {
      if (positional_index == argc) break;
      value_assign_fast(item, args[positional_index++]);
    }
    if (use_key) {
      if (!runtime_call_callable(runtime, *key_callable, &item, 1, key, error)) {
        return false;
      }
    } else {
      value_assign_fast(key, item);
    }
    if (!have_best) {
      best_item = std::move(item);
      best_key = std::move(key);
      have_best = true;
      continue;
    }
    bool better = false;
    {
      Value comparison;
      if (!runtime_value_compare(runtime, want_min ? "<" : ">", key, best_key,
                                 comparison, error)) {
        Value pending;
        if (runtime.take_pending_exception(pending))
          runtime.set_pending_exception(std::move(pending));
        else
          runtime.raise_class_error("TypeError", error);
        return false;
      }
      if (!runtime_truthy(runtime, comparison, better, error)) return false;
    }
    // Match native min/max release order before the next iterator step:
    // replaced winner key then item; discarded candidate item then key.
    // Incoming item/key remain owned throughout reentrant finalizers.
    if (better) {
      value_set_invalid(best_key);
      value_set_invalid(best_item);
      best_key = std::move(key);
      best_item = std::move(item);
    } else {
      value_set_invalid(item);
      value_set_invalid(key);
    }
  }
  Value winner;
  if (have_best) {
    winner = std::move(best_item);
    value_set_invalid(best_key);
  } else if (default_value != nullptr) {
    // Do not call or validate an unused key on an empty iterable.
    value_assign_fast(winner, *default_value);
  } else {
    runtime.raise_class_error("ValueError", want_min ? "min() arg is an empty sequence" : "max() arg is an empty sequence");
    error = want_min ? "min() arg is an empty sequence" : "max() arg is an empty sequence";
    return false;
  }
  value_set_invalid(iterator);
  value_assign_fast(out, winner);
  return true;
}
