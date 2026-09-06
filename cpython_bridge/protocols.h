#pragma once
#include "engine.h"
namespace x3py {
PyObject* get_iterator(PyObject*);
PyObject* next_item(PyObject*);
int set_item(PyObject*, PyObject*, PyObject*);
int truth(PyObject*);
PyObject* string_repr(PyObject*);
PyObject* string_value(PyObject*);
Py_hash_t hash_value(PyObject*);
PyObject* compare_values(PyObject*, PyObject*, int);
}
