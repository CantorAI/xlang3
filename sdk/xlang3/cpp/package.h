/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/cpp/value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>
#include <mutex>
#include <limits>
#include <cstring>
#include <cstddef>

namespace X {

template <typename T>
class Package;

namespace detail {

template <typename T>
struct always_false : std::false_type {};

class NativeError : public std::runtime_error {
public:
  explicit NativeError(const std::string& message) : std::runtime_error(message) {}
};

struct InstanceEvents {
  std::recursive_mutex mutex;
  std::unordered_map<std::string, Value> values;
};
struct EventStorage {
  mutable std::shared_ptr<InstanceEvents> state;
  EventStorage() = default;
  EventStorage(const EventStorage&) {}
  EventStorage& operator=(const EventStorage&) { reset(); return *this; }
  void reset() { std::atomic_store(&state, std::shared_ptr<InstanceEvents>()); }
};
template<class T>
auto notify_event_created(T& object, const char* name, Value& event, int)
    -> decltype(object.OnEventCreated(name, event), void()) { object.OnEventCreated(name, event); }
template<class T> void notify_event_created(T&, const char*, Value&, long) {}

template<class T>
Value instance_event(T& object, const char* name, const EventStorage& storage, X3PackageHost* host) {
  if (!host || !name || !*name) return {};
  auto state = std::atomic_load(&storage.state);
  if (!state) {
    auto fresh = std::make_shared<InstanceEvents>();
    if (std::atomic_compare_exchange_strong(&storage.state, &state, fresh)) state = std::move(fresh);
  }
  std::lock_guard<std::recursive_mutex> lock(state->mutex);
  auto found = state->values.find(name);
  if (found != state->values.end()) return found->second;
  X3Value raw = x3_value_invalid();
  if (host->event_create(host->runtime, name, &raw) != X3_STATUS_OK) throw NativeError("cannot create native instance event");
  Value event(host, raw, false);
  notify_event_created(object, name, event, 0);
  state->values.emplace(name, event);
  return event;
}

inline X3Status set_native_error(X3PackageHost* host, X3CallContext* context, const char* message) {
  if (host != nullptr && host->set_error != nullptr) {
    host->set_error(context, message == nullptr ? "native package error" : message);
  }
  return X3_STATUS_ERROR;
}

template <typename T, typename R, typename... Args>
struct MemberBinding {
  X3PackageHost* host = nullptr;
  T* object = nullptr;
  R(T::*method)(Args...) = nullptr;
};

template <typename T, typename R, typename... Args>
struct ConstMemberBinding {
  X3PackageHost* host = nullptr;
  T* object = nullptr;
  R(T::*method)(Args...) const = nullptr;
};

template <typename T, typename R, typename... Args>
struct InstanceMemberBinding {
  X3PackageHost* host = nullptr;
  std::string name;
  R(T::*method)(Args...) = nullptr;
};

template <typename T, typename R, typename... Args>
struct ConstInstanceMemberBinding {
  X3PackageHost* host = nullptr;
  std::string name;
  R(T::*method)(Args...) const = nullptr;
};

template <typename T>
const char* native_type_name() {
  return typeid(T).name();
}

template <typename T>
decltype(auto) arg_from_value(Value& value) {
  using U = std::decay_t<T>;
  if constexpr (std::is_same_v<U, Value>) {
    return (value);
  } else if constexpr (std::is_same_v<U, std::string>) {
    return value.ToString(false);
  } else if constexpr (std::is_integral_v<U> && !std::is_same_v<U, bool>) {
    return static_cast<U>(value.ToLongLong());
  } else if constexpr (std::is_floating_point_v<U>) {
    return static_cast<U>(value.ToDouble());
  } else if constexpr (std::is_same_v<U, bool>) {
    return value.ToLongLong() != 0;
  } else {
    static_assert(always_false<U>::value, "Unsupported xlang3 package argument type");
  }
}

template <typename T>
Value value_from_field(X3PackageHost* host, const T& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    return Value::String(host, value);
  } else if constexpr (std::is_same_v<T, bool>) {
    return Value(value);
  } else if constexpr (std::is_integral_v<T>) {
    return Value(static_cast<long long>(value));
  } else if constexpr (std::is_floating_point_v<T>) {
    return Value(static_cast<double>(value));
  } else {
    static_assert(always_false<T>::value, "Unsupported xlang3 package field property type");
  }
}

template <typename T>
void field_from_value(T& out, Value& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    out = value.ToString(false);
  } else if constexpr (std::is_same_v<T, bool>) {
    out = value.ToLongLong() != 0;
  } else if constexpr (std::is_integral_v<T>) {
    out = static_cast<T>(value.ToLongLong());
  } else if constexpr (std::is_floating_point_v<T>) {
    out = static_cast<T>(value.ToDouble());
  } else {
    static_assert(always_false<T>::value, "Unsupported xlang3 package field property type");
  }
}

template <typename T>
auto sync_package_fields(T* object, int) -> decltype(object->__xlang3_package_->SyncFieldsFromModule(), void()) {
  if (object->__xlang3_package_ != nullptr) {
    object->__xlang3_package_->SyncFieldsFromModule();
  }
}

template <typename T>
void sync_package_fields(T*, long) {}

inline X3Value release_to_raw(Value&& value) {
  return value.Detach();
}

inline X3Value return_to_raw(X3PackageHost*, Value&& value) {
  return release_to_raw(std::move(value));
}

inline X3Value return_to_raw(X3PackageHost* host, const Value& value) {
  if (value.raw().tag == X3_TAG_OBJECT && host != nullptr && host->value_retain != nullptr) {
    host->value_retain(value.raw());
  }
  return value.raw();
}

inline X3Value return_to_raw(X3PackageHost*, std::nullptr_t) {
  return x3_value_none();
}

inline X3Value return_to_raw(X3PackageHost* host, const std::string& value) {
  return host->value_string(host->runtime, value.c_str());
}

inline X3Value return_to_raw(X3PackageHost* host, const char* value) {
  return host->value_string(host->runtime, value == nullptr ? "" : value);
}

inline X3Value return_to_raw(X3PackageHost*, bool value) {
  return x3_value_bool(value ? 1 : 0);
}

template <typename T>
X3Value return_to_raw(X3PackageHost*, T value) {
  if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    return x3_value_int64(static_cast<int64_t>(value));
  } else if constexpr (std::is_floating_point_v<T>) {
    return x3_value_double(static_cast<double>(value));
  } else {
    static_assert(always_false<T>::value, "Unsupported xlang3 package return type");
  }
}

