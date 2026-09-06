#include "python_object.h"
#include "xlang_object.h"
#include <string>
#include <vector>

namespace x3py {
namespace {
constexpr const char* payload_type = "xlang3.cpython.Object";
struct PyRef {
  PyObject* value;
  explicit PyRef(PyObject* p = nullptr) : value(p) {}
  ~PyRef() { Py_XDECREF(value); }
  PyRef(const PyRef&) = delete;
  PyObject* release() { auto* p = value; value = nullptr; return p; }
};

void destroy_payload(void* raw) {
  auto* payload = static_cast<PythonPayload*>(raw);
  auto* engine = payload->engine;
  PyObject* object;
  {
    std::lock_guard<std::mutex> lock(engine->foreign_mutex);
    object = payload->object;
    if (object) engine->foreign.erase(object);
    payload->object = nullptr;
  }
  if (object) {
    EnterPython python(engine);
    Py_DECREF(object);
  }
  delete payload;
}

PyObject* argument(const std::shared_ptr<Engine>& engine, X3Value value) {
  engine->run([&] { x3_value_retain(value); });
  auto* result = to_python(engine, engine->proxy_type, value);
  if (!result) throw PythonError{};
  return result;
}

enum class Operation { GetAttr, SetAttr, DelAttr, Call, GetItem, SetItem, DelItem, Len, Iter, Next, Bool };

PyObject* apply(Operation operation, const std::shared_ptr<Engine>& engine,
    PyObject* object, const X3Value* args, uint32_t argc,
    const X3KeywordArg* kwargs, uint32_t kwargc) {
  if (operation == Operation::Call) {
    PyRef positional(PyTuple_New(argc));
    PyRef keywords(kwargc ? PyDict_New() : nullptr);
    if (!positional.value || (kwargc && !keywords.value)) throw PythonError{};
    for (uint32_t i = 0; i < argc; ++i)
      PyTuple_SET_ITEM(positional.value, i, argument(engine, args[i]));
    for (uint32_t i = 0; i < kwargc; ++i) {
      PyRef value(argument(engine, kwargs[i].value));
      if (PyDict_SetItemString(keywords.value, kwargs[i].name, value.value) < 0) throw PythonError{};
    }
    return PyObject_Call(object, positional.value, keywords.value);
  }
  if (kwargc) { PyErr_SetString(PyExc_TypeError, "unexpected protocol keyword arguments"); return nullptr; }
  const uint32_t expected = operation == Operation::SetAttr || operation == Operation::SetItem ? 2 :
      operation == Operation::GetAttr || operation == Operation::DelAttr ||
      operation == Operation::GetItem || operation == Operation::DelItem ? 1 : 0;
  if (argc != expected) { PyErr_SetString(PyExc_TypeError, "invalid protocol argument count"); return nullptr; }
  PyRef key(argc ? argument(engine, args[0]) : nullptr);
  PyRef value(argc > 1 ? argument(engine, args[1]) : nullptr);
  int status = 0;
  switch (operation) {
    case Operation::GetAttr: return PyObject_GetAttr(object, key.value);
    case Operation::SetAttr: status = PyObject_SetAttr(object, key.value, value.value); break;
    case Operation::DelAttr: status = PyObject_DelAttr(object, key.value); break;
    case Operation::GetItem: return PyObject_GetItem(object, key.value);
    case Operation::SetItem: status = PyObject_SetItem(object, key.value, value.value); break;
    case Operation::DelItem: status = PyObject_DelItem(object, key.value); break;
    case Operation::Len: {
      auto size = PyObject_Length(object);
      return size < 0 ? nullptr : PyLong_FromSsize_t(size);
    }
    case Operation::Iter: return PyObject_GetIter(object);
    case Operation::Next: {
      auto* result = PyIter_Next(object);
      if (!result && !PyErr_Occurred()) PyErr_SetNone(PyExc_StopIteration);
      return result;
    }
    case Operation::Bool: {
      int truth = PyObject_IsTrue(object);
      return truth < 0 ? nullptr : PyBool_FromLong(truth);
    }
    default: break;
  }
  return status < 0 ? nullptr : Py_NewRef(Py_None);
}

X3Status forward_error(Engine* engine, X3CallContext* context) {
  PyRef exception(PyErr_GetRaisedException());
  const char* name = "RuntimeError";
  struct Kind { PyObject* type; const char* name; };
  const Kind kinds[] = {
    {PyExc_StopIteration, "StopIteration"}, {PyExc_AttributeError, "AttributeError"},
    {PyExc_KeyError, "KeyError"}, {PyExc_IndexError, "IndexError"},
    {PyExc_TypeError, "TypeError"}, {PyExc_ValueError, "ValueError"},
    {PyExc_OverflowError, "OverflowError"}, {PyExc_ZeroDivisionError, "ZeroDivisionError"},
    {PyExc_MemoryError, "MemoryError"}, {PyExc_OSError, "OSError"},
    {PyExc_KeyboardInterrupt, "KeyboardInterrupt"}, {PyExc_SystemExit, "SystemExit"}
  };
  for (const auto& kind : kinds) {
    if (exception.value && PyErr_GivenExceptionMatches(exception.value, kind.type)) { name = kind.name; break; }
  }
  PyRef text(exception.value ? PyObject_Str(exception.value) : nullptr);
  const char* utf8 = text.value ? PyUnicode_AsUTF8(text.value) : nullptr;
  std::string message = utf8 ? utf8 : "CPython operation failed";
  PyErr_Clear();
  engine->run([&] { engine->python_host->raise_class_error(context, name, message.c_str()); });
  return X3_STATUS_ERROR;
}

template<Operation operation>
X3Status dispatch(X3CallContext* context, X3Runtime*, void* user,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result) {
  auto* raw_engine = static_cast<Engine*>(user);
  EnterPython python(raw_engine);
  try {
    if (raw_engine->closing || !argc) throw std::runtime_error("CPython bridge is closed");
    auto engine = raw_engine->shared_from_this();
    auto* payload = engine->run([&] { return static_cast<PythonPayload*>(
        x3_instance_get_native_data(args[0], payload_type)); });
    if (!payload || !payload->object) throw std::runtime_error("CPython object is no longer available");
    PyRef object(Py_NewRef(payload->object));
    PyRef returned(apply(operation, engine, object.value, args + 1, argc - 1, kwargs, kwargc));
    if (!returned.value) return forward_error(raw_engine, context);
    *result = from_python(engine, engine->proxy_type, returned.value);
    // The native dispatcher retains results aliasing positional arguments.
    for (uint32_t i = 0; i < argc; ++i) {
      if (result->tag == X3_TAG_OBJECT && args[i].tag == X3_TAG_OBJECT && result->as.obj == args[i].as.obj) {
        engine->run([&] { x3_value_release(*result); });
        break;
      }
    }
    return X3_STATUS_OK;
  } catch (...) {
    translate_exception();
    return forward_error(raw_engine, context);
  }
}

struct EngineOwner {
  PyObject_HEAD
  std::shared_ptr<Engine>* engine;
  PyObject* proxy_type;
};
int owner_traverse(PyObject* self, visitproc visit, void* arg) {
  auto* owner = reinterpret_cast<EngineOwner*>(self);
  Py_VISIT(Py_TYPE(self));
  Py_VISIT(owner->proxy_type);
  if (owner->engine) {
    auto& engine = **owner->engine;
    for (const auto& entry : engine.buffers) Py_VISIT(entry.second);
    std::lock_guard<std::mutex> lock(engine.foreign_mutex);
    for (const auto& entry : engine.foreign) Py_VISIT(entry.second->object);
  }
  return 0;
}
int owner_clear(PyObject* self) {
  auto* owner = reinterpret_cast<EngineOwner*>(self);
  if (owner->engine) {
    auto& engine = **owner->engine;
    engine.closing = true;
    std::vector<PyObject*> objects;
    try {
      std::lock_guard<std::mutex> lock(engine.foreign_mutex);
      objects.reserve(engine.foreign.size());
      for (auto& entry : engine.foreign) {
        objects.push_back(entry.second->object);
        entry.second->object = nullptr;
      }
      engine.foreign.clear();
    } catch (...) { translate_exception(); return -1; }
    for (auto* object : objects) Py_XDECREF(object);
  }
  Py_CLEAR(owner->proxy_type);
  return 0;
}
void owner_dealloc(PyObject* self) {
  PyObject_GC_UnTrack(self);
  auto* owner = reinterpret_cast<EngineOwner*>(self);
  if (owner_clear(self) < 0) PyErr_WriteUnraisable(self);
  delete owner->engine;
  auto* type = Py_TYPE(self);
  type->tp_free(self);
  Py_DECREF(type);
}
}

PyObject* create_engine_owner(const std::shared_ptr<Engine>& engine) {
  PyType_Slot slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(owner_dealloc)},
    {Py_tp_traverse, reinterpret_cast<void*>(owner_traverse)},
    {Py_tp_clear, reinterpret_cast<void*>(owner_clear)}, {0, nullptr}
  };
  PyType_Spec spec = {"xlang3._EngineOwner", sizeof(EngineOwner), 0,
      Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_DISALLOW_INSTANTIATION, slots};
  PyRef type(PyType_FromSpec(&spec));
  if (!type.value) return nullptr;
  auto* owner = reinterpret_cast<EngineOwner*>(reinterpret_cast<PyTypeObject*>(type.value)->tp_alloc(
      reinterpret_cast<PyTypeObject*>(type.value), 0));
  if (!owner) return nullptr;
  try {
    owner->engine = new std::shared_ptr<Engine>(engine);
    owner->proxy_type = Py_NewRef(reinterpret_cast<PyObject*>(engine->proxy_type));
  } catch (...) { Py_DECREF(owner); throw; }
  return reinterpret_cast<PyObject*>(owner);
}

