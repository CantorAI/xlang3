#include "xlang3/abi/xprotocol.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/object_model.h"
#include "runtime_lock.h"
#include <stdexcept>
#include <vector>

namespace {
template<class F> X3Status execute(X3Runtime* runtime, F&& fn) {
  xlang3::XlangRuntimeExecutionGuard guard;
  auto* rt = reinterpret_cast<xlang3::Runtime*>(runtime);
  if (!rt) return X3_STATUS_ERROR;
  try {
    std::string error;
    if (fn(*rt, error)) return X3_STATUS_OK;
    rt->set_last_error(error);
  } catch (const std::exception& error) { rt->set_last_error(error.what()); }
  return X3_STATUS_ERROR;
}
}

extern "C" X3Status x3_call_builtin(X3Runtime* runtime, const char* name,
    const X3Value* args, uint32_t argc, X3Value* result) {
  return execute(runtime, [&](auto& rt, auto& error) {
    if (!name || !result || (argc && !args)) throw std::invalid_argument("null builtin argument");
    auto* callable = rt.find_builtin(name);
    if (!callable) throw std::invalid_argument("unknown builtin");
    std::vector<xlang3::Value> values;
    values.reserve(argc);
    for (uint32_t i = 0; i < argc; ++i) values.push_back(xlang3::from_c_value(args[i], error));
    xlang3::Value out;
    if (!error.empty()) return false;
    const std::string_view operation(name);
    if (operation == "slice" && argc >= 1 && argc <= 3) {
      out = argc == 1 ? xlang3::Value::slice(xlang3::Value::none(), values[0], xlang3::Value::none())
          : xlang3::Value::slice(values[0], values[1], argc == 3 ? values[2] : xlang3::Value::none());
    } else if (operation == "bool" && argc <= 1) {
      bool truth = false;
      if (argc && !xlang3::runtime_truthy(rt, values[0], truth, error)) return false;
      out = xlang3::Value::boolean(truth);
    } else if (operation == "str" && argc == 1) {
      xlang3::Value method;
      std::string lookup;
      if (xlang3::value_as_instance(values[0]) && xlang3::object_get_attr(values[0], "__str__", method, lookup)) {
        if (!xlang3::runtime_call_callable(rt, method, nullptr, 0, out, error)) return false;
        if (!xlang3::value_as_string(out)) throw std::runtime_error("__str__ returned non-string");
      } else out = xlang3::Value::string(xlang3::value_to_string(values[0]));
    } else if (!xlang3::runtime_call_callable(rt, *callable, values.data(), argc, out, error)) return false;
    *result = xlang3::to_c_value(out);
    return true;
  });
}

extern "C" X3Status x3_get_iter(X3Runtime* runtime, X3Value value, X3Value* result) {
  return execute(runtime, [&](auto& rt, auto& error) {
    if (!result) throw std::invalid_argument("null iterator output");
    auto source = xlang3::from_c_value(value, error);
    xlang3::Value out;
    if (!error.empty() || !xlang3::runtime_get_iter(rt, source, out, error)) return false;
    *result = xlang3::to_c_value(out);
    return true;
  });
}

extern "C" X3Status x3_iter_next(X3Runtime* runtime, X3Value value, X3Value* result, int32_t* done) {
  return execute(runtime, [&](auto&, auto& error) {
    if (!result || !done) throw std::invalid_argument("null iterator output");
    *done = 0;
    auto iterator = xlang3::from_c_value(value, error);
    xlang3::Value out;
    bool exhausted = false;
    if (!error.empty() || !xlang3::sequence_iter_next(iterator, exhausted, out, error)) return false;
    *done = exhausted ? 1 : 0;
    *result = exhausted ? x3_value_none() : xlang3::to_c_value(out);
    return true;
  });
}

namespace {
X3Status mutate_item(X3Runtime* runtime, X3Value object, X3Value key, const X3Value* value) {
  return execute(runtime, [&](auto& rt, auto& error) {
    auto target = xlang3::from_c_value(object, error);
    auto index = xlang3::from_c_value(key, error);
    auto item = value ? xlang3::from_c_value(*value, error) : xlang3::Value::none();
    if (!error.empty()) return false;
    if (value ? xlang3::sequence_set_item(target, index, item, error)
              : xlang3::sequence_delete_item(target, index, error)) return true;
    xlang3::Value method;
    std::string lookup;
    if (xlang3::value_as_instance(target) && xlang3::object_get_attr(target,
        value ? "__setitem__" : "__delitem__", method, lookup)) {
      const xlang3::Value args[] = {index, item};
      xlang3::Value ignored;
      error.clear();
      return xlang3::runtime_call_callable(rt, method, args, value ? 2 : 1, ignored, error);
    }
    if (error == "Existing exports of data: object cannot be re-sized") rt.raise_class_error("BufferError", error);
    else if (error == "key not found") rt.raise_class_error("KeyError", error);
    else if (error == "index out of range") rt.raise_class_error("IndexError", error);
    return false;
  });
}
}
extern "C" X3Status x3_set_item(X3Runtime* rt, X3Value obj, X3Value key, X3Value value) {
  return mutate_item(rt, obj, key, &value);
}
extern "C" X3Status x3_delete_item(X3Runtime* rt, X3Value obj, X3Value key) {
  return mutate_item(rt, obj, key, nullptr);
}
extern "C" X3Status x3_delete_attr(X3Runtime* runtime, X3Value object, const char* name) {
  return execute(runtime, [&](auto& rt, auto& error) {
    if (!name) throw std::invalid_argument("null attribute name");
    auto target = xlang3::from_c_value(object, error);
    if (!error.empty()) return false;
    if (auto* instance = xlang3::value_as_instance(target)) {
      auto* klass = xlang3::value_as_class(instance->klass);
      xlang3::Value hook;
      if (klass && klass->has_delattr_hook &&
          xlang3::object_get_class_attr_for_instance(target, "__delattr__", hook, error)) {
        const xlang3::Value args[] = {target, xlang3::Value::string(name)};
        xlang3::Value ignored;
        return xlang3::runtime_call_callable(rt, hook, args, 2, ignored, error);
      }
    }
    return xlang3::object_delete_attr(target, name, error);
  });
}
extern "C" const char* x3_runtime_take_exception_type(X3Runtime* runtime) {
  xlang3::XlangRuntimeExecutionGuard guard;
  thread_local std::string name;
  name.clear();
  auto* rt = reinterpret_cast<xlang3::Runtime*>(runtime);
  xlang3::Value exception;
  if (rt && rt->take_pending_exception(exception)) {
    if (auto* klass = xlang3::value_as_class(rt->exception_type(exception))) name = klass->name;
  }
  return name.empty() ? nullptr : name.c_str();
}
