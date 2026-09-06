#include "common.h"
#include <marshal.h>
#include <unordered_set>

namespace x3py::graph {
namespace {
enum class Phase : uint8_t { Unseen, Resolving, Ready };
struct Node {
  Kind kind;
  Ref object;
  std::vector<uint32_t> refs;
  uint8_t building = 0;
  Phase phase = Phase::Unseen;
  bool linked = false;
  bool dependencies_ready = false;
  bool class_hooks_run = false;
};
class Reader {
  std::vector<Node> nodes;
  unsigned depth = 0;
  PyObject* ref(Node& node, size_t index) { return ensure(node.refs.at(index)); }
  void expect(bool condition) { if (!condition) invalid("invalid CPython graph record"); }
  void arity(Node& node, size_t size) { expect(node.refs.size() == size); }
  void immutable_dependencies(uint32_t id, std::vector<uint8_t>& marks, unsigned level = 0) {
    const auto& node = nodes[id];
    if (node.kind != Kind::Tuple && node.kind != Kind::FrozenSet) return;
    if (marks[id] == 2) return;
    expect(marks[id] != 1 && level < 512);
    marks[id] = 1;
    for (auto child : node.refs) immutable_dependencies(child, marks, level + 1);
    marks[id] = 2;
  }
  void validate(Node& node) {
    switch (node.kind) {
      case Kind::List: case Kind::Tuple: case Kind::Set: case Kind::FrozenSet: break;
      case Kind::Dict: case Kind::Globals: expect(node.refs.size() % 2 == 0); break;
      case Kind::Cell: expect(node.refs.size() <= 1); break;
      case Kind::Function: expect(node.refs.size() == 12 || node.refs.size() == 13); break;
      case Kind::Class: expect(node.refs.size() == 6 || node.refs.size() == 7); break;
      case Kind::Reduced: arity(node, 6); break;
      case Kind::Imported: case Kind::Instance: case Kind::Method: arity(node, 2); break;
      case Kind::Property: arity(node, 4); break;
      case Kind::Module: case Kind::StaticMethod: case Kind::ClassMethod: arity(node, 1); break;
      default: arity(node, 0); break;
    }
  }
  void parse(Input& in) {
    if (in.remaining > max_bytes) invalid("snapshot exceeds byte limit");
    expect(in.scalar<uint32_t>() == version);
    const auto count = in.scalar<uint32_t>();
    expect(count && count <= max_nodes && count <= in.remaining / 13);
    expect(in.scalar<uint32_t>() == 0);
    nodes.resize(count);
    uint64_t refs = 0;
    for (auto& node : nodes) {
      auto tag = in.scalar<uint8_t>();
      expect(tag <= static_cast<uint8_t>(Kind::Reduced)); node.kind = static_cast<Kind>(tag);
      auto size = in.scalar<uint64_t>(); expect(size <= in.remaining && size <= PY_SSIZE_T_MAX);
      if (node.kind == Kind::Bytes || node.kind == Kind::ByteArray) {
        const bool bytes = node.kind == Kind::Bytes;
        node.object = Ref(bytes ? PyBytes_FromStringAndSize(nullptr, static_cast<Py_ssize_t>(size))
                                : PyByteArray_FromStringAndSize(nullptr, static_cast<Py_ssize_t>(size)));
        in.data(bytes ? PyBytes_AS_STRING(node.object.p) : PyByteArray_AS_STRING(node.object.p), size);
      } else if (node.kind == Kind::Text || node.kind == Kind::Int || node.kind == Kind::Code) {
        Ref data(PyBytes_FromStringAndSize(nullptr, static_cast<Py_ssize_t>(size)));
        in.data(PyBytes_AS_STRING(data.p), size);
        if (node.kind == Kind::Text)
          node.object = Ref(PyUnicode_DecodeUTF8(PyBytes_AS_STRING(data.p), static_cast<Py_ssize_t>(size), "surrogatepass"));
        else if (node.kind == Kind::Int) {
          expect(size != 0);
          node.object = Ref(PyLong_FromNativeBytes(PyBytes_AS_STRING(data.p), static_cast<size_t>(size), Py_ASNATIVEBYTES_LITTLE_ENDIAN));
        } else node.object = std::move(data); // Marshal only after every record has been validated.
      } else if (node.kind == Kind::Float || node.kind == Kind::Complex) {
        expect(size == (node.kind == Kind::Float ? 8 : 16));
        double real = in.scalar<double>();
        node.object = Ref(node.kind == Kind::Float ? PyFloat_FromDouble(real) : PyComplex_FromDoubles(real, in.scalar<double>()));
      } else expect(size == 0);
      const auto n = in.scalar<uint32_t>();
      refs += n; expect(refs <= max_refs && n <= in.remaining / sizeof(uint32_t));
      node.refs.resize(n);
      in.data(node.refs.data(), node.refs.size() * sizeof(uint32_t));
      for (auto& id : node.refs) { id = little(id); expect(id < count); }
      validate(node);
    }
    expect(in.remaining == 0);
    std::vector<uint8_t> marks(count);
    for (uint32_t i = 0; i < count; ++i) {
      immutable_dependencies(i, marks);
    }
    for (auto& node : nodes) {
      auto kind = [&](size_t i) { return nodes[node.refs[i]].kind; };
      if (node.kind == Kind::Function) {
        expect(kind(0) == Kind::Code && (kind(1) == Kind::Globals || kind(1) == Kind::Dict));
        expect(kind(2) == Kind::Text && kind(3) == Kind::Text);
        expect(kind(4) == Kind::None || kind(4) == Kind::Tuple);
        expect(kind(5) == Kind::None || kind(5) == Kind::Dict);
        expect(kind(6) == Kind::None || kind(6) == Kind::Tuple);
        if (kind(6) == Kind::Tuple)
          for (auto id : nodes[node.refs[6]].refs) expect(nodes[id].kind == Kind::Cell);
        expect(kind(7) == Kind::Dict && kind(8) == Kind::Dict && kind(11) == Kind::Tuple);
      } else if (node.kind == Kind::Class) {
        expect(kind(0) == Kind::Text && kind(1) == Kind::Text && kind(2) == Kind::Text);
        expect(kind(3) == Kind::Tuple && kind(5) == Kind::Dict);
        expect(kind(4) == Kind::None || kind(4) == Kind::Text || kind(4) == Kind::Tuple || kind(4) == Kind::List);
      } else if (node.kind == Kind::Reduced) {
        expect(kind(1) == Kind::Tuple);
        expect(kind(3) == Kind::None || kind(3) == Kind::List);
        expect(kind(4) == Kind::None || kind(4) == Kind::List);
        if (kind(4) == Kind::List)
          for (auto id : nodes[node.refs[4]].refs)
            expect(nodes[id].kind == Kind::Tuple && nodes[id].refs.size() == 2);
      } else if (node.kind == Kind::Imported) expect(kind(0) == Kind::Text && kind(1) == Kind::Text);
      else if (node.kind == Kind::Module) expect(kind(0) == Kind::Text);
    }
  }
  PyObject* constructor_value(uint32_t id, unsigned level = 0) {
    if (level >= 512) invalid("reducer constructor dependency depth exceeds 512");
    auto& node = nodes[id];
    auto* object = ensure(id);
    // Existing shells close ordinary graph cycles; the execution barrier below
    // distinguishes those links from a constructor that depends on itself.
    if (node.phase != Phase::Unseen) return object;
    node.phase = Phase::Resolving;
    link_node(node);
    for (auto child : node.refs) constructor_value(child, level + 1);
    finish_hashes(node);
    restore_items(node);
    restore_state(node);
    finish_class(node);
    node.phase = Phase::Ready;
    return object;
  }
  void require_ready(uint32_t id) {
    std::vector<uint32_t> pending{id};
    std::unordered_set<uint32_t> seen;
    while (!pending.empty()) {
      auto current = pending.back(); pending.pop_back();
      if (!seen.insert(current).second) continue;
      auto& node = nodes[current];
      if (node.dependencies_ready) continue;
      if (node.phase != Phase::Ready || node.building)
        invalid("cyclic constructor dependency reaches an unfinished CPython object");
      pending.insert(pending.end(), node.refs.begin(), node.refs.end());
    }
    for (auto current : seen) nodes[current].dependencies_ready = true;
  }
  bool prepare_definition(uint32_t id, uint32_t target, std::unordered_set<uint32_t>& visiting, unsigned level = 0) {
    if (level >= 512) invalid("class definition dependency depth exceeds 512");
    if (id == target) return false;
    auto& node = nodes[id];
    if (node.phase == Phase::Ready) return true;
    if (!visiting.insert(id).second) return false;
    if (node.kind == Kind::Cell && !node.refs.empty() && node.refs[0] == target) {
      ensure(id); return false;
    }
    ensure(id);
    link_node(node);
    bool ready = true;
    for (auto child : node.refs) if (!prepare_definition(child, target, visiting, level + 1)) ready = false;
    if (ready) {
      finish_hashes(node); restore_items(node); restore_state(node); finish_class(node);
      node.phase = Phase::Ready;
    }
    return ready;
  }
  void class_cell(uint32_t value, uint32_t target, PyObject* ns, unsigned level = 0) {
    if (level >= 512) invalid("class descriptor dependency depth exceeds 512");
    auto& node = nodes[value];
    if (node.kind == Kind::StaticMethod || node.kind == Kind::ClassMethod) { class_cell(node.refs[0], target, ns, level + 1); return; }
    if (node.kind == Kind::Property) {
      for (size_t i = 0; i < 3; ++i) class_cell(node.refs[i], target, ns, level + 1);
      return;
    }
    if (node.kind != Kind::Function || nodes[node.refs[6]].kind != Kind::Tuple) return;
    auto* code = reinterpret_cast<PyCodeObject*>(ref(node, 0));
    Ref names(PyCode_GetFreevars(code));
    auto& closure = nodes[node.refs[6]];
    for (size_t i = 0; i < closure.refs.size(); ++i) {
      auto& cell = nodes[closure.refs[i]];
      if (cell.refs.size() == 1 && cell.refs[0] == target &&
          PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(names.p, i), "__class__") == 0)
        checked(PyMapping_SetItemString(ns, "__classcell__", ensure(closure.refs[i])));
    }
  }
  PyObject* ensure(uint32_t id) {
    auto& node = nodes[id];
    if (node.object.p && node.kind != Kind::Code) return node.object.p;
    if (node.building) invalid("unsupported cyclic constructor dependency in CPython snapshot");
    if (depth >= 512) invalid("CPython snapshot dependency depth exceeds 512");
    struct Depth { unsigned& value; Depth(unsigned& v) : value(v) { ++value; } ~Depth() { --value; } } guard(depth);
    node.building = 1;
    switch (node.kind) {
      case Kind::None: node.object = Ref(Py_NewRef(Py_None)); break;
      case Kind::False: node.object = Ref(Py_NewRef(Py_False)); break;
      case Kind::True: node.object = Ref(Py_NewRef(Py_True)); break;
      case Kind::Ellipsis: node.object = Ref(Py_NewRef(Py_Ellipsis)); break;
      case Kind::NotImplemented: node.object = Ref(Py_NewRef(Py_NotImplemented)); break;
      case Kind::Code: {
        if (PyCode_Check(node.object.p)) break;
        Ref code(PyMarshal_ReadObjectFromString(PyBytes_AS_STRING(node.object.p), PyBytes_GET_SIZE(node.object.p)));
        expect(PyCode_Check(code.p)); node.object = std::move(code); break;
      }
      case Kind::List: node.object = Ref(PyList_New(0)); break;
      case Kind::Dict: case Kind::Globals: node.object = Ref(PyDict_New()); break;
      case Kind::Set: node.object = Ref(PySet_New(nullptr)); break;
      case Kind::Cell: node.object = Ref(PyCell_New(nullptr)); break;
      case Kind::Tuple: {
        node.object = Ref(PyTuple_New(static_cast<Py_ssize_t>(node.refs.size())));
        for (size_t i = 0; i < node.refs.size(); ++i) PyTuple_SET_ITEM(node.object.p, i, Py_NewRef(ref(node, i)));
        break;
      }
      case Kind::FrozenSet: {
        Ref items(PyList_New(0));
        for (auto item : node.refs) {
          auto* value = constructor_value(item); require_ready(item);
          checked(PyList_Append(items.p, value));
        }
        node.object = Ref(PyFrozenSet_New(items.p)); break;
      }
      case Kind::Function: {
        auto* code = ref(node, 0); auto* globals = ref(node, 1);
        Ref module(PyImport_ImportModule("builtins"));
        auto* builtins = node.refs.size() == 13 ? ref(node, 12) : PyModule_GetDict(module.p);
        expect(PyDict_Check(builtins));
        auto* previous = PyDict_GetItemString(globals, "__builtins__");
        Ref saved; if (previous) saved = Ref(Py_NewRef(previous));
        checked(PyDict_SetItemString(globals, "__builtins__", builtins));
        node.object = Ref(PyFunction_New(code, globals));
        if (saved.p) checked(PyDict_SetItemString(globals, "__builtins__", saved.p));
        else checked(PyDict_DelItemString(globals, "__builtins__"));
        auto* closure = ref(node, 6);
        Ref freevars(PyCode_GetFreevars(reinterpret_cast<PyCodeObject*>(code)));
        expect(PyTuple_GET_SIZE(freevars.p) == (closure == Py_None ? 0 : PyTuple_GET_SIZE(closure)));
        checked(PyFunction_SetClosure(node.object.p, closure));
        break;
      }
      case Kind::Module: node.object = Ref(PyImport_Import(ref(node, 0))); break;
      case Kind::Imported: {
        Ref module(PyImport_Import(ref(node, 0)));
        Ref dot(PyUnicode_FromString(".")); Ref names(PyUnicode_Split(ref(node, 1), dot.p, -1));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(names.p); ++i)
          module = Ref(PyObject_GetAttr(module.p, PyList_GET_ITEM(names.p, i)));
        node.object = std::move(module); break;
      }
      case Kind::Class: {
        auto* bases = ref(node, 3);
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(bases); ++i) {
          auto* base = PyTuple_GET_ITEM(bases, i);
          expect(PyType_Check(base));
        }
        auto* metaclass = node.refs.size() == 7 ? constructor_value(node.refs[6]) : reinterpret_cast<PyObject*>(&PyType_Type);
        expect(PyType_Check(metaclass));
        for (auto base : nodes[node.refs[3]].refs) constructor_value(base);
        auto prepare = attr(metaclass, "__prepare__");
        Ref ns(PyObject_CallFunctionObjArgs(prepare.p, ref(node, 0), bases, nullptr));
        expect(PyMapping_Check(ns.p));
        checked(PyMapping_SetItemString(ns.p, "__module__", ref(node, 2)));
        checked(PyMapping_SetItemString(ns.p, "__qualname__", ref(node, 1)));
        auto* slots = ref(node, 4);
        if (PyList_Check(slots)) {
          auto& source = nodes[node.refs[4]];
          Ref tuple(PyTuple_New(static_cast<Py_ssize_t>(source.refs.size())));
          for (size_t i = 0; i < source.refs.size(); ++i) PyTuple_SET_ITEM(tuple.p, i, Py_NewRef(ref(source, i)));
          checked(PyMapping_SetItemString(ns.p, "__slots__", tuple.p));
        } else if (slots != Py_None) checked(PyMapping_SetItemString(ns.p, "__slots__", slots));
        if (node.refs.size() == 7) {
          auto& members = nodes[node.refs[5]];
          std::unordered_set<uint32_t> visiting;
          for (size_t i = 0; i < members.refs.size(); i += 2) {
            auto value = members.refs[i + 1];
            if (value == id) continue;
            prepare_definition(value, id, visiting);
            checked(PyObject_SetItem(ns.p, ensure(members.refs[i]), ensure(value)));
            class_cell(value, id, ns.p);
          }
        }
        node.object = Ref(PyObject_CallFunctionObjArgs(metaclass, ref(node, 0), bases, ns.p, nullptr));
        node.class_hooks_run = node.refs.size() == 7;
        break;
      }
      case Kind::Instance: {
        auto* klass = ref(node, 0); expect(PyType_Check(klass));
        auto* type = reinterpret_cast<PyTypeObject*>(klass);
        expect(python_instance_layout(type));
        Ref args(PyTuple_New(0)); node.object = Ref(PyBaseObject_Type.tp_new(type, args.p, nullptr)); break;
      }
      case Kind::Reduced: {
        auto* callable = constructor_value(node.refs[0]);
        auto* args = constructor_value(node.refs[1]);
        require_ready(node.refs[0]); require_ready(node.refs[1]);
        expect(PyCallable_Check(callable) && PyTuple_Check(args));
        node.object = Ref(PyObject_CallObject(callable, args)); break;
      }
      case Kind::StaticMethod: node.object = Ref(PyStaticMethod_New(ref(node, 0))); break;
      case Kind::ClassMethod: node.object = Ref(PyClassMethod_New(ref(node, 0))); break;
      case Kind::Property:
        node.object = Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyProperty_Type),
          ref(node, 0), ref(node, 1), ref(node, 2), ref(node, 3), nullptr)); break;
      case Kind::Method: node.object = Ref(PyMethod_New(ref(node, 0), ref(node, 1))); break;
      default: invalid("missing CPython snapshot value");
    }
    node.building = 0;
    return node.object.p;
  }
  bool plain_key(uint32_t id, unsigned level = 0) {
    if (level == 512) invalid("snapshot key depth exceeds 512");
    const auto& node = nodes[id];
    switch (node.kind) {
      case Kind::None: case Kind::False: case Kind::True: case Kind::Ellipsis: case Kind::NotImplemented:
      case Kind::Int: case Kind::Float: case Kind::Complex: case Kind::Text: case Kind::Bytes: return true;
      case Kind::Tuple: case Kind::FrozenSet:
        for (auto ref : node.refs) if (!plain_key(ref, level + 1)) return false;
        return true;
      default: return false;
    }
  }
  void set_attributes(PyObject* object, PyObject* dictionary) {
    expect(PyDict_Check(dictionary));
    Py_ssize_t pos = 0; PyObject *key, *value;
    while (PyDict_Next(dictionary, &pos, &key, &value)) {
      expect(PyUnicode_Check(key));
      checked(PyType_Check(object) ? PyType_Type.tp_setattro(object, key, value) : PyObject_SetAttr(object, key, value));
    }
  }
  void link_node(Node& node) {
    if (node.linked) return;
    node.linked = true;
    if (node.kind == Kind::List)
      for (auto id : node.refs) checked(PyList_Append(node.object.p, ensure(id)));
    else if (node.kind == Kind::Cell && !node.refs.empty()) checked(PyCell_Set(node.object.p, ref(node, 0)));
    else if (node.kind == Kind::Function) {
      auto* fn = node.object.p;
      checked(PyObject_SetAttrString(fn, "__name__", ref(node, 2)));
      checked(PyObject_SetAttrString(fn, "__qualname__", ref(node, 3)));
      checked(PyFunction_SetDefaults(fn, ref(node, 4)));
      checked(PyFunction_SetKwDefaults(fn, ref(node, 5)));
      checked(PyObject_SetAttrString(fn, "__dict__", ref(node, 7)));
      checked(PyFunction_SetAnnotations(fn, ref(node, 8)));
      checked(PyObject_SetAttrString(fn, "__module__", ref(node, 9)));
      checked(PyObject_SetAttrString(fn, "__doc__", ref(node, 10)));
      checked(PyObject_SetAttrString(fn, "__type_params__", ref(node, 11)));
    }
    // Publish non-executing edges before following cycles or invoking user code.
    if (node.kind == Kind::Dict || node.kind == Kind::Globals)
      for (size_t i = 0; i < node.refs.size(); i += 2)
        if (plain_key(node.refs[i])) checked(PyDict_SetItem(node.object.p, ref(node, i), ref(node, i + 1)));
    if (node.kind == Kind::Class) {
      auto* members = ref(node, 5);
      link_node(nodes[node.refs[5]]);
      set_attributes(node.object.p, members);
    }
    if (node.kind == Kind::Set)
      for (auto id : node.refs) if (plain_key(id)) checked(PySet_Add(node.object.p, ensure(id)));
  }
  void restore_items(Node& node) {
    // Pickle's reducer contract applies items before state, with identity already published.
    if (node.kind == Kind::Reduced) {
      auto* items = ref(node, 3);
      if (items != Py_None) {
        auto append = attr(node.object.p, "append");
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items); ++i) {
          Ref result(PyObject_CallOneArg(append.p, PyList_GET_ITEM(items, i)));
        }
      }
      auto* pairs = ref(node, 4);
      if (pairs != Py_None) for (Py_ssize_t i = 0; i < PyList_GET_SIZE(pairs); ++i) {
        auto* pair = PyList_GET_ITEM(pairs, i);
        checked(PyObject_SetItem(node.object.p, PyTuple_GET_ITEM(pair, 0), PyTuple_GET_ITEM(pair, 1)));
      }
    }
  }
  void restore_state(Node& node) {
    if (node.kind == Kind::Instance || node.kind == Kind::Reduced) {
      auto* state = ref(node, node.kind == Kind::Reduced ? 2 : 1);
      if (state == Py_None) return;
      if (node.kind == Kind::Reduced && ref(node, 5) != Py_None) {
        auto* setter = ref(node, 5); expect(PyCallable_Check(setter));
        Ref result(PyObject_CallFunctionObjArgs(setter, node.object.p, state, nullptr)); return;
      }
      Ref hook; hook.p = PyObject_GetAttrString(node.object.p, "__setstate__");
      if (hook.p) { Ref result(PyObject_CallOneArg(hook.p, state)); return; }
      if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw PythonError{};
      PyErr_Clear();
      auto* dictionary = state; PyObject* slots = Py_None;
      if (PyTuple_Check(state)) {
        expect(PyTuple_GET_SIZE(state) == 2);
        dictionary = PyTuple_GET_ITEM(state, 0); slots = PyTuple_GET_ITEM(state, 1);
      }
      if (dictionary != Py_None) {
        expect(PyDict_Check(dictionary));
        if (node.kind == Kind::Reduced) {
          auto existing = attr(node.object.p, "__dict__"); expect(PyDict_Check(existing.p));
          checked(PyDict_Update(existing.p, dictionary));
        } else checked(PyObject_SetAttrString(node.object.p, "__dict__", dictionary));
      }
      if (slots != Py_None) set_attributes(node.object.p, slots);
    }
  }
  void finish_hashes(Node& node) {
    if (node.kind == Kind::Dict || node.kind == Kind::Globals) {
      for (size_t i = 0; i < node.refs.size(); i += 2)
        if (!plain_key(node.refs[i])) checked(PyDict_SetItem(node.object.p, ref(node, i), ref(node, i + 1)));
    } else if (node.kind == Kind::Set) {
      for (auto id : node.refs) if (!plain_key(id)) checked(PySet_Add(node.object.p, ensure(id)));
    }
  }
  void finish_class(Node& node) {
    if (node.kind == Kind::Class && !node.class_hooks_run) {
      auto* members = ref(node, 5);
      Py_ssize_t pos = 0; PyObject *key, *value;
      while (PyDict_Next(members, &pos, &key, &value)) {
        Ref hook; hook.p = PyObject_GetAttrString(value, "__set_name__");
        if (hook.p) { Ref result(PyObject_CallFunctionObjArgs(hook.p, node.object.p, key, nullptr)); }
        else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
        else throw PythonError{};
      }
    }
  }
  void connect() {
    for (uint32_t i = 0; i < nodes.size(); ++i) ensure(i);
    for (auto& node : nodes) link_node(node);
    for (auto& node : nodes) if (node.phase == Phase::Unseen) restore_items(node);
    for (auto& node : nodes) if (node.phase == Phase::Unseen) restore_state(node);
    for (auto& node : nodes) if (node.phase == Phase::Unseen) finish_hashes(node);
    for (auto& node : nodes) if (node.phase == Phase::Unseen) finish_class(node);
  }
public:
  PyObject* read(Input& input) { parse(input); connect(); return Py_NewRef(nodes[0].object.p); }
};
}
}
namespace x3py {
PyObject* read_python_graph(Engine& engine, X3Stream* stream) {
  graph::Input input{engine, stream, x3_stream_size(stream)};
  return graph::Reader{}.read(input);
}
}