void close_borrowed_engine(const std::shared_ptr<Engine>& engine) {
  engine->closing = true;
  // Python modules can retain callbacks after their XLang3 runtime goes away.
  // Invalidate these proxies while the runtime and native package hosts exist.
  while (!engine->proxies.empty()) {
    auto it = engine->proxies.begin();
    PyRef proxy(Py_NewRef(it->second));
    auto* object = reinterpret_cast<XlangObject*>(proxy.value);
    auto value = object->value;
    object->value = x3_value_invalid();
    engine->proxies.erase(it);
    engine->run([&] { x3_value_release(value); });
  }
  if (engine->owner && owner_clear(engine->owner) < 0)
    PyErr_WriteUnraisable(engine->owner);
  auto klass = engine->python_class;
  engine->python_class = x3_value_invalid();
  engine->run([&] { x3_value_release(klass); });
}

void initialize_python_class(const std::shared_ptr<Engine>& engine, const char* package_name) {
  X3Value module = x3_value_invalid();
  engine->run([&] {
    engine->check(x3_runtime_register_package(engine->runtime, package_name,
        [](void* host_ptr, X3Value, void* context) -> X3Status {
          auto* engine = static_cast<Engine*>(context);
          engine->python_host = static_cast<X3PackageHost*>(host_ptr);
          struct Method { const char* name; X3NativeKeywordFn fn; };
          const Method methods[] = {
            {"__getattribute__", dispatch<Operation::GetAttr>},
            {"__setattr__", dispatch<Operation::SetAttr>},
            {"__delattr__", dispatch<Operation::DelAttr>},
            {"__call__", dispatch<Operation::Call>},
            {"__getitem__", dispatch<Operation::GetItem>},
            {"__setitem__", dispatch<Operation::SetItem>},
            {"__delitem__", dispatch<Operation::DelItem>},
            {"__len__", dispatch<Operation::Len>},
            {"__iter__", dispatch<Operation::Iter>},
            {"__next__", dispatch<Operation::Next>},
            {"__bool__", dispatch<Operation::Bool>}
          };
          std::vector<X3NativeFunctionDef> definitions;
          for (const auto& method : methods) {
            X3NativeFunctionDef def{};
            def.size = sizeof(def);
            def.name = method.name;
            def.user_data = engine;
            def.min_argc = 1;
            def.max_argc = UINT32_MAX;
            def.keyword_callback = method.fn;
            definitions.push_back(def);
          }
          return engine->python_host->create_class(engine->python_host, "CPythonObject",
              definitions.data(), static_cast<uint32_t>(definitions.size()), &engine->python_class);
        }, engine.get(), &module));
    x3_value_release(module);
  });
}