template <typename T, typename R, typename... Args, size_t... I>
X3Status invoke_instance_member(
    X3CallContext* context,
    X3PackageHost* host,
    R(T::*method)(Args...),
    const X3Value* args,
    uint32_t argc,
    X3Value* result,
    std::index_sequence<I...>) {
  if (host == nullptr || result == nullptr || argc < sizeof...(Args) + 1) {
    if (host != nullptr && host->set_error != nullptr) host->set_error(context, "bad native method call");
    return X3_STATUS_ERROR;
  }
  auto* self = static_cast<T*>(host->instance_get_native_data(args[0], native_type_name<T>()));
  if (self == nullptr) {
    host->set_error(context, "native instance data is missing");
    return X3_STATUS_ERROR;
  }
  auto values = std::make_tuple(Value(host, args[I + 1], true)...);
  try {
    if constexpr (std::is_void_v<R>) {
      (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
      *result = x3_value_none();
    } else {
      *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
    }
  } catch (const std::exception& ex) {
    return set_native_error(host, context, ex.what());
  }
  return X3_STATUS_OK;
}

template <typename T, typename R, typename... Args>
X3Status instance_member_thunk(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* binding = static_cast<InstanceMemberBinding<T, R, Args...>*>(user_data);
  return invoke_instance_member(
      context,
      binding->host,
      binding->method,
      args,
      argc,
      result,
      std::index_sequence_for<Args...>{});
}

template <typename T, typename R, typename... Args, size_t... I>
X3Status invoke_const_instance_member(
    X3CallContext* context,
    X3PackageHost* host,
    R(T::*method)(Args...) const,
    const X3Value* args,
    uint32_t argc,
    X3Value* result,
    std::index_sequence<I...>) {
  if (host == nullptr || result == nullptr || argc < sizeof...(Args) + 1) {
    if (host != nullptr && host->set_error != nullptr) host->set_error(context, "bad native method call");
    return X3_STATUS_ERROR;
  }
  auto* self = static_cast<T*>(host->instance_get_native_data(args[0], native_type_name<T>()));
  if (self == nullptr) {
    host->set_error(context, "native instance data is missing");
    return X3_STATUS_ERROR;
  }
  auto values = std::make_tuple(Value(host, args[I + 1], true)...);
  try {
    if constexpr (std::is_void_v<R>) {
      (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
      *result = x3_value_none();
    } else {
      *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
    }
  } catch (const std::exception& ex) {
    return set_native_error(host, context, ex.what());
  }
  return X3_STATUS_OK;
}

template <typename T, typename R, typename... Args>
X3Status const_instance_member_thunk(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* binding = static_cast<ConstInstanceMemberBinding<T, R, Args...>*>(user_data);
  return invoke_const_instance_member(
      context,
      binding->host,
      binding->method,
      args,
      argc,
      result,
      std::index_sequence_for<Args...>{});
}

template <typename T, typename R, typename... Args, size_t... I>
X3Status invoke_member(
    X3CallContext* context,
    T* self,
    X3PackageHost* host,
    R(T::*method)(Args...),
    const X3Value* args,
    uint32_t argc,
    X3Value* result,
    std::index_sequence<I...>) {
  if (self == nullptr || host == nullptr || result == nullptr || argc < sizeof...(Args)) {
    if (host != nullptr && host->set_error != nullptr) host->set_error(context, "bad native function call");
    return X3_STATUS_ERROR;
  }
  auto values = std::make_tuple(Value(host, args[I], true)...);
  sync_package_fields(self, 0);
  try {
    if constexpr (std::is_void_v<R>) {
      (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
      *result = x3_value_none();
    } else {
      *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
    }
  } catch (const std::exception& ex) {
    return set_native_error(host, context, ex.what());
  }
  return X3_STATUS_OK;
}

template <typename T, typename R, typename... Args>
X3Status member_thunk(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* binding = static_cast<MemberBinding<T, R, Args...>*>(user_data);
  return invoke_member(
      context,
      binding->object,
      binding->host,
      binding->method,
      args,
      argc,
      result,
      std::index_sequence_for<Args...>{});
}

template <typename T, typename R, typename... Args, size_t... I>
X3Status invoke_const_member(
    X3CallContext* context,
    const T* self,
    X3PackageHost* host,
    R(T::*method)(Args...) const,
    const X3Value* args,
    uint32_t argc,
    X3Value* result,
    std::index_sequence<I...>) {
  if (self == nullptr || host == nullptr || result == nullptr || argc < sizeof...(Args)) {
    if (host != nullptr && host->set_error != nullptr) host->set_error(context, "bad native function call");
    return X3_STATUS_ERROR;
  }
  auto values = std::make_tuple(Value(host, args[I], true)...);
  sync_package_fields(self, 0);
  try {
    if constexpr (std::is_void_v<R>) {
      (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
      *result = x3_value_none();
    } else {
      *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
    }
  } catch (const std::exception& ex) {
    return set_native_error(host, context, ex.what());
  }
  return X3_STATUS_OK;
}

template <typename T, typename R, typename... Args>
X3Status const_member_thunk(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* binding = static_cast<ConstMemberBinding<T, R, Args...>*>(user_data);
  return invoke_const_member(
      context,
      binding->object,
      binding->host,
      binding->method,
      args,
      argc,
      result,
      std::index_sequence_for<Args...>{});
}

template <typename T>
struct BindingCleanupFor {
  static void cleanup(void* data) {
    delete static_cast<T*>(data);
  }
};

template <typename T>
void cleanup_native_instance(void* data) {
  delete static_cast<T*>(data);
}

template <typename T>
void noop_native_instance_cleanup(void*) {}

template<class T>
X3Status attach_owned_instance(X3PackageHost* host, X3Value instance, std::unique_ptr<T> object) {
  T* native = object.get();
  if constexpr (std::is_base_of_v<std::enable_shared_from_this<T>, T>) {
    auto owner = std::make_unique<std::shared_ptr<T>>(std::move(object));
    if (!host->instance_set_native_owner || host->instance_set_native_owner(instance,
        native_type_name<T>(), owner->get(), owner.get(),
        [](void* data) { delete static_cast<std::shared_ptr<T>*>(data); }) != X3_STATUS_OK) return X3_STATUS_ERROR;
    owner.release();
  } else {
    if (host->instance_set_native_data(instance, native_type_name<T>(), object.get(), cleanup_native_instance<T>) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    object.release();
  }
  return T::APISET().BindNativeInstance(host, instance, native);
}

template <typename T>
auto call_package_created(T* object, Package<T>* package, int) -> decltype(object->OnPackageCreated(package), void()) {
  object->OnPackageCreated(package);
}

template <typename T>
void call_package_created(T*, Package<T>*, long) {}

template<class T> struct ConstructorBinding {
  X3PackageHost* host;
  T* borrowed;
};

struct ConstructorArgument {
  Value value;
  template<class U, std::enable_if_t<std::is_arithmetic_v<U> ||
      std::is_same_v<U, std::string> || std::is_same_v<U, Value>, int> = 0>
  operator U() const {
    if constexpr (std::is_same_v<U, Value>) return value;
    else if constexpr (std::is_same_v<U, std::string>) {
      if (!value.IsString()) throw NativeError("constructor requires a string");
      return value.ToString();
    } else if constexpr (std::is_same_v<U, bool>) {
      if (value.raw().tag != X3_TAG_BOOL) throw NativeError("constructor requires a boolean");
      return value.ToLongLong() != 0;
    } else if constexpr (std::is_integral_v<U>) {
      if (!value.IsInt64()) throw NativeError("constructor requires an integer");
      const auto n = value.ToLongLong();
      if constexpr (std::is_unsigned_v<U>) {
        if (n < 0 || static_cast<uint64_t>(n) > (std::numeric_limits<U>::max)())
          throw NativeError("constructor integer out of range");
      } else if (n < (std::numeric_limits<U>::min)() || n > (std::numeric_limits<U>::max)())
        throw NativeError("constructor integer out of range");
      return static_cast<U>(n);
    } else {
      if (!value.IsDouble() && !value.IsInt64()) throw NativeError("constructor requires a number");
      return static_cast<U>(value.ToDouble());
    }
  }
};

template<class T, size_t... I>
T* construct_native(X3PackageHost* host, const X3Value* args, std::index_sequence<I...>) {
  return new T(ConstructorArgument{Value(host, args[I + 1], true)}...);
}

template <typename T, uint32_t Argc>
X3Status constructor_thunk(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* binding = static_cast<ConstructorBinding<T>*>(user_data);
  auto* host = binding->host;
  try {
  if (host == nullptr || result == nullptr || argc < 1) {
    return X3_STATUS_ERROR;
  }
  if (argc - 1 < Argc) {
    host->set_error(context, "native constructor received too few arguments");
    return X3_STATUS_ERROR;
  }
  T* object = binding->borrowed;
  if (object) {
    if (object->__xlang3_host_ && object->__xlang3_host_->runtime != host->runtime) {
      host->set_error(context, "native singleton is already bound to another runtime");
      return X3_STATUS_ERROR;
    }
  } else if constexpr (Argc == 0) {
    object = new T();
  } else if constexpr (Argc == 1 && std::is_constructible_v<T, std::string>) {
    Value arg0(host, args[1], true);
    object = new T(arg0.ToString(false));
  } else if constexpr (Argc == 1 && std::is_constructible_v<T, Value>) {
    object = new T(Value(host, args[1], true));
  } else if constexpr (Argc == 1 && std::is_constructible_v<T, long long>) {
    Value argument(host, args[1], true);
    if (!argument.IsInt64()) return set_native_error(host, context, "constructor requires an integer");
    object = construct_native<T>(host, args, std::make_index_sequence<1>{});
  } else if constexpr (Argc > 1) {
    object = construct_native<T>(host, args, std::make_index_sequence<Argc>{});
  } else {
    host->set_error(context, "native constructor signature is not supported by xlang3 C++ binding");
    return X3_STATUS_ERROR;
  }
  object->__xlang3_host_ = host;
  const auto attach_status = binding->borrowed
      ? host->instance_set_native_data(args[0], native_type_name<T>(), object, noop_native_instance_cleanup<T>)
      : attach_owned_instance(host, args[0], std::unique_ptr<T>(object));
  if (attach_status != X3_STATUS_OK) {
    host->set_error(context, "failed to attach native instance data");
    return X3_STATUS_ERROR;
  }
  if (binding->borrowed && T::APISET().BindNativeInstance(host, args[0], object) != X3_STATUS_OK)
    return set_native_error(host, context, "failed to bind native base classes");
  *result = x3_value_none();
  return X3_STATUS_OK;
  } catch (const std::exception& error) {
    return set_native_error(host, context, error.what());
  } catch (...) {
    return set_native_error(host, context, "native constructor failed");
  }
}

} // namespace detail

using Error = detail::NativeError;
using ARGS = std::vector<Value>;
using KWARGS = std::vector<std::pair<std::string, Value>>;

namespace detail {
template<class T> struct VariableBinding {
  X3PackageHost* host;
  T* object;
  Value(T::*method)(const ARGS&, const KWARGS&);
  static X3Status Invoke(X3CallContext* context, X3Runtime*, void* data,
      const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
    auto& binding = *static_cast<VariableBinding*>(data);
    try {
      auto* self = binding.object;
      uint32_t first = 0;
      if (!self) {
        if (!argc) throw Error("native method requires an instance");
        self = static_cast<T*>(binding.host->instance_get_native_data(args[0], native_type_name<T>()));
        first = 1;
      }
      if (!self) throw Error("native instance data is missing");
      ARGS positional;
      KWARGS keywords;
      positional.reserve(argc - first);
      keywords.reserve(kwargc);
      for (uint32_t i = first; i < argc; ++i) positional.emplace_back(binding.host, args[i], true);
      for (uint32_t i = 0; i < kwargc; ++i) keywords.emplace_back(kwargs[i].name, Value(binding.host, kwargs[i].value, true));
      *result = return_to_raw(binding.host, (self->*binding.method)(positional, keywords));
      return X3_STATUS_OK;
    } catch (const std::exception& e) { return set_native_error(binding.host, context, e.what()); }
  }
};
}

template <typename T>
class Package {
public:
  Package(X3PackageHost* host, const char* module_name, T* object, bool owns_object = true)
      : host_(host), object_(object), owns_object_(owns_object) {
    if (host_ != nullptr && host_->add_module != nullptr) {
      host_->add_module(host_, module_name, &module_);
    }
  }

  ~Package() {
    for (auto& binding : bindings_) {
      if (binding.cleanup != nullptr) {
        binding.cleanup(binding.data);
      }
    }
    if (object_) {
      object_->__xlang3_package_ = nullptr;
      object_->__xlang3_host_ = nullptr;
      if (owns_object_) delete object_;
    }
  }

  Package(const Package&) = delete;
  Package& operator=(const Package&) = delete;

  X3PackageHost* host() const { return host_; }
  X3Module* module() const { return module_; }
  T* object() const { return object_; }
  T* operator->() const { return object_; }

  Value CurrentModule() const { return cur_module_; }
  void SetCurrentModule(Value module) { cur_module_ = std::move(module); }
  Value GetValue(const char* name) const {
    auto it = values_.find(name == nullptr ? std::string() : std::string(name));
    return it == values_.end() ? Value() : it->second;
  }
  Value GetLiveValue(const char* name) const {
    if (module_ == nullptr || host_ == nullptr || host_->module_get_attr == nullptr || name == nullptr) {
      return GetValue(name);
    }
    X3Value raw = x3_value_invalid();
    if (host_->module_get_attr(module_, name, &raw) != X3_STATUS_OK) {
      return GetValue(name);
    }
    return Value(host_, raw, false);
  }
  bool SetLiveValue(const char* name, const Value& value) {
    if (module_ == nullptr || host_ == nullptr || host_->module_set_attr == nullptr || name == nullptr) {
      return false;
    }
    values_[name] = value;
    return host_->module_set_attr(module_, name, value.raw()) == X3_STATUS_OK;
  }
  void SyncFieldsFromModule() {
    for (auto& field : field_bindings_) {
      field.sync(*this);
    }
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...), uint32_t flags = 0) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    return AddFunc(name, method, flags);
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const, uint32_t flags = 0) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    return AddFunc(name, method, flags);
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...), uint32_t flags = 0) {
    if (module_ == nullptr || name == nullptr) return false;
    auto* binding = new detail::MemberBinding<T, R, Args...>{host_, object_, method};
    bindings_.push_back({binding, detail::BindingCleanupFor<detail::MemberBinding<T, R, Args...>>::cleanup});
    X3NativeFunctionDef def{};
    def.size = sizeof(def);
    def.name = name;
    def.callback = &detail::member_thunk<T, R, Args...>;
    def.flags = flags;
    def.user_data = binding;
    def.min_argc = static_cast<uint32_t>(sizeof...(Args));
    def.max_argc = static_cast<uint32_t>(sizeof...(Args));
    return host_->module_add_function(module_, &def) == X3_STATUS_OK;
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const, uint32_t flags = 0) {
    if (module_ == nullptr || name == nullptr) return false;
    auto* binding = new detail::ConstMemberBinding<T, R, Args...>{host_, object_, method};
    bindings_.push_back({binding, detail::BindingCleanupFor<detail::ConstMemberBinding<T, R, Args...>>::cleanup});
    X3NativeFunctionDef def{};
    def.size = sizeof(def);
    def.name = name;
    def.callback = &detail::const_member_thunk<T, R, Args...>;
    def.flags = flags;
    def.user_data = binding;
    def.min_argc = static_cast<uint32_t>(sizeof...(Args));
    def.max_argc = static_cast<uint32_t>(sizeof...(Args));
    return host_->module_add_function(module_, &def) == X3_STATUS_OK;
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFuncEx(const char* name, R(T::*method)(Args...)) {
    return AddFunc<Argc>(name, method);
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddRTFunc(const char* name, R(T::*method)(Args...)) {
    return AddFunc<Argc>(name, method);
  }

  template <typename R, typename... Args>
  bool AddVarFunc(const char* name, R(T::*method)(Args...)) {
    return AddFunc(name, method);
  }

  bool AddVarFunc(const char* name, Value(T::*method)(const ARGS&, const KWARGS&)) {
    using Binding = detail::VariableBinding<T>;
    auto* binding = new Binding{host_, object_, method};
    bindings_.push_back({binding, &detail::BindingCleanupFor<Binding>::cleanup});
    X3NativeFunctionDef def{};
    def.size = sizeof(def); def.name = name; def.user_data = binding;
    def.keyword_callback = &Binding::Invoke;
    return host_->module_add_function(module_, &def) == X3_STATUS_OK;
  }

  template <typename R, typename... Args>
  bool AddVarFuncEx(const char* name, R(T::*method)(Args...)) {
    return AddFunc(name, method);
  }

  template <typename R>
  bool AddProp(const char* name, R(T::*getter)()) {
    return AddFunc<0>(name, getter);
  }

  template <typename R>
  bool AddProp(const char* name, R(T::*getter)() const) {
    return AddFunc(name, getter);
  }

  template <typename FieldT>
  bool AddProp0(const char* name, FieldT T::*field) {
    return AddPropWithType(name, field);
  }

  template <typename FieldT>
  bool AddPropWithType(const char* name, FieldT T::*field) {
    if (name == nullptr || field == nullptr || object_ == nullptr) {
      return false;
    }
    const std::string registered_name(name);
    field_bindings_.push_back({registered_name, [field, registered_name](Package<T>& package) {
                                 Value value = package.GetLiveValue(registered_name.c_str());
                                 if (value.IsValid()) {
                                   detail::field_from_value(package.object_->*field, value);
                                 }
                               }});
    return AddValue(registered_name.c_str(), detail::value_from_field(host_, object_->*field));
  }

  template <typename R>
  bool AddExpressionDecorator(const char* name, R(T::*method)(std::vector<Value>)) {
    using Binding = detail::MemberBinding<T, R, std::vector<Value>>;
    auto* binding = new Binding{host_, object_, method};
    auto callback = +[](X3CallContext* context, X3Runtime*, void* data,
                        const X3Value* args, uint32_t argc, X3Value* result) -> X3Status {
      auto* b = static_cast<Binding*>(data);
      try {
        std::vector<Value> values;
        values.reserve(argc);
        for (uint32_t i = 0; i < argc; ++i) values.emplace_back(b->host, args[i], true);
        *result = detail::return_to_raw(b->host, (b->object->*b->method)(std::move(values)));
        return X3_STATUS_OK;
      } catch (const std::exception& ex) {
        return detail::set_native_error(b->host, context, ex.what());
      }
    };
    if (!AddRawParamFunc(name, callback, binding, 0, UINT32_MAX, X3_NATIVE_CAPTURE_EXPRESSIONS)) {
      delete binding;
      return false;
    }
    bindings_.push_back({binding, detail::BindingCleanupFor<Binding>::cleanup});
    return true;
  }

  bool AddRawParamFunc(
      const char* name,
      X3NativeFn callback,
      void* user_data = nullptr,
      uint32_t min_argc = 0,
      uint32_t max_argc = UINT32_MAX, uint32_t flags = 0) {
    if (module_ == nullptr || name == nullptr || callback == nullptr) return false;
    X3NativeFunctionDef def{};
    def.size = sizeof(def);
    def.name = name;
    def.callback = callback;
    def.user_data = user_data;
    def.min_argc = min_argc;
    def.max_argc = max_argc;
    def.flags = flags;
    return host_->module_add_function(module_, &def) == X3_STATUS_OK;
  }

  bool AddValue(const char* name, const Value& value) {
    if (module_ == nullptr || name == nullptr ||
        host_->module_add_value(module_, name, value.raw()) != X3_STATUS_OK) {
      return false;
    }
    values_[name] = value;
    return true;
  }

  bool AddConst(const char* name, const Value& value) {
    return AddValue(name, value);
  }

  bool AddConst(const char* name, int value) {
    return AddValue(name, Value(value));
  }

  bool AddConst(const char* name, long long value) {
    return AddValue(name, Value(value));
  }

  bool AddConst(const char* name, bool value) {
    return AddValue(name, Value(value));
  }

  bool AddConst(const char* name, double value) {
    return AddValue(name, Value(value));
  }

  bool AddConst(const char* name, const char* value) {
    return host_ != nullptr && AddValue(name, Value::String(host_, value == nullptr ? std::string() : std::string(value)));
  }

  bool AddConst(const char* name, const std::string& value) {
    return host_ != nullptr && AddValue(name, Value::String(host_, value));
  }

  Value AddEvent(const char* name) {
    if (module_ == nullptr || name == nullptr || host_ == nullptr || host_->event_create == nullptr) {
      return {};
    }
    X3Value event = x3_value_invalid();
    if (host_->event_create(host_->runtime, name, &event) != X3_STATUS_OK) {
      return {};
    }
    Value value(host_, event, false);
    if (!AddValue(name, value)) {
      return {};
    }
    return value;
  }

  template <uint32_t Argc, typename ClassT>
  bool AddClass(const char* name, ClassT* borrowed = nullptr) {
    ClassT::BuildAPI();
    X3Value klass = x3_value_invalid();
    if (ClassT::APISET().template CreateClass<Argc>(host_, module_, name, &klass, borrowed) != X3_STATUS_OK) {
      return false;
    }
    values_[name] = Value(host_, klass, false);
    return true;
  }

  template <uint32_t Argc, typename ClassT, typename ParentT>
  bool AddClass(const char* name) {
    (void)static_cast<ParentT*>(nullptr);
    return AddClass<Argc, ClassT>(name);
  }

  template <typename ClassT>
  bool AddVarClass(const char* name) {
    return AddClass<0, ClassT>(name);
  }

  template <typename ClassT>
  Value CreateInstance(const char* class_name, ClassT* object, bool owns_object = true) const {
    std::unique_ptr<ClassT> owner(owns_object ? object : nullptr);
    if (host_ == nullptr || class_name == nullptr || object == nullptr || host_->value_instance == nullptr ||
        host_->instance_set_native_data == nullptr) {
      return {};
    }
    auto it = values_.find(class_name);
    if (it == values_.end()) {
      return {};
    }
    X3Value instance = host_->value_instance(host_->runtime, it->second.raw());
    if (instance.tag == X3_TAG_INVALID) {
      return {};
    }
    Value result(host_, instance, false);
    object->__xlang3_host_ = host_;
    X3Status status = owns_object
        ? detail::attach_owned_instance(host_, instance, std::move(owner))
        : host_->instance_set_native_data(instance, detail::native_type_name<ClassT>(), object,
            detail::noop_native_instance_cleanup<ClassT>);
    if (status != X3_STATUS_OK) return {};
    if (!owns_object && ClassT::APISET().BindNativeInstance(host_, instance, object) != X3_STATUS_OK) return {};
    return result;
  }

  template<class ClassT>
  Value CreateInstance(const char* class_name, std::shared_ptr<ClassT> object) const {
    if (!object || !host_ || !host_->instance_set_native_owner) return {};
    auto owner = std::make_unique<std::shared_ptr<ClassT>>(std::move(object));
    auto instance = CreateInstance(class_name, owner->get(), false);
    if (!instance.IsValid()) return {};
    if (host_->instance_set_native_owner(instance.raw(), detail::native_type_name<ClassT>(), owner->get(), owner.get(),
        [](void* data) { delete static_cast<std::shared_ptr<ClassT>*>(data); }) != X3_STATUS_OK) return {};
    auto* native = owner->get();
    owner.release();
    if (ClassT::APISET().BindNativeInstance(host_, instance.raw(), native) != X3_STATUS_OK) return {};
    return instance;
  }

  bool RegisterCleanup() {
    if (host_ == nullptr || object_ == nullptr) return false;
    return host_->package_set_cleanup(
               host_,
               this,
               [](void* data) {
                 auto* package = static_cast<Package*>(data);
                 delete package;
               }) == X3_STATUS_OK;
  }

private:
  struct BindingCleanup {
    void* data = nullptr;
    void (*cleanup)(void*) = nullptr;
  };
  struct FieldBinding {
    std::string name;
    std::function<void(Package<T>&)> sync;
  };

  X3PackageHost* host_ = nullptr;
  X3Module* module_ = nullptr;
  T* object_ = nullptr;
  bool owns_object_ = true;
  Value cur_module_;
  std::unordered_map<std::string, Value> values_;
  std::vector<BindingCleanup> bindings_;
  std::vector<FieldBinding> field_bindings_;
};

template <typename T>
class APISet {
public:
  explicit APISet(const char* module_name = nullptr) : module_name_(module_name == nullptr ? std::string() : std::string(module_name)) {}

  template<class Base>
  bool AddBase() {
    static_assert(std::is_base_of_v<Base, T>, "AddBase requires a C++ base class");
    Base::BuildAPI();
    auto& base = Base::APISET();
    class_method_builders_.insert(class_method_builders_.end(),
        base.class_method_builders_.begin(), base.class_method_builders_.end());
    class_attr_builders_.insert(class_attr_builders_.end(),
        base.class_attr_builders_.begin(), base.class_attr_builders_.end());
    base_casts_.push_back([](T* object, const char* name) -> void* {
      return Base::APISET().CastNative(static_cast<Base*>(object), name);
    });
    base_hosts_.push_back([](T* object, X3PackageHost* host) {
      Base::APISET().BindHost(static_cast<Base*>(object), host);
    });
    return true;
  }

  template<class Interface>
  bool AddInterface() {
    static_assert(std::is_base_of_v<Interface, T>, "AddInterface requires a C++ base interface");
    base_casts_.push_back([](T* object, const char* name) -> void* {
      return std::strcmp(name, detail::native_type_name<Interface>()) == 0
          ? static_cast<Interface*>(object) : nullptr;
    });
    return true;
  }

  void* CastNative(T* object, const char* name) const {
    if (!object || !name) return nullptr;
    if (std::strcmp(name, detail::native_type_name<T>()) == 0) return object;
    for (auto cast : base_casts_) if (auto* base = cast(object, name)) return base;
    return nullptr;
  }

  void BindHost(T* object, X3PackageHost* host) const {
    object->__xlang3_host_ = host;
    if (!host) object->__xlang3_events_.reset();
    for (auto bind : base_hosts_) bind(object, host);
  }

  X3Status BindNativeInstance(X3PackageHost* host, X3Value instance, T* object) const {
    BindHost(object, host);
    if (base_casts_.empty()) return X3_STATUS_OK;
    if (!host || host->size < offsetof(X3PackageHost, instance_set_native_cast) + sizeof(host->instance_set_native_cast) ||
        !host->instance_set_native_cast) return X3_STATUS_ERROR;
    return host->instance_set_native_cast(instance, [](void* data, const char* name) -> void* {
      return T::APISET().CastNative(static_cast<T*>(data), name);
    });
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...), uint32_t flags = 0) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method, flags](Package<T>& package) {
      return package.template AddFunc<Argc>(registered_name.c_str(), method, flags);
    });
    class_method_builders_.push_back([registered_name, method, flags](X3PackageHost* host, std::vector<X3NativeFunctionDef>& methods) {
      auto* binding = new detail::InstanceMemberBinding<T, R, Args...>{host, registered_name, method};
      X3NativeFunctionDef def{};
      def.size = sizeof(def);
      def.name = binding->name.c_str();
      def.callback = &detail::instance_member_thunk<T, R, Args...>;
      def.flags = flags;
      def.user_data = binding;
      def.min_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      def.max_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      methods.push_back(def);
    });
    return true;
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const, uint32_t flags = 0) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method, flags](Package<T>& package) {
      return package.template AddFunc<Argc>(registered_name.c_str(), method, flags);
    });
    class_method_builders_.push_back([registered_name, method, flags](X3PackageHost* host, std::vector<X3NativeFunctionDef>& methods) {
      auto* binding = new detail::ConstInstanceMemberBinding<T, R, Args...>{host, registered_name, method};
      X3NativeFunctionDef def{};
      def.size = sizeof(def);
      def.name = binding->name.c_str();
      def.callback = &detail::const_instance_member_thunk<T, R, Args...>;
      def.flags = flags;
      def.user_data = binding;
      def.min_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      def.max_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      methods.push_back(def);
    });
    return true;
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...), uint32_t flags = 0) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method, flags](Package<T>& package) {
      return package.AddFunc(registered_name.c_str(), method, flags);
    });
    return true;
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const, uint32_t flags = 0) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method, flags](Package<T>& package) {
      return package.AddFunc(registered_name.c_str(), method, flags);
    });
    return true;
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFuncEx(const char* name, R(T::*method)(Args...)) {
    return AddFunc<Argc>(name, method);
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddRTFunc(const char* name, R(T::*method)(Args...)) {
    return AddFunc<Argc>(name, method);
  }

  template <typename R, typename... Args>
  bool AddVarFunc(const char* name, R(T::*method)(Args...)) {
    return AddFunc(name, method);
  }

  template <typename R, typename... Args>
  bool AddVarFuncEx(const char* name, R(T::*method)(Args...)) {
    return AddFunc(name, method);
  }

  bool AddVarFunc(const char* name, Value(T::*method)(const ARGS&, const KWARGS&)) {
    const std::string registered_name(name);
    registrations_.push_back([registered_name, method](Package<T>& package) {
      return package.AddVarFunc(registered_name.c_str(), method);
    });
    class_method_builders_.push_back([registered_name, method](X3PackageHost* host, std::vector<X3NativeFunctionDef>& methods) {
      using Binding = detail::VariableBinding<T>;
      auto binding = std::make_unique<Binding>(Binding{host, nullptr, method});
      X3NativeFunctionDef def{};
      def.size = sizeof(def); def.name = registered_name.c_str(); def.user_data = binding.get();
      def.keyword_callback = &Binding::Invoke;
      if (host->package_set_cleanup(host, binding.get(), &detail::BindingCleanupFor<Binding>::cleanup) != X3_STATUS_OK)
        throw Error("cannot retain native keyword binding");
      binding.release();
      methods.push_back(def);
    });
    return true;
  }

  template<class Setter, class Getter>
  bool AddPropL(const char* name, Setter setter, Getter getter) {
    const std::string registered_name(name);
    registrations_.push_back([](Package<T>&) -> bool {
      throw Error("writable properties must be registered on a native class");
    });
    class_attr_builders_.push_back([registered_name, setter, getter](X3PackageHost* host, X3Value klass) {
      struct Binding { X3PackageHost* host; Setter setter; Getter getter; };
      auto binding = std::make_unique<Binding>(Binding{host, setter, getter});
      auto get = +[](X3CallContext* context, X3Runtime*, void* data, const X3Value* args, uint32_t argc, X3Value* result) -> X3Status {
        auto& b = *static_cast<Binding*>(data);
        try {
          auto* self = argc == 1 ? static_cast<T*>(b.host->instance_get_native_data(args[0], detail::native_type_name<T>())) : nullptr;
          if (!self) throw Error("property requires a native instance");
          *result = detail::return_to_raw(b.host, b.getter(self));
          return X3_STATUS_OK;
        } catch (const std::exception& e) { return detail::set_native_error(b.host, context, e.what()); }
      };
      auto set = +[](X3CallContext* context, X3Runtime*, void* data, const X3Value* args, uint32_t argc, X3Value* result) -> X3Status {
        auto& b = *static_cast<Binding*>(data);
        try {
          auto* self = argc == 2 ? static_cast<T*>(b.host->instance_get_native_data(args[0], detail::native_type_name<T>())) : nullptr;
          if (!self) throw Error("property requires an instance and a value");
          b.setter(self, Value(b.host, args[1], true));
          *result = x3_value_none();
          return X3_STATUS_OK;
        } catch (const std::exception& e) { return detail::set_native_error(b.host, context, e.what()); }
      };
      X3Value property = x3_value_invalid();
      if (host->property_create(host->runtime, registered_name.c_str(), get, set, binding.get(), &property) != X3_STATUS_OK) return false;
      Value owned_property(host, property, false);
      if (host->package_set_cleanup(host, binding.get(), &detail::BindingCleanupFor<Binding>::cleanup) != X3_STATUS_OK) return false;
      binding.release();
      return host->class_add_value(klass, registered_name.c_str(), property) == X3_STATUS_OK;
    });
    return true;
  }

  template <typename R>
  bool AddProp(const char* name, R(T::*getter)()) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, getter](Package<T>& package) {
      return package.template AddFunc<0>(registered_name.c_str(), getter);
    });
    class_attr_builders_.push_back([registered_name, getter](X3PackageHost* host, X3Value klass) {
      if (host == nullptr || host->property_create == nullptr || host->class_add_value == nullptr) {
        return false;
      }
      auto* binding = new detail::InstanceMemberBinding<T, R>{host, registered_name, getter};
      X3Value property = x3_value_invalid();
      if (host->property_create(
              host->runtime,
              registered_name.c_str(),
              &detail::instance_member_thunk<T, R>,
              nullptr,
              binding,
              &property) != X3_STATUS_OK) {
        delete binding;
        return false;
      }
      const X3Status status = host->class_add_value(klass, registered_name.c_str(), property);
      if (host->value_release != nullptr) {
        host->value_release(property);
      }
      return status == X3_STATUS_OK;
    });
    return true;
  }

  template <typename R>
  bool AddProp(const char* name, R(T::*getter)() const) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, getter](Package<T>& package) {
      return package.template AddFunc<0>(registered_name.c_str(), getter);
    });
    class_attr_builders_.push_back([registered_name, getter](X3PackageHost* host, X3Value klass) {
      if (host == nullptr || host->property_create == nullptr || host->class_add_value == nullptr) {
        return false;
      }
      auto* binding = new detail::ConstInstanceMemberBinding<T, R>{host, registered_name, getter};
      X3Value property = x3_value_invalid();
      if (host->property_create(
              host->runtime,
              registered_name.c_str(),
              &detail::const_instance_member_thunk<T, R>,
              nullptr,
              binding,
              &property) != X3_STATUS_OK) {
        delete binding;
        return false;
      }
      const X3Status status = host->class_add_value(klass, registered_name.c_str(), property);
      if (host->value_release != nullptr) {
        host->value_release(property);
      }
      return status == X3_STATUS_OK;
    });
    return true;
  }

  template <typename FieldT>
  bool AddProp0(const char* name, FieldT T::*field) {
    return AddPropWithType(name, field);
  }

  template <typename FieldT>
  bool AddPropWithType(const char* name, FieldT T::*field) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, field](Package<T>& package) {
      return package.AddPropWithType(registered_name.c_str(), field);
    });
    return true;
  }

  bool AddRawParamFunc(
      const char* name,
      X3NativeFn callback,
      void* user_data = nullptr,
      uint32_t min_argc = 0,
      uint32_t max_argc = UINT32_MAX) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, callback, user_data, min_argc, max_argc](Package<T>& package) {
      return package.AddRawParamFunc(registered_name.c_str(), callback, user_data, min_argc, max_argc);
    });
    return true;
  }

  template <typename R>
  bool AddExpressionDecorator(const char* name, R(T::*method)(std::vector<Value>)) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method](Package<T>& package) {
      return package.AddExpressionDecorator(registered_name.c_str(), method);
    });
    return true;
  }

  bool AddConst(const char* name, const Value& value) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, value](Package<T>& package) {
      return package.AddConst(registered_name.c_str(), value);
    });
    class_attr_builders_.push_back([registered_name, value](X3PackageHost* host, X3Value klass) {
      if (host == nullptr || host->class_add_value == nullptr) {
        return false;
      }
      X3Value raw = value.raw();
      if (raw.tag == X3_TAG_OBJECT && host->value_retain != nullptr) {
        host->value_retain(raw);
      }
      const X3Status status = host->class_add_value(klass, registered_name.c_str(), raw);
      if (host->value_release != nullptr) {
        host->value_release(raw);
      }
      return status == X3_STATUS_OK;
    });
    return true;
  }

  template <typename V>
  bool AddConst(const char* name, V value) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, value](Package<T>& package) {
      return package.AddConst(registered_name.c_str(), value);
    });
    class_attr_builders_.push_back([registered_name, value](X3PackageHost* host, X3Value klass) {
      if (host == nullptr || host->class_add_value == nullptr) {
        return false;
      }
      X3Value raw = detail::return_to_raw(host, value);
      const X3Status status = host->class_add_value(klass, registered_name.c_str(), raw);
      if (host->value_release != nullptr) {
        host->value_release(raw);
      }
      return status == X3_STATUS_OK;
    });
    return true;
  }

  bool AddEvent(const char* name) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name](Package<T>& package) {
      return package.AddEvent(registered_name.c_str()).IsEvent();
    });
    class_attr_builders_.push_back([registered_name](X3PackageHost* host, X3Value klass) {
      if (host == nullptr || host->property_create == nullptr || host->class_add_value == nullptr) {
        return false;
      }
      struct Binding { X3PackageHost* host; std::string name; };
      auto binding = std::make_unique<Binding>(Binding{host, registered_name});
      auto getter = +[](X3CallContext* context, X3Runtime*, void* data, const X3Value* args, uint32_t argc, X3Value* result) -> X3Status {
        auto& binding = *static_cast<Binding*>(data);
        try {
          auto* object = argc == 1 ? static_cast<T*>(binding.host->instance_get_native_data(args[0], detail::native_type_name<T>())) : nullptr;
          if (!object) throw Error("event requires a native instance");
          *result = detail::return_to_raw(binding.host, object->GetEvent(binding.name.c_str()));
          return X3_STATUS_OK;
        } catch (const std::exception& exception) { return detail::set_native_error(binding.host, context, exception.what()); }
      };
      X3Value raw = x3_value_invalid();
      if (host->property_create(host->runtime, registered_name.c_str(), getter, nullptr, binding.get(), &raw) != X3_STATUS_OK) return false;
      Value property(host, raw, false);
      if (host->package_set_cleanup(host, binding.get(), &detail::BindingCleanupFor<Binding>::cleanup) != X3_STATUS_OK) return false;
      binding.release();
      return host->class_add_value(klass, registered_name.c_str(), raw) == X3_STATUS_OK;
    });
    return true;
  }

  template <uint32_t Argc>
  X3Status CreateClass(X3PackageHost* host, X3Module* module, const char* name,
      X3Value* out_class = nullptr, T* borrowed = nullptr) {
    if (host == nullptr || name == nullptr ||
        (module ? host->module_add_class == nullptr : host->create_class == nullptr)) {
      return X3_STATUS_ERROR;
    }
    std::vector<X3NativeFunctionDef> methods;
    methods.reserve(class_method_builders_.size() + 1);
    X3NativeFunctionDef init{};
    init.size = sizeof(init);
    init.name = "__init__";
    init.callback = &detail::constructor_thunk<T, Argc>;
    auto binding = std::make_unique<detail::ConstructorBinding<T>>(detail::ConstructorBinding<T>{host, borrowed});
    init.user_data = binding.get();
    if (host->package_set_cleanup(host, binding.get(), [](void* data) {
          auto* binding = static_cast<detail::ConstructorBinding<T>*>(data);
          if (binding->borrowed && binding->borrowed->__xlang3_host_ == binding->host) {
            T::APISET().BindHost(binding->borrowed, nullptr);
          }
          delete binding;
        }) != X3_STATUS_OK) return X3_STATUS_ERROR;
    binding.release();
    init.min_argc = Argc + 1;
    init.max_argc = Argc + 1;
    methods.push_back(init);
    for (auto& builder : class_method_builders_) {
      builder(host, methods);
    }
    X3Value klass = x3_value_invalid();
    const X3Status status = module
        ? host->module_add_class(module, name, methods.data(), static_cast<uint32_t>(methods.size()), &klass)
        : host->create_class(host, name, methods.data(), static_cast<uint32_t>(methods.size()), &klass);
    if (status != X3_STATUS_OK) {
      return status;
    }
    bool attrs_ok = true;
    for (auto& builder : class_attr_builders_) {
      attrs_ok = builder(host, klass) && attrs_ok;
    }
    if (!attrs_ok) {
      if (host->value_release != nullptr) {
        host->value_release(klass);
      }
      return X3_STATUS_ERROR;
    }
    if (serializer_builder_ && !serializer_builder_(host)) {
      host->value_release(klass);
      return X3_STATUS_ERROR;
    }
    if (out_class != nullptr) {
      *out_class = klass;
    } else if (host->value_release != nullptr) {
      host->value_release(klass);
    }
    return X3_STATUS_OK;
  }

  template<class Encode, class Decode>
  bool AddSerializer(const char* type_id, uint32_t version, Encode encode, Decode decode) {
    if (!type_id || !*type_id || serializer_builder_) return false;
    serializer_builder_ = [id = std::string(type_id), version, encode, decode](X3PackageHost* host) {
      struct Binding { X3PackageHost* host; Encode encode; Decode decode; };
      auto binding = std::make_unique<Binding>(Binding{host, encode, decode});
      X3NativeSerializerDef def{};
      def.size = sizeof(def);
      def.type_id = id.c_str();
      def.native_type = detail::native_type_name<T>();
      def.version = version;
      def.user_data = binding.get();
      def.cleanup = [](void* data) { delete static_cast<Binding*>(data); };
      def.encode = [](X3Runtime*, X3Value instance, void* data, X3Value* out) -> X3Status {
        auto& binding = *static_cast<Binding*>(data);
        auto* object = static_cast<T*>(binding.host->instance_get_native_data(instance, detail::native_type_name<T>()));
        if (!object) return X3_STATUS_ERROR;
        try {
          Value state = std::invoke(binding.encode, object);
          if (!state.IsValid()) return X3_STATUS_ERROR;
          *out = state.Detach();
          return X3_STATUS_OK;
        } catch (...) { return X3_STATUS_ERROR; }
      };
      def.decode = [](X3Runtime*, X3Value instance, X3Value state, void* data) -> X3Status {
        auto& binding = *static_cast<Binding*>(data);
        try {
          auto object = std::make_unique<T>();
          T::APISET().BindHost(object.get(), binding.host);
          Value value(binding.host, state, true);
          std::invoke(binding.decode, object.get(), value);
          return detail::attach_owned_instance(binding.host, instance, std::move(object));
        } catch (...) { return X3_STATUS_ERROR; }
      };
      if (!host->register_native_serializer ||
          host->register_native_serializer(host->runtime, &def) != X3_STATUS_OK) return false;
      binding.release();
      return true;
    };
    return true;
  }

  template <uint32_t Argc, typename ClassT>
  bool AddClass(const char* name, ClassT* borrowed = nullptr) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, borrowed](Package<T>& package) {
      return package.template AddClass<Argc, ClassT>(registered_name.c_str(), borrowed);
    });
    class_attr_builders_.push_back([registered_name, borrowed](X3PackageHost* host, X3Value parent) {
      ClassT::BuildAPI();
      X3Value child = x3_value_invalid();
      if (ClassT::APISET().template CreateClass<Argc>(host, nullptr, registered_name.c_str(), &child, borrowed) != X3_STATUS_OK)
        return false;
      const auto status = host->class_add_value(parent, registered_name.c_str(), child);
      host->value_release(child);
      return status == X3_STATUS_OK;
    });
    return true;
  }

  template <uint32_t Argc, typename ClassT, typename ParentT>
  bool AddClass(const char* name) {
    return AddClass<Argc, ClassT>(name);
  }

  template <typename ClassT>
  bool AddVarClass(const char* name) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name](Package<T>& package) {
      return package.template AddVarClass<ClassT>(registered_name.c_str());
    });
    return true;
  }

  X3Status Create(X3PackageHost* host, const char* module_name, X3Value cur_module) {
    if (host == nullptr) {
      return X3_STATUS_ERROR;
    }
    const char* effective_module_name = module_name != nullptr && module_name[0] != '\0' ? module_name : module_name_.c_str();
    T* object = nullptr;
    if constexpr (std::is_default_constructible_v<T>) {
      object = new T();
    } else {
    object = new T(host);
  }
    return CreateObject(host, effective_module_name, cur_module, object, true);
  }

  X3Status CreateBorrowed(X3PackageHost* host, const char* module_name, X3Value cur_module, T& object) {
    if (!host || object.__xlang3_package_) return X3_STATUS_ERROR;
    return CreateObject(host, module_name, cur_module, &object, false);
  }

