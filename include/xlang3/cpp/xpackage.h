/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/cpp/xvalue.h"

#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace X {

template <typename T>
class Package;

namespace detail {

template <typename T>
struct always_false : std::false_type {};

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
std::decay_t<T> arg_from_value(Value& value) {
  using U = std::decay_t<T>;
  if constexpr (std::is_same_v<U, Value>) {
    return value;
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
  if constexpr (std::is_void_v<R>) {
    (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
    *result = x3_value_none();
  } else {
    *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
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
  if constexpr (std::is_void_v<R>) {
    (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
    *result = x3_value_none();
  } else {
    *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
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
  if constexpr (std::is_void_v<R>) {
    (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
    *result = x3_value_none();
  } else {
    *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
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
  if constexpr (std::is_void_v<R>) {
    (self->*method)(arg_from_value<Args>(std::get<I>(values))...);
    *result = x3_value_none();
  } else {
    *result = return_to_raw(host, (self->*method)(arg_from_value<Args>(std::get<I>(values))...));
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

template <typename T>
auto call_package_created(T* object, Package<T>* package, int) -> decltype(object->OnPackageCreated(package), void()) {
  object->OnPackageCreated(package);
}

template <typename T>
void call_package_created(T*, Package<T>*, long) {}

template <typename T, uint32_t Argc>
X3Status constructor_thunk(
    X3CallContext* context,
    X3Runtime*,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = static_cast<X3PackageHost*>(user_data);
  if (host == nullptr || result == nullptr || argc < 1) {
    return X3_STATUS_ERROR;
  }
  if (argc - 1 < Argc) {
    host->set_error(context, "native constructor received too few arguments");
    return X3_STATUS_ERROR;
  }
  T* object = nullptr;
  if constexpr (Argc == 0) {
    object = new T();
  } else if constexpr (Argc == 1 && std::is_constructible_v<T, std::string>) {
    Value arg0(host, args[1], true);
    object = new T(arg0.ToString(false));
  } else if constexpr (std::is_default_constructible_v<T>) {
    object = new T();
  } else {
    host->set_error(context, "native constructor signature is not supported by xlang3 C++ binding");
    return X3_STATUS_ERROR;
  }
  object->__xlang3_host_ = host;
  if (host->instance_set_native_data(args[0], native_type_name<T>(), object, cleanup_native_instance<T>) != X3_STATUS_OK) {
    delete object;
    host->set_error(context, "failed to attach native instance data");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

} // namespace detail

template <typename T>
class Package {
public:
  Package(X3PackageHost* host, const char* module_name, T* object) : host_(host), object_(object) {
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
    delete object_;
  }

  Package(const Package&) = delete;
  Package& operator=(const Package&) = delete;

  X3PackageHost* host() const { return host_; }
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
  bool AddFunc(const char* name, R(T::*method)(Args...)) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    return AddFunc(name, method);
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    return AddFunc(name, method);
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...)) {
    if (module_ == nullptr || name == nullptr) return false;
    auto* binding = new detail::MemberBinding<T, R, Args...>{host_, object_, method};
    bindings_.push_back({binding, detail::BindingCleanupFor<detail::MemberBinding<T, R, Args...>>::cleanup});
    X3NativeFunctionDef def{};
    def.size = sizeof(def);
    def.name = name;
    def.callback = &detail::member_thunk<T, R, Args...>;
    def.user_data = binding;
    def.min_argc = static_cast<uint32_t>(sizeof...(Args));
    def.max_argc = static_cast<uint32_t>(sizeof...(Args));
    return host_->module_add_function(module_, &def) == X3_STATUS_OK;
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const) {
    if (module_ == nullptr || name == nullptr) return false;
    auto* binding = new detail::ConstMemberBinding<T, R, Args...>{host_, object_, method};
    bindings_.push_back({binding, detail::BindingCleanupFor<detail::ConstMemberBinding<T, R, Args...>>::cleanup});
    X3NativeFunctionDef def{};
    def.size = sizeof(def);
    def.name = name;
    def.callback = &detail::const_member_thunk<T, R, Args...>;
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

  bool AddRawParamFunc(
      const char* name,
      X3NativeFn callback,
      void* user_data = nullptr,
      uint32_t min_argc = 0,
      uint32_t max_argc = UINT32_MAX) {
    if (module_ == nullptr || name == nullptr || callback == nullptr) return false;
    X3NativeFunctionDef def{};
    def.size = sizeof(def);
    def.name = name;
    def.callback = callback;
    def.user_data = user_data;
    def.min_argc = min_argc;
    def.max_argc = max_argc;
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
  bool AddClass(const char* name) {
    ClassT::BuildAPI();
    X3Value klass = x3_value_invalid();
    if (ClassT::APISET().template CreateClass<Argc>(host_, module_, name, &klass) != X3_STATUS_OK) {
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
    object->__xlang3_host_ = host_;
    X3Status status = host_->instance_set_native_data(
        instance,
        detail::native_type_name<ClassT>(),
        object,
        owns_object ? detail::cleanup_native_instance<ClassT> : detail::noop_native_instance_cleanup<ClassT>);
    if (status != X3_STATUS_OK) {
      if (host_->value_release != nullptr) {
        host_->value_release(instance);
      }
      if (owns_object) {
        delete object;
      }
      return {};
    }
    return Value(host_, instance, false);
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
  Value cur_module_;
  std::unordered_map<std::string, Value> values_;
  std::vector<BindingCleanup> bindings_;
  std::vector<FieldBinding> field_bindings_;
};

template <typename T>
class APISet {
public:
  explicit APISet(const char* module_name = nullptr) : module_name_(module_name == nullptr ? std::string() : std::string(module_name)) {}

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...)) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method](Package<T>& package) {
      return package.template AddFunc<Argc>(registered_name.c_str(), method);
    });
    class_method_builders_.push_back([registered_name, method](X3PackageHost* host, std::vector<X3NativeFunctionDef>& methods) {
      auto* binding = new detail::InstanceMemberBinding<T, R, Args...>{host, registered_name, method};
      X3NativeFunctionDef def{};
      def.size = sizeof(def);
      def.name = binding->name.c_str();
      def.callback = &detail::instance_member_thunk<T, R, Args...>;
      def.user_data = binding;
      def.min_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      def.max_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      methods.push_back(def);
    });
    return true;
  }

  template <uint32_t Argc, typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const) {
    static_assert(Argc == sizeof...(Args), "AddFunc<N> argument count must match method signature");
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method](Package<T>& package) {
      return package.template AddFunc<Argc>(registered_name.c_str(), method);
    });
    class_method_builders_.push_back([registered_name, method](X3PackageHost* host, std::vector<X3NativeFunctionDef>& methods) {
      auto* binding = new detail::ConstInstanceMemberBinding<T, R, Args...>{host, registered_name, method};
      X3NativeFunctionDef def{};
      def.size = sizeof(def);
      def.name = binding->name.c_str();
      def.callback = &detail::const_instance_member_thunk<T, R, Args...>;
      def.user_data = binding;
      def.min_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      def.max_argc = static_cast<uint32_t>(sizeof...(Args) + 1);
      methods.push_back(def);
    });
    return true;
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...)) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method](Package<T>& package) {
      return package.AddFunc(registered_name.c_str(), method);
    });
    return true;
  }

  template <typename R, typename... Args>
  bool AddFunc(const char* name, R(T::*method)(Args...) const) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name, method](Package<T>& package) {
      return package.AddFunc(registered_name.c_str(), method);
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
      if (host == nullptr || host->event_create == nullptr || host->class_add_value == nullptr) {
        return false;
      }
      X3Value event = x3_value_invalid();
      if (host->event_create(host->runtime, registered_name.c_str(), &event) != X3_STATUS_OK) {
        return false;
      }
      const X3Status status = host->class_add_value(klass, registered_name.c_str(), event);
      if (host->value_release != nullptr) {
        host->value_release(event);
      }
      return status == X3_STATUS_OK;
    });
    return true;
  }

  template <uint32_t Argc>
  X3Status CreateClass(X3PackageHost* host, X3Module* module, const char* name, X3Value* out_class = nullptr) {
    if (host == nullptr || module == nullptr || name == nullptr || host->module_add_class == nullptr) {
      return X3_STATUS_ERROR;
    }
    std::vector<X3NativeFunctionDef> methods;
    methods.reserve(class_method_builders_.size() + 1);
    X3NativeFunctionDef init{};
    init.size = sizeof(init);
    init.name = "__init__";
    init.callback = &detail::constructor_thunk<T, Argc>;
    init.user_data = host;
    init.min_argc = Argc + 1;
    init.max_argc = Argc + 1;
    methods.push_back(init);
    for (auto& builder : class_method_builders_) {
      builder(host, methods);
    }
    X3Value klass = x3_value_invalid();
    const X3Status status = host->module_add_class(module, name, methods.data(), static_cast<uint32_t>(methods.size()), &klass);
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
    if (out_class != nullptr) {
      *out_class = klass;
    } else if (host->value_release != nullptr) {
      host->value_release(klass);
    }
    return X3_STATUS_OK;
  }

  template <uint32_t Argc, typename ClassT>
  bool AddClass(const char* name) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name](Package<T>& package) {
      return package.template AddClass<Argc, ClassT>(registered_name.c_str());
    });
    return true;
  }

  template <uint32_t Argc, typename ClassT, typename ParentT>
  bool AddClass(const char* name) {
    const std::string registered_name = name == nullptr ? std::string() : std::string(name);
    registrations_.push_back([registered_name](Package<T>& package) {
      return package.template AddClass<Argc, ClassT, ParentT>(registered_name.c_str());
    });
    return true;
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
    auto* package = new Package<T>(host, effective_module_name, object);
    object->__xlang3_host_ = host;
    object->__xlang3_package_ = package;
    package->SetCurrentModule(Value(host, cur_module, true));
    bool ok = true;
    for (auto& registration : registrations_) {
      ok = registration(*package) && ok;
    }
    detail::call_package_created(object, package, 0);
    ok = package->RegisterCleanup() && ok;
    if (!ok) {
      delete package;
      return X3_STATUS_ERROR;
    }
    return X3_STATUS_OK;
  }

private:
  std::string module_name_;
  std::vector<std::function<bool(Package<T>&)>> registrations_;
  std::vector<std::function<void(X3PackageHost*, std::vector<X3NativeFunctionDef>&)>> class_method_builders_;
  std::vector<std::function<bool(X3PackageHost*, X3Value)>> class_attr_builders_;
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
    static bool xlang3_package_build_called = false; \
    if (xlang3_package_build_called) { \
      return; \
    } \
    xlang3_package_build_called = true;

#define END_PACKAGE \
  } \
  X3PackageHost* __xlang3_host_ = nullptr; \
  X::Package<__xlang3_package_class>* __xlang3_package_ = nullptr; \
  X3PackageHost* Host() const { \
    return __xlang3_package_ == nullptr ? __xlang3_host_ : __xlang3_package_->host(); \
  } \
  X::Value GetEvent(const char* name) const { \
    return __xlang3_package_ == nullptr ? X::Value() : __xlang3_package_->GetValue(name); \
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
