#include "xlang_object.h"
#include "python_object.h"
#include <limits>

namespace x3py {
X3Value from_python(const std::shared_ptr<Engine>& engine, PyTypeObject* type, PyObject* object) {
  engine->ensure_open();
  if (PyObject_TypeCheck(object, type)) {
    auto* proxy = reinterpret_cast<XlangObject*>(object);
    if (*proxy->engine != engine) {
      PyErr_SetString(PyExc_ValueError, "cannot mix XLang3 runtime contexts");
      throw PythonError{};
    }
    engine->run([&] { x3_value_retain(proxy->value); });
    return proxy->value;
  }
  if (object == Py_None) return x3_value_none();
  if (PyBool_Check(object)) return x3_value_bool(object == Py_True);
  if (PyLong_Check(object)) {
    auto value = PyLong_AsLongLong(object);
    if (PyErr_Occurred()) throw PythonError{};
    return x3_value_int64(value);
  }
  if (PyFloat_Check(object)) return x3_value_double(PyFloat_AS_DOUBLE(object));
  if (PySlice_Check(object)) {
    auto* slice = reinterpret_cast<PySliceObject*>(object);
    OwnedValue start(engine, from_python(engine, type, slice->start));
    OwnedValue stop(engine, from_python(engine, type, slice->stop));
    OwnedValue step(engine, from_python(engine, type, slice->step));
    const X3Value args[] = {start.value, stop.value, step.value};
    X3Value result = x3_value_invalid();
    engine->execute([&] { engine->check_protocol(x3_call_builtin(engine->runtime, "slice", args, 3, &result), "TypeError"); });
    return result;
  }
  X3Value value = x3_value_invalid();
  if (PyUnicode_Check(object)) {
    Py_ssize_t size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(object, &size);
    if (!data) throw PythonError{};
    value = engine->run([&] { return x3_value_string_utf8(engine->runtime, data, size); });
  } else if (PyBytes_Check(object)) {
    const char* data = PyBytes_AS_STRING(object);
    auto size = PyBytes_GET_SIZE(object);
    value = engine->run([&] { return x3_value_bytes(engine->runtime, data, size); });
  } else {
    return wrap_python(engine, object);
  }
  if (value.tag == X3_TAG_INVALID) throw std::runtime_error("cannot allocate XLang3 argument");
  return value;
}

PyObject* to_python(const std::shared_ptr<Engine>& engine, PyTypeObject* type, X3Value value) {
  OwnedValue owner(engine, value);
  engine->ensure_open();
  switch (value.tag) {
    case X3_TAG_NONE: return Py_NewRef(Py_None);
    case X3_TAG_BOOL: return PyBool_FromLong(value.as.b);
    case X3_TAG_INT64: return PyLong_FromLongLong(value.as.i64);
    case X3_TAG_UINT64: return PyLong_FromUnsignedLongLong(value.as.u64);
    case X3_TAG_DOUBLE: return PyFloat_FromDouble(value.as.f64);
    case X3_TAG_OBJECT: break;
    default: throw std::runtime_error("XLang3 returned an invalid value");
  }
  auto kind = engine->run([&] { return x3_value_object_kind(value); });
  if (kind == X3_OBJECT_KIND_INSTANCE) {
    if (auto* original = unwrap_python(engine, value)) return original;
    if (PyErr_Occurred()) throw PythonError{};
  }
  if (kind == X3_OBJECT_KIND_STRING) {
    const char* data = nullptr;
    uint64_t size = 0;
    engine->run([&] { engine->check(x3_value_string_data(engine->runtime, value, &data, &size)); });
    if (size > PY_SSIZE_T_MAX) throw std::overflow_error("string is too large for CPython");
    return PyUnicode_DecodeUTF8(data, static_cast<Py_ssize_t>(size), "strict");
  }
  if (kind == X3_OBJECT_KIND_BYTES) {
    const void* data = nullptr;
    uint64_t size = 0;
    engine->run([&] { engine->check(x3_value_bytes_data(engine->runtime, value, &data, &size)); });
    if (size > PY_SSIZE_T_MAX) throw std::overflow_error("bytes are too large for CPython");
    return PyBytes_FromStringAndSize(static_cast<const char*>(data), static_cast<Py_ssize_t>(size));
  }
  auto found = engine->proxies.find(value.as.obj);
  if (found != engine->proxies.end()) return Py_NewRef(found->second);
  auto* proxy = reinterpret_cast<XlangObject*>(type->tp_alloc(type, 0));
  if (!proxy) return nullptr;
  proxy->value = x3_value_invalid();
  try {
    proxy->engine = new std::shared_ptr<Engine>(engine);
    proxy->owner = Py_NewRef(engine->owner);
    engine->proxies.emplace(value.as.obj, reinterpret_cast<PyObject*>(proxy));
    proxy->value = owner.detach();
  } catch (...) { Py_DECREF(proxy); throw; }
  return reinterpret_cast<PyObject*>(proxy);
}
}