private:
  X3Status CreateObject(X3PackageHost* host, const char* module_name, X3Value cur_module, T* object, bool owns) {
    auto package_owner = std::make_unique<Package<T>>(host, module_name, object, owns);
    auto* package = package_owner.get();
    object->__xlang3_host_ = host;
    object->__xlang3_package_ = package;
    package->SetCurrentModule(Value(host, cur_module, true));
    bool ok = true;
    for (auto& registration : registrations_) {
      ok = registration(*package) && ok;
    }
    detail::call_package_created(object, package, 0);
    if (!ok || !package->RegisterCleanup()) return X3_STATUS_ERROR;
    package_owner.release();
    return X3_STATUS_OK;
  }

private:
  template<class> friend class APISet;
  std::vector<void* (*)(T*, const char*)> base_casts_;
  std::vector<void (*)(T*, X3PackageHost*)> base_hosts_;
  std::string module_name_;
  std::vector<std::function<bool(Package<T>&)>> registrations_;
  std::vector<std::function<void(X3PackageHost*, std::vector<X3NativeFunctionDef>&)>> class_method_builders_;
  std::vector<std::function<bool(X3PackageHost*, X3Value)>> class_attr_builders_;
  std::function<bool(X3PackageHost*)> serializer_builder_;
};

} // namespace X

