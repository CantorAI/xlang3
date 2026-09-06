#include "protocols.h"
#include "xlang_object.h"

namespace x3py {
namespace {
PyObject* builtin(PyObject* object, const char* name) {
  return protect([&]() -> PyObject* {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    X3Value out = x3_value_invalid();
    engine->execute([&] { engine->check_protocol(
        x3_call_builtin(engine->runtime, name, &self->value, 1, &out), "TypeError"); });
    return to_python(engine, Py_TYPE(object), out);
  });
}
}

PyObject* get_iterator(PyObject* object) {
  return protect([&]() -> PyObject* {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    X3Value out = x3_value_invalid();
    engine->execute([&] { engine->check_protocol(
        x3_get_iter(engine->runtime, self->value, &out), "TypeError"); });
    return to_python(engine, Py_TYPE(object), out);
  });
}

PyObject* next_item(PyObject* object) {
  return protect([&]() -> PyObject* {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    X3Value out = x3_value_invalid();
    int32_t done = 0;
    engine->execute([&] { engine->check_protocol(
        x3_iter_next(engine->runtime, self->value, &out, &done), "TypeError"); });
    if (done) return nullptr;
    return to_python(engine, Py_TYPE(object), out);
  });
}

int set_item(PyObject* object, PyObject* key, PyObject* value) {
  try {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    OwnedValue index(engine, from_python(engine, Py_TYPE(object), key));
    OwnedValue item(engine, value ? from_python(engine, Py_TYPE(object), value) : x3_value_none());
    engine->execute([&] { engine->check_protocol(value
        ? x3_set_item(engine->runtime, self->value, index.value, item.value)
        : x3_delete_item(engine->runtime, self->value, index.value), "TypeError"); });
    return 0;
  } catch (...) { translate_exception(); return -1; }
}

int truth(PyObject* object) {
  auto* result = builtin(object, "bool");
  if (!result) return -1;
  int answer = PyObject_IsTrue(result);
  Py_DECREF(result);
  return answer;
}
PyObject* string_repr(PyObject* object) { return builtin(object, "repr"); }
PyObject* string_value(PyObject* object) { return builtin(object, "str"); }
Py_hash_t hash_value(PyObject* object) {
  auto* result = builtin(object, "hash");
  if (!result) return -1;
  auto hash = PyLong_AsSsize_t(result);
  Py_DECREF(result);
  return hash == -1 && !PyErr_Occurred() ? -2 : hash;
}
PyObject* compare_values(PyObject* left, PyObject* right, int op) {
  return protect([&]() -> PyObject* {
    auto* self = reinterpret_cast<XlangObject*>(left);
    auto engine = *self->engine;
    OwnedValue other(engine, from_python(engine, Py_TYPE(left), right));
    static const X3ValueCompareOp operations[] = {X3_VALUE_COMPARE_LT, X3_VALUE_COMPARE_LE,
        X3_VALUE_COMPARE_EQ, X3_VALUE_COMPARE_NE, X3_VALUE_COMPARE_GT, X3_VALUE_COMPARE_GE};
    int32_t result = 0;
    engine->execute([&] { engine->check_protocol(
        x3_value_compare_op(engine->runtime, operations[op], self->value, other.value, &result), "TypeError"); });
    return PyBool_FromLong(result);
  });
}
}
