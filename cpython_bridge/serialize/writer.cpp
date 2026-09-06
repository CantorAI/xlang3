#include "common.h"
#include "reduction.h"
#include <marshal.h>
#include <unordered_set>
#include <deque>

namespace x3py::graph {
namespace {
struct Node {
  Ref object;
  Kind kind = Kind::None;
  Ref payload;
  std::vector<uint32_t> refs;
  std::unordered_set<PyObject*> global_names;
  bool globals_header = false;
};
class Writer {
  std::deque<Node> nodes;
  std::unordered_map<PyObject*, uint32_t> memo;
  uint64_t references = 0;
  Ref builtin_module;
  PyObject* builtin_dictionary() {
    if (!builtin_module.p) builtin_module = Ref(PyImport_ImportModule("builtins"));
    return PyModule_GetDict(builtin_module.p);
  }
  uint32_t add(PyObject* object) {
    if (!object) object = Py_None;
    auto found = memo.find(object);
    if (found != memo.end()) return found->second;
    if (nodes.size() == max_nodes) invalid("snapshot exceeds node limit");
    const auto id = static_cast<uint32_t>(nodes.size());
    nodes.emplace_back();
    nodes.back().object = Ref(Py_NewRef(object));
    memo.emplace(object, id);
    return id;
  }
  void link(Node& node, PyObject* object) {
    if (++references > max_refs) invalid("snapshot exceeds reference limit");
    node.refs.push_back(add(object));
  }
  void attribute(Node& node, const char* name) {
    auto value = attr(node.object.p, name); link(node, value.p);
  }
  void sequence(Node& node, PyObject* sequence) {
    Ref items(PySequence_Fast(sequence, "expected sequence"));
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(items.p); ++i)
      link(node, PySequence_Fast_GET_ITEM(items.p, i));
  }
  void mapping(Node& node, PyObject* mapping) {
    Ref items(PyDict_Items(mapping));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items.p); ++i) {
      auto* pair = PyList_GET_ITEM(items.p, i);
      link(node, PyTuple_GET_ITEM(pair, 0)); link(node, PyTuple_GET_ITEM(pair, 1));
    }
  }
  bool imported(Node& node) {
    Ref module, name;
    module.p = PyObject_GetAttrString(node.object.p, "__module__");
    if (!module.p) { PyErr_Clear(); return false; }
    name.p = PyObject_GetAttrString(node.object.p, "__qualname__");
    if (!name.p) { PyErr_Clear(); return false; }
    if (!PyUnicode_Check(module.p) || !PyUnicode_Check(name.p)) return false;
    const char* m = PyUnicode_AsUTF8(module.p);
    const char* q = PyUnicode_AsUTF8(name.p);
    if (!m || !q) throw PythonError{};
    if (!std::strcmp(m, "__main__") || std::strstr(q, "<locals>")) return false;
    Ref loaded; loaded.p = PyImport_ImportModule(m);
    if (!loaded.p) { PyErr_Clear(); return false; }
    Ref dot(PyUnicode_FromString("."));
    Ref parts(PyUnicode_Split(name.p, dot.p, -1));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(parts.p); ++i) {
      Ref next; next.p = PyObject_GetAttr(loaded.p, PyList_GET_ITEM(parts.p, i));
      if (!next.p) { PyErr_Clear(); return false; }
      loaded = std::move(next);
    }
    if (loaded.p != node.object.p) return false;
    node.kind = Kind::Imported; link(node, module.p); link(node, name.p); return true;
  }
  void capture_globals(Node& globals, PyObject* code) {
    if (!globals.globals_header) {
      globals.globals_header = true;
      for (auto* key : {"__name__", "__package__", "__file__", "__builtins__"}) {
        Ref name(PyUnicode_InternFromString(key));
        auto* value = PyDict_GetItemWithError(globals.object.p, name.p);
        if (value) { globals.global_names.insert(name.p); link(globals, name.p); link(globals, value); }
        else if (PyErr_Occurred()) throw PythonError{};
      }
    }
    std::vector<PyObject*> pending{code};
    std::unordered_set<PyObject*> visited;
    while (!pending.empty()) {
      auto* current = pending.back(); pending.pop_back();
      if (!visited.insert(current).second) continue;
      auto names = attr(current, "co_names");
      for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(names.p); ++i) {
        auto* name = PyTuple_GET_ITEM(names.p, i);
        auto* value = PyDict_GetItemWithError(globals.object.p, name);
        if (!value) { if (PyErr_Occurred()) throw PythonError{}; continue; }
        if (globals.global_names.insert(name).second) { link(globals, name); link(globals, value); }
      }
      auto constants = attr(current, "co_consts");
      for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants.p); ++i) {
        auto* value = PyTuple_GET_ITEM(constants.p, i);
        if (PyCode_Check(value)) pending.push_back(value);
      }
    }
  }
  void describe(Node& node) {
    auto* object = node.object.p;
    if (node.kind == Kind::Globals) return;
    if (object == Py_None) node.kind = Kind::None;
    else if (object == Py_False) node.kind = Kind::False;
    else if (object == Py_True) node.kind = Kind::True;
    else if (object == Py_Ellipsis) node.kind = Kind::Ellipsis;
    else if (object == Py_NotImplemented) node.kind = Kind::NotImplemented;
    else if (PyLong_CheckExact(object)) {
      node.kind = Kind::Int;
      auto size = PyLong_AsNativeBytes(object, nullptr, 0, Py_ASNATIVEBYTES_LITTLE_ENDIAN);
      if (size < 0) throw PythonError{};
      if (static_cast<uint64_t>(size) > max_bytes) invalid("integer exceeds snapshot limit");
      node.payload = Ref(PyBytes_FromStringAndSize(nullptr, size));
      if (PyLong_AsNativeBytes(object, PyBytes_AS_STRING(node.payload.p), size, Py_ASNATIVEBYTES_LITTLE_ENDIAN) < 0)
        throw PythonError{};
    }
    else if (PyFloat_CheckExact(object)) node.kind = Kind::Float;
    else if (PyComplex_CheckExact(object)) node.kind = Kind::Complex;
    else if (PyUnicode_CheckExact(object)) {
      node.kind = Kind::Text;
      Py_ssize_t length;
      if (!PyUnicode_AsUTF8AndSize(object, &length)) {
        if (!PyErr_ExceptionMatches(PyExc_UnicodeEncodeError)) throw PythonError{};
        PyErr_Clear();
        node.payload = Ref(PyUnicode_AsEncodedString(object, "utf-8", "surrogatepass"));
      }
    }
    else if (PyBytes_CheckExact(object)) node.kind = Kind::Bytes;
    else if (PyByteArray_CheckExact(object)) node.kind = Kind::ByteArray;
    else if (PyList_CheckExact(object)) { node.kind = Kind::List; sequence(node, object); }
    else if (PyTuple_CheckExact(object)) { node.kind = Kind::Tuple; sequence(node, object); }
    else if (PyDict_CheckExact(object)) {
      if (object == builtin_dictionary()) {
        node.kind = Kind::Imported;
        Ref module(PyUnicode_FromString("builtins")), name(PyUnicode_FromString("__dict__"));
        link(node, module.p); link(node, name.p);
      } else { node.kind = Kind::Dict; mapping(node, object); }
    }
    else if (PySet_CheckExact(object) || PyFrozenSet_CheckExact(object)) {
      node.kind = PySet_CheckExact(object) ? Kind::Set : Kind::FrozenSet;
      sequence(node, object);
    }
    else if (PyCell_Check(object)) {
      node.kind = Kind::Cell;
      Ref contents; contents.p = PyCell_Get(object);
      if (contents.p) link(node, contents.p);
      else if (PyErr_Occurred()) throw PythonError{};
    }
    else if (PyCode_Check(object)) {
      node.kind = Kind::Code; node.payload = Ref(PyMarshal_WriteObjectToString(object, Py_MARSHAL_VERSION));
    }
    else if (PyFunction_Check(object)) {
      if (imported(node)) return;
      node.kind = Kind::Function;
      auto builtins = attr(object, "__builtins__");
      auto* code = PyFunction_GetCode(object);
      auto* dictionary = PyFunction_GetGlobals(object);
      auto found = memo.find(dictionary);
      const bool fresh = found == memo.end();
      auto& globals = nodes[add(dictionary)];
      if (fresh) globals.kind = Kind::Globals;
      if (globals.kind == Kind::Globals) capture_globals(globals, code);
      link(node, code); link(node, dictionary);
      attribute(node, "__name__"); attribute(node, "__qualname__");
      link(node, PyFunction_GetDefaults(object)); link(node, PyFunction_GetKwDefaults(object));
      link(node, PyFunction_GetClosure(object)); attribute(node, "__dict__");
      attribute(node, "__annotations__"); attribute(node, "__module__");
      attribute(node, "__doc__"); attribute(node, "__type_params__");
      link(node, builtins.p);
    }
    else if (PyModule_Check(object)) { node.kind = Kind::Module; attribute(node, "__name__"); }
    else if (PyType_Check(object)) {
      if (has_registered_reduction(object)) {
        auto recipe = reduction_recipe(object);
        if (!PyTuple_Check(recipe.p)) {
          if (imported(node)) return;
          invalid("class reducer global must identify an importable class");
        }
        node.kind = Kind::Reduced;
        for (Py_ssize_t i = 0; i < 6; ++i) link(node, PyTuple_GET_ITEM(recipe.p, i));
        return;
      }
      if (imported(node)) return;
      if (!(reinterpret_cast<PyTypeObject*>(object)->tp_flags & Py_TPFLAGS_HEAPTYPE))
        unsupported(object);
      node.kind = Kind::Class;
      attribute(node, "__name__"); attribute(node, "__qualname__"); attribute(node, "__module__");
      attribute(node, "__bases__");
      auto* dictionary = reinterpret_cast<PyTypeObject*>(object)->tp_dict;
      auto* slots = PyDict_GetItemString(dictionary, "__slots__");
      link(node, slots);
      Ref members(PyDict_New());
      Py_ssize_t pos = 0; PyObject *key, *value;
      while (PyDict_Next(dictionary, &pos, &key, &value)) {
        const char* name = PyUnicode_AsUTF8(key); if (!name) throw PythonError{};
        if (!std::strcmp(name, "__dict__") || !std::strcmp(name, "__weakref__") ||
            !std::strcmp(name, "__slots__") || !std::strcmp(name, "__classcell__")) continue;
        if (Py_IS_TYPE(value, &PyMemberDescr_Type) || Py_IS_TYPE(value, &PyGetSetDescr_Type)) continue;
        checked(PyDict_SetItem(members.p, key, value));
      }
      link(node, members.p);
      link(node, reinterpret_cast<PyObject*>(Py_TYPE(object)));
    }
    else if (Py_IS_TYPE(object, &PyStaticMethod_Type) || Py_IS_TYPE(object, &PyClassMethod_Type)) {
      node.kind = Py_IS_TYPE(object, &PyStaticMethod_Type) ? Kind::StaticMethod : Kind::ClassMethod;
      attribute(node, "__func__");
    }
    else if (Py_IS_TYPE(object, &PyProperty_Type)) {
      node.kind = Kind::Property;
      attribute(node, "fget"); attribute(node, "fset"); attribute(node, "fdel"); attribute(node, "__doc__");
    }
    else if (PyMethod_Check(object)) {
      node.kind = Kind::Method; link(node, PyMethod_GET_FUNCTION(object)); link(node, PyMethod_GET_SELF(object));
    }
    else if (PyCFunction_Check(object) && imported(node)) {}
    else {
      auto* type = Py_TYPE(object);
      // Native storage cannot be reconstructed by assigning a Python __dict__.
      if (!python_instance_layout(type) || has_custom_reduction(object)) {
        auto recipe = reduction_recipe(object);
        if (PyUnicode_Check(recipe.p)) {
          auto module_name = attr(object, "__module__");
          if (!PyUnicode_Check(module_name.p) || PyUnicode_CompareWithASCIIString(module_name.p, "__main__") == 0) {
            PyErr_SetString(PyExc_TypeError, "reducer global must belong to an importable module"); throw PythonError{};
          }
          Ref module(PyImport_Import(module_name.p));
          Ref dot(PyUnicode_FromString(".")); Ref parts(PyUnicode_Split(recipe.p, dot.p, -1));
          Ref global(Py_NewRef(module.p));
          for (Py_ssize_t i = 0; i < PyList_GET_SIZE(parts.p); ++i)
            global = Ref(PyObject_GetAttr(global.p, PyList_GET_ITEM(parts.p, i)));
          if (global.p != object) invalid("reducer global name does not identify the original object");
          node.kind = Kind::Imported; link(node, module_name.p); link(node, recipe.p);
        } else {
          node.kind = Kind::Reduced;
          for (Py_ssize_t i = 0; i < 6; ++i) link(node, PyTuple_GET_ITEM(recipe.p, i));
        }
        return;
      }
      node.kind = Kind::Instance; link(node, reinterpret_cast<PyObject*>(type));
      Ref state(PyObject_CallMethod(object, "__getstate__", nullptr)); link(node, state.p);
    }
  }
