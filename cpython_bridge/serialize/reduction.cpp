#include "reduction.h"

namespace x3py::graph {
namespace {
Ref registered_reducer(PyObject* object) {
  Ref module(PyImport_ImportModule("copyreg"));
  auto table = attr(module.p, "dispatch_table");
  auto* value = PyDict_GetItemWithError(table.p, reinterpret_cast<PyObject*>(Py_TYPE(object)));
  if (!value && PyErr_Occurred()) throw PythonError{};
  Ref result;
  if (value) result = Ref(Py_NewRef(value));
  return result;
}
Ref consume(PyObject* iterator, bool pairs) {
  if (iterator == Py_None) return Ref(Py_NewRef(Py_None));
  if (!PyIter_Check(iterator)) {
    PyErr_SetString(PyExc_TypeError, "reducer items must be iterators"); throw PythonError{};
  }
  Ref items(PyList_New(0));
  for (uint64_t count = 0;; ++count) {
    Ref item;
    item.p = PyIter_Next(iterator);
    if (!item.p) { if (PyErr_Occurred()) throw PythonError{}; break; }
    if (count >= max_refs / (pairs ? 2 : 1)) invalid("reducer items exceed snapshot reference limit");
    if (pairs && (!PyTuple_Check(item.p) || PyTuple_GET_SIZE(item.p) != 2)) {
      PyErr_SetString(PyExc_TypeError, "reducer dictionary items must be key/value tuples"); throw PythonError{};
    }
    checked(PyList_Append(items.p, item.p));
  }
  return items;
}
}
bool has_registered_reduction(PyObject* object) { return registered_reducer(object).p != nullptr; }
bool has_custom_reduction(PyObject* object) {
  if (registered_reducer(object).p) return true;
  auto* type = reinterpret_cast<PyObject*>(Py_TYPE(object));
  auto* base = reinterpret_cast<PyObject*>(&PyBaseObject_Type);
  for (auto* name : {"__reduce_ex__", "__reduce__"}) {
    auto method = attr(type, name); auto standard = attr(base, name);
    if (method.p != standard.p) return true;
  }
  return false;
}
Ref reduction_recipe(PyObject* object) {
  auto reducer = registered_reducer(object);
  Ref recipe(reducer.p ? PyObject_CallOneArg(reducer.p, object)
                       : PyObject_CallMethod(object, "__reduce_ex__", "i", 4));
  if (PyUnicode_Check(recipe.p)) return recipe;
  if (!PyTuple_Check(recipe.p) || PyTuple_GET_SIZE(recipe.p) < 2 || PyTuple_GET_SIZE(recipe.p) > 6) {
    PyErr_SetString(PyExc_TypeError, "reducer must return a global name or a tuple of two to six items");
    throw PythonError{};
  }
  if (!PyCallable_Check(PyTuple_GET_ITEM(recipe.p, 0)) || !PyTuple_Check(PyTuple_GET_ITEM(recipe.p, 1))) {
    PyErr_SetString(PyExc_TypeError, "reducer requires a callable and tuple arguments"); throw PythonError{};
  }
  Ref result(PyTuple_New(6));
  for (Py_ssize_t i = 0; i < 6; ++i) {
    auto* value = i < PyTuple_GET_SIZE(recipe.p) ? PyTuple_GET_ITEM(recipe.p, i) : Py_None;
    if (i == 3 || i == 4) {
      auto items = consume(value, i == 4); PyTuple_SET_ITEM(result.p, i, items.release());
    } else {
      if (i == 5 && value != Py_None && !PyCallable_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "reducer state setter must be callable"); throw PythonError{};
      }
      PyTuple_SET_ITEM(result.p, i, Py_NewRef(value));
    }
  }
  return result;
}
}