#if defined(_WIN32)
#define XLANG3_PACKAGE_EXPORT __declspec(dllexport)
#else
#define XLANG3_PACKAGE_EXPORT __attribute__((visibility("default")))
#endif

#define BEGIN_PACKAGE(PackageClass) \
public: \
  using __xlang3_package_class = PackageClass; \
  static X::APISet<PackageClass>& APISET() { \
    static X::APISet<PackageClass> apiset(#PackageClass); \
    return apiset; \
  } \
  static void BuildAPI() { \
    static std::once_flag xlang3_package_build_once; \
    std::call_once(xlang3_package_build_once, [] {

#define END_PACKAGE \
    }); \
  } \
  X3PackageHost* __xlang3_host_ = nullptr; \
  X::Package<__xlang3_package_class>* __xlang3_package_ = nullptr; \
  X::detail::EventStorage __xlang3_events_; \
  X3PackageHost* Host() const { \
    return __xlang3_package_ == nullptr ? __xlang3_host_ : __xlang3_package_->host(); \
  } \
  X::Value GetEvent(const char* name) const { \
    return __xlang3_package_ == nullptr ? \
        X::detail::instance_event(*const_cast<__xlang3_package_class*>(this), name, __xlang3_events_, Host()) : \
        __xlang3_package_->GetValue(name); \
  }

#define XLANG3_IMPLEMENT_PACKAGE(PackageClass) \
  extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION; \
  extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_ptr, X3Value curModule) { \
    auto* host = static_cast<X3PackageHost*>(host_ptr); \
    if (host == nullptr) { \
      return X3_STATUS_ERROR; \
    } \
    PackageClass::BuildAPI(); \
    return PackageClass::APISET().Create(host, #PackageClass, curModule); \
  }