public:
  void write(Output& out, PyObject* root) {
    add(root);
    for (size_t i = 0; i < nodes.size(); ++i) describe(nodes[i]);
    out.scalar<uint32_t>(version); out.scalar<uint32_t>(static_cast<uint32_t>(nodes.size()));
    out.scalar<uint32_t>(0);
    for (auto& node : nodes) {
      out.scalar<uint8_t>(static_cast<uint8_t>(node.kind));
      const void* data = nullptr; uint64_t size = 0; double values[2]{};
      if (node.payload.p) { data = PyBytes_AS_STRING(node.payload.p); size = PyBytes_GET_SIZE(node.payload.p); }
      else if (node.kind == Kind::Text) {
        Py_ssize_t length; data = PyUnicode_AsUTF8AndSize(node.object.p, &length);
        if (!data) throw PythonError{}; size = static_cast<uint64_t>(length);
      }
      else if (node.kind == Kind::Bytes) { data = PyBytes_AS_STRING(node.object.p); size = PyBytes_GET_SIZE(node.object.p); }
      else if (node.kind == Kind::ByteArray) { data = PyByteArray_AS_STRING(node.object.p); size = PyByteArray_GET_SIZE(node.object.p); }
      else if (node.kind == Kind::Float) { values[0] = little(PyFloat_AS_DOUBLE(node.object.p)); data = values; size = 8; }
      else if (node.kind == Kind::Complex) {
        auto v = PyComplex_AsCComplex(node.object.p); values[0] = little(v.real); values[1] = little(v.imag);
        data = values; size = 16;
      }
      out.scalar<uint64_t>(size); out.data(data, size);
      out.scalar<uint32_t>(static_cast<uint32_t>(node.refs.size()));
      if (little_endian()) out.data(node.refs.data(), node.refs.size() * sizeof(uint32_t));
      else for (auto id : node.refs) out.scalar<uint32_t>(id);
    }
  }
};
}
}
namespace x3py {
void write_python_graph(Engine& engine, X3Stream* stream, PyObject* root) {
  graph::Output output{engine, stream}; graph::Writer{}.write(output, root);
}
}
