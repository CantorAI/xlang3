#include "tensor_internal.h"
#include "xlang3/c_api_bridge.h"
#include <algorithm>
#include <unordered_set>

namespace xlang3::tensor {
namespace {
Value transform(const Value& v, const std::unordered_map<uint64_t, Value>* bindings,
    std::unordered_set<const Object*>& active, unsigned depth) {
  if (depth > 128) throw std::runtime_error("tensor argument nesting exceeds 128");
  if (auto* t = get(v)) {
    if (!bindings) return v;
    auto it = bindings->find(t->id);
    if (it == bindings->end()) throw std::runtime_error("unresolved tensor dependency");
    return it->second;
  }
  auto* list = value_as_list(v); auto* tuple = value_as_tuple(v); auto* dict = value_as_dict(v);
  if (!list && !tuple && !dict) {
    if (v.tag == ValueTag::Object && !value_as_string(v) && !value_as_bytes(v))
      throw std::runtime_error("tensor attributes accept scalars, tensors, bytes, lists, tuples and dictionaries");
    return v;
  }
  if (!active.insert(v.as.obj).second) throw std::runtime_error("cyclic tensor argument container");
  Value result;
  if (dict) {
    std::vector<std::pair<Value, Value>> items;
    for (const auto& kv : dict->entries) {
      if (!value_as_string(kv.first)) throw std::runtime_error("tensor dictionary keys must be strings");
      items.emplace_back(kv.first, transform(kv.second, bindings, active, depth + 1));
    }
    result = Value::dict(std::move(items));
  } else {
    std::vector<Value> items;
    for (const auto& x : (list ? list->items : tuple->items)) items.push_back(transform(x, bindings, active, depth + 1));
    result = list ? Value::list(std::move(items)) : Value::tuple(std::move(items));
  }
  active.erase(v.as.obj); return result;
}
void collect(const Value& v, std::vector<Value>& result) {
  if (get(v)) { result.push_back(v); return; }
  if (auto* l = value_as_list(v)) for (const auto& x : l->items) collect(x, result);
  if (auto* l = value_as_tuple(v)) for (const auto& x : l->items) collect(x, result);
  if (auto* d = value_as_dict(v)) for (const auto& kv : d->entries) collect(kv.second, result);
}
Value node_attributes(const Tensor& t) { return t.attributes.tag == ValueTag::Invalid ? Value::dict({}) : t.attributes; }
}
Value snapshot(const Value& v) { std::unordered_set<const Object*> active; return transform(v, nullptr, active, 0); }
Value substitute(const Value& v, const std::unordered_map<uint64_t, Value>& b) {
  std::unordered_set<const Object*> active; return transform(v, &b, active, 0);
}
void dependencies(const Value& v, std::vector<Value>& result) { collect(v, result); }
Value operation(Runtime& rt, const Operator& op, std::vector<Value> operands) {
  if (op.registration->runtime && op.registration->runtime != &rt)
    throw std::runtime_error("tensor operator belongs to a different runtime");
  if (operands.size() != op.registration->arity) throw std::runtime_error("tensor operator arity mismatch");
  auto t = std::make_unique<Tensor>();
  t->registration = op.registration; t->name = op.name; t->attributes = snapshot(op.attributes);
  t->operands = std::move(operands); t->regions = current_regions(rt);
  std::vector<Value> deps = t->operands; dependencies(t->attributes, deps);
  bool tensor_found = false;
  for (const auto& value : deps) {
    if (auto* input = get(value)) {
      if (input->runtime != &rt) throw std::runtime_error("cannot combine tensors from different runtimes");
      if (!tensor_found) { t->dtype = input->dtype; tensor_found = true; }
    } else if (value.tag != ValueTag::Int64 && value.tag != ValueTag::Double && value.tag != ValueTag::Bool)
      throw std::runtime_error("tensor operand must be a tensor or numeric scalar");
  }
  if (!tensor_found) throw std::runtime_error("tensor operation requires a tensor operand");
  // Shape is unknown until a backend infers it; copying an input shape would be
  // incorrect for matmul, projections, packed operators, or broadcasting.
  return wrap_tensor(rt, std::move(t));
}
Value apply(Runtime& rt, const Value& left, const Value& right, const std::string& intrinsic) {
  if (intrinsic == "mul") {
    if (auto* op = get_operator(right)) {
      if (op->registration->runtime && op->registration->runtime != &rt)
        throw std::runtime_error("tensor operator belongs to a different runtime");
      if (op->left.tag != ValueTag::Invalid) throw std::runtime_error("cannot compose a partially bound operator on the right");
      if (op->registration->arity == 1) return operation(rt, *op, {left});
      auto bound = std::make_unique<Operator>(*op); bound->left = left;
      return wrap_operator(rt, std::move(bound));
    }
    if (auto* op = get_operator(left)) {
      if (op->left.tag == ValueTag::Invalid) throw std::runtime_error("binary tensor operator has no left operand");
      return operation(rt, *op, {op->left, right});
    }
  }
  Operator op; op.registration = std::make_shared<Registration>();
  op.registration->provider = "cpu"; op.registration->name = "binary_op";
  op.registration->arity = 2; op.name = intrinsic; op.attributes = Value::dict({});
  return operation(rt, op, {left, right});
}
bool handles(const Value& a,const Value& b) { return get(a) || get(b) || get_operator(a) || get_operator(b); }
bool binary(const Value& a,const Value& b,const char* op,Value& out,std::string& error) {
  try {
    auto* t=get(a); if(!t) t=get(b);
    auto* p=get_operator(a); if(!p) p=get_operator(b);
    auto* runtime=t ? t->runtime : p ? p->registration->runtime : nullptr;
    if (!runtime) throw std::runtime_error("operator composition needs a tensor runtime");
    out=apply(*runtime,a,b,op); return true;
  } catch(const std::exception& e) { error=e.what(); return false; }
}
Value build_graph(Runtime& rt, const Value& outputs) {
  auto graph = std::make_unique<Graph>(); graph->runtime = &rt; graph->outputs = snapshot(outputs);
  std::vector<Value> pending; dependencies(graph->outputs, pending);
  std::unordered_set<uint64_t> visited;
  std::unordered_map<std::string, uint64_t> input_names;
  while (!pending.empty()) {
    Value value = std::move(pending.back()); pending.pop_back();
    auto* t = get(value);
    if (!t || !visited.insert(t->id).second) continue;
    if (t->runtime != &rt) throw std::runtime_error("graph contains a foreign runtime tensor");
    if (!t->registration && !t->storage && !input_names.emplace(t->name, t->id).second)
      throw std::runtime_error("duplicate symbolic input name: " + t->name);
    Step step; step.output = value; step.dependencies = t->operands;
    dependencies(t->attributes, step.dependencies); step.kernel = resolve(*t);
    for (const auto& dep : step.dependencies) if (get(dep)) pending.push_back(dep);
    graph->steps.push_back(std::move(step));
  }
  // Nodes are assigned IDs after their dependencies exist. This is also a
  // stable ordering for the backend's explicitly ordered binding operations.
  std::sort(graph->steps.begin(), graph->steps.end(), [](const Step& a, const Step& b) { return get(a.output)->id < get(b.output)->id; });
  return wrap_graph(rt, std::move(graph));
}
Value run(Runtime& rt, const Graph& graph, const Value& bindings) {
  if (graph.runtime != &rt) throw std::runtime_error("graph belongs to a different runtime");
  if (!value_as_dict(bindings)) throw std::runtime_error("graph bindings must be a dictionary");
  for (const auto& step : graph.steps) if (step.kernel == Kernel::Unsupported)
    throw std::runtime_error("CPU backend does not implement " + get(step.output)->registration->provider + ":" + get(step.output)->name);
  std::unordered_map<uint64_t, Value> values;
  for (const auto& step : graph.steps) {
    const auto& node = *get(step.output);
    Value result;
    if (step.kernel == Kernel::Constant) result = step.output;
    else if (step.kernel == Kernel::Input) {
      result = attr(bindings, node.name);
      auto* t = get(result);
      if (!t || !t->storage || t->runtime != &rt || t->dtype != node.dtype || t->shape != node.shape)
        throw std::runtime_error("missing or incompatible tensor input: " + node.name);
    } else {
      std::vector<Value> args;
      for (const auto& operand : node.operands) args.push_back(substitute(operand, values));
      result = compute(rt, node, step.kernel, args, substitute(node_attributes(node), values));
    }
    values.emplace(node.id, std::move(result));
  }
  return substitute(graph.outputs, values);
}
void replay(Runtime& rt, const Graph& graph, X3TensorVisitor visitor, void* context) {
  if (graph.runtime != &rt) throw std::runtime_error("graph belongs to a different runtime");
  for (const auto& step : graph.steps) {
    const auto& t = *get(step.output);
    auto callback = visitor ? visitor : t.registration ? t.registration->replay : nullptr;
    if (!callback) {
      if (!visitor && !t.registration) continue;
      throw std::runtime_error("no replay handler for " + t.name);
    }
    struct BorrowedValues {
      std::vector<X3Value> values;
      ~BorrowedValues() { for (auto value : values) x3_value_release(value); }
    } owned{std::vector<X3Value>(step.dependencies.size()+3, x3_value_invalid())};
    auto& inputs=owned.values;
    const auto count=step.dependencies.size();
    for (size_t i=0;i<count;++i) inputs[i]=to_c_value(step.dependencies[i]);
    inputs[count]=to_c_value(node_attributes(t));
    inputs[count+1]=to_c_value(step.output);
    inputs[count+2]=to_c_value(t.regions);
    X3TensorOperation info{sizeof(X3TensorOperation), t.id,
      t.registration ? t.registration->provider.c_str() : "",
      t.registration ? t.name.c_str() : t.storage ? "constant" : "input",
      inputs.data(), static_cast<uint32_t>(count), static_cast<uint32_t>(t.operands.size()),
      inputs[count], inputs[count+1], inputs[count+2], t.registration ? t.registration->flags : 0};
    auto status = callback(reinterpret_cast<X3Runtime*>(&rt), visitor ? context : t.registration->context, &info);
    if (status != X3_STATUS_OK) throw std::runtime_error("tensor replay failed at " + t.name + ": " + rt.last_error());
  }
}
Value inspect(const Graph& graph) {
  std::vector<Value> nodes;
  for (const auto& step : graph.steps) {
    const auto& t = *get(step.output); std::vector<Value> inputs;
    for (const auto& value : step.dependencies) inputs.push_back(get(value) ? Value::int64(get(value)->id) : value);
    nodes.push_back(record({{"id", Value::int64(t.id)},
      {"name", Value::string(t.registration ? t.name : t.storage ? "constant" : "input")},
      {"provider", Value::string(t.registration ? t.registration->provider : "")},
      {"inputs", Value::list(std::move(inputs))}, {"attributes", snapshot(node_attributes(t))},
      {"regions", snapshot(t.regions)}, {"ordered", Value::boolean(t.registration && (t.registration->flags & X3_TENSOR_ORDERED))}}));
  }
  return Value::list(std::move(nodes));
}
}
