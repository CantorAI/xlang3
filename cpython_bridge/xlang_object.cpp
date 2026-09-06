#include "xlang_object.h"
#include "buffer.h"
#include "protocols.h"
#include <vector>
#include <string>
#include <limits>

namespace x3py {
namespace {
const char* python_name(PyObject* name) {
  Py_ssize_t size = 0;
  const char* text = PyUnicode_AsUTF8AndSize(name, &size);
  if (!text) throw PythonError{};
  if (std::string_view(text, static_cast<size_t>(size)).find('\0') != std::string_view::npos) {
    PyErr_SetString(PyExc_ValueError, "XLang3 attribute and keyword names cannot contain NUL");
    throw PythonError{};
  }
  return text;
}

void dealloc(PyObject* object) {
  PyObject_GC_UnTrack(object);
  auto* self = reinterpret_cast<XlangObject*>(object);
  if (self->engine) {
    auto engine = *self->engine;
    if (self->value.tag == X3_TAG_OBJECT) engine->proxies.erase(self->value.as.obj);
    engine->run([&] { x3_value_release(self->value); });
    delete self->engine;
  }
  Py_XDECREF(self->owner);
  auto* type = Py_TYPE(object);
  type->tp_free(object);
  Py_DECREF(type);
}

int traverse_proxy(PyObject* object, visitproc visit, void* arg) {
  auto* self = reinterpret_cast<XlangObject*>(object);
  Py_VISIT(Py_TYPE(object));
  Py_VISIT(self->owner);
  return 0;
}

int clear_proxy(PyObject* object) {
  auto* self = reinterpret_cast<XlangObject*>(object);
  if (self->engine) {
    auto engine = *self->engine;
    auto value = self->value;
    self->value = x3_value_invalid();
    if (value.tag == X3_TAG_OBJECT) engine->proxies.erase(value.as.obj);
    engine->run([&] { x3_value_release(value); });
  }
  Py_CLEAR(self->owner);
  return 0;
}

PyObject* get_attr(PyObject* object, PyObject* name) {
  return protect([&]() -> PyObject* {
    auto* self = reinterpret_cast<XlangObject*>(object);
    const char* text = python_name(name);
    // CPython introspection belongs to the proxy type; other attributes are live.
    if (std::string(text) == "__class__") return PyObject_GenericGetAttr(object, name);
    auto engine = *self->engine;
    X3Value result = x3_value_invalid();
    engine->execute([&] { engine->check_protocol(x3_get_attr(engine->runtime, self->value, text, &result), "AttributeError"); });
    return to_python(engine, Py_TYPE(object), result);
  });
}

int set_attr(PyObject* object, PyObject* name, PyObject* value) {
  auto* result = protect([&]() -> PyObject* {
    const char* text = python_name(name);
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    OwnedValue converted(engine, value ? from_python(engine, Py_TYPE(object), value) : x3_value_none());
    engine->execute([&] { engine->check_protocol(value
        ? x3_set_attr(engine->runtime, self->value, text, converted.value)
        : x3_delete_attr(engine->runtime, self->value, text), "AttributeError"); });
    return Py_NewRef(Py_None);
  });
  if (!result) return -1;
  Py_DECREF(result);
  return 0;
}

struct Arguments {
  std::shared_ptr<Engine> engine;
  std::vector<X3Value> positional;
  std::vector<X3KeywordArg> keywords;
  ~Arguments() {
    engine->run([&] {
      for (auto value : positional) x3_value_release(value);
      for (auto& keyword : keywords) x3_value_release(keyword.value);
    });
  }
};

PyObject* call(PyObject* object, PyObject* args, PyObject* kwargs) {
  return protect([&]() -> PyObject* {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    auto count = PyTuple_GET_SIZE(args);
    auto decref = [](PyObject* value) { Py_XDECREF(value); };
    std::unique_ptr<PyObject, decltype(decref)> keyword_snapshot(
        kwargs ? PyDict_Copy(kwargs) : nullptr, decref);
    if (kwargs && !keyword_snapshot) throw PythonError{};
    auto* stable_kwargs = keyword_snapshot.get();
    auto kwcount = stable_kwargs ? PyDict_Size(stable_kwargs) : 0;
    if (count > UINT32_MAX || kwcount > UINT32_MAX) {
      PyErr_SetString(PyExc_OverflowError, "too many call arguments");
      return nullptr;
    }
    Arguments converted{engine};
    converted.positional.reserve(static_cast<size_t>(count));
    converted.keywords.reserve(static_cast<size_t>(kwcount));
    for (Py_ssize_t i = 0; i < count; ++i)
      converted.positional.push_back(from_python(engine, Py_TYPE(object), PyTuple_GET_ITEM(args, i)));
    Py_ssize_t pos = 0;
    PyObject* key;
    PyObject* item;
    while (stable_kwargs && PyDict_Next(stable_kwargs, &pos, &key, &item)) {
      const char* name = python_name(key);
      converted.keywords.push_back({name, from_python(engine, Py_TYPE(object), item)});
    }
    X3Value result = x3_value_invalid();
    engine->execute([&] {
      engine->check(x3_call_kw(engine->runtime, self->value, converted.positional.data(),
          static_cast<uint32_t>(count), converted.keywords.data(), static_cast<uint32_t>(kwcount), &result));
    });
    return to_python(engine, Py_TYPE(object), result);
  });
}

PyObject* get_item(PyObject* object, PyObject* key) {
  return protect([&]() -> PyObject* {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    OwnedValue converted(engine, from_python(engine, Py_TYPE(object), key));
    X3Value result = x3_value_invalid();
    engine->execute([&] { engine->check_protocol(x3_get_item(engine->runtime, self->value, converted.value, &result), "TypeError"); });
    return to_python(engine, Py_TYPE(object), result);
  });
}

Py_ssize_t length(PyObject* object) {
  try {
    auto* self = reinterpret_cast<XlangObject*>(object);
    auto engine = *self->engine;
    uint64_t result = 0;
    engine->execute([&] { engine->check_protocol(x3_len(engine->runtime, self->value, &result), "TypeError"); });
    if (result > PY_SSIZE_T_MAX) throw std::overflow_error("length exceeds CPython limits");
    return static_cast<Py_ssize_t>(result);
  } catch (...) { translate_exception(); return -1; }
}

PyObject* repr(PyObject* object) {
  return PyUnicode_FromFormat("<xlang3.Object at %p>", object);
}
}

PyObject* create_proxy_type(PyObject* module) {
  static PyType_Slot slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(dealloc)},
    {Py_tp_traverse, reinterpret_cast<void*>(traverse_proxy)},
    {Py_tp_clear, reinterpret_cast<void*>(clear_proxy)},
    {Py_bf_getbuffer, reinterpret_cast<void*>(get_buffer)},
    {Py_bf_releasebuffer, reinterpret_cast<void*>(release_buffer)},
    {Py_tp_getattro, reinterpret_cast<void*>(get_attr)},
    {Py_tp_setattro, reinterpret_cast<void*>(set_attr)},
    {Py_tp_call, reinterpret_cast<void*>(call)},
    {Py_tp_repr, reinterpret_cast<void*>(string_repr)},
    {Py_tp_str, reinterpret_cast<void*>(string_value)},
    {Py_tp_hash, reinterpret_cast<void*>(hash_value)},
    {Py_tp_richcompare, reinterpret_cast<void*>(compare_values)},
    {Py_tp_iter, reinterpret_cast<void*>(get_iterator)},
    {Py_tp_iternext, reinterpret_cast<void*>(next_item)},
    {Py_nb_bool, reinterpret_cast<void*>(truth)},
    {Py_mp_subscript, reinterpret_cast<void*>(get_item)},
    {Py_mp_ass_subscript, reinterpret_cast<void*>(set_item)},
    {Py_mp_length, reinterpret_cast<void*>(length)},
    {0, nullptr}
  };
  static PyType_Spec spec = {"xlang3.Object", sizeof(XlangObject), 0,
      Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_DISALLOW_INSTANTIATION, slots};
  return PyType_FromModuleAndSpec(module, &spec, nullptr);
}
}