X3Value wrap_python(const std::shared_ptr<Engine>& engine, PyObject* object) {
  if (engine->closing) throw std::runtime_error("CPython bridge is closed");
  PyRef owner(Py_NewRef(object));
  return engine->run([&] {
    {
      std::lock_guard<std::mutex> lock(engine->foreign_mutex);
      auto found = engine->foreign.find(object);
      if (found != engine->foreign.end()) {
        x3_value_retain(found->second->instance);
        return found->second->instance;
      }
    }
    auto instance = x3_value_instance(engine->runtime, engine->python_class);
    if (instance.tag == X3_TAG_INVALID) throw std::runtime_error("cannot allocate CPython proxy");
    auto payload = std::make_unique<PythonPayload>(PythonPayload{engine.get(), object, instance});
    try {
      engine->check(x3_instance_set_native_data(instance, payload_type, payload.get(), destroy_payload));
    } catch (...) { x3_value_release(instance); throw; }
    auto* attached = payload.release();
    owner.release();
    try {
      std::lock_guard<std::mutex> lock(engine->foreign_mutex);
      engine->foreign.emplace(object, attached);
    } catch (...) { x3_value_release(instance); throw; }
    return instance;
  });
}

PyObject* unwrap_python(const std::shared_ptr<Engine>& engine, X3Value value) {
  auto* payload = engine->run([&] { return static_cast<PythonPayload*>(
      x3_instance_get_native_data(value, payload_type)); });
  if (!payload) return nullptr;
  if (!payload->object || engine->closing) {
    PyErr_SetString(PyExc_RuntimeError, "CPython bridge is closed");
    return nullptr;
  }
  return Py_NewRef(payload->object);
}
}
