#include "tensor_internal.h"
#include "xlang3/object_model.h"
#include "xlang3/module_object.h"
#include "xlang3/c_api_bridge.h"
#include <cstring>
#include <limits>

namespace xlang3::tensor {
namespace {
struct Region { Runtime* runtime; Value attributes; };
thread_local std::vector<Region> regions;
Value kwargs_value(const NativeKeywordArg* kwargs, uint32_t count) {
  std::vector<std::pair<Value,Value>> result;
  for (uint32_t i=0; i<count; ++i) result.emplace_back(Value::string(kwargs[i].name),*kwargs[i].value);
  return Value::dict(std::move(result));
}
template<class F> bool guarded(std::string& error,F f) {
  try { f(); return true; } catch (const std::exception& e) { error=e.what(); return false; }
}
Value numbers(const std::vector<int64_t>& shape) {
  std::vector<Value> values; for (auto n : shape) values.push_back(Value::int64(n)); return Value::tuple(std::move(values));
}
void flatten(const Value& v, std::vector<int64_t>& shape, std::vector<Value>& values, unsigned depth=0) {
  if (depth>32) throw std::runtime_error("tensor rank exceeds 32");
  auto* list=value_as_list(v); auto* tuple=value_as_tuple(v);
  if (!list && !tuple) {
    if (v.tag!=ValueTag::Int64 && v.tag!=ValueTag::Double && v.tag!=ValueTag::Bool) throw std::runtime_error("tensor data must be numeric");
    values.push_back(v); return;
  }
  auto items=list ? list->items : static_cast<std::vector<Value>>(tuple->items);
  shape.push_back(static_cast<int64_t>(items.size()));
  std::vector<int64_t> child;
  for (size_t i=0; i<items.size(); ++i) {
    std::vector<int64_t> dims; flatten(items[i],dims,values,depth+1);
    if (i && dims!=child) throw std::runtime_error("ragged tensor data");
    child=std::move(dims);
  }
  shape.insert(shape.end(),child.begin(),child.end());
}
template<class T> void fill(Tensor& t,const std::vector<Value>& values) {
  auto* p=static_cast<T*>(t.storage->data);
  for (size_t i=0; i<values.size(); ++i) {
    const auto& v=values[i];
    if constexpr (std::is_integral_v<T>) {
      if (v.tag==ValueTag::Double) throw std::runtime_error("floating data requires floating tensor dtype");
      auto x=v.tag==ValueTag::Bool ? static_cast<int64_t>(v.as.b) : v.as.i64;
      if (x<(std::numeric_limits<T>::min)() || x>(std::numeric_limits<T>::max)()) throw std::runtime_error("tensor integer out of range");
      p[i]=static_cast<T>(x);
    } else p[i]=static_cast<T>(v.tag==ValueTag::Double ? v.as.f64 : v.tag==ValueTag::Bool ? v.as.b : v.as.i64);
  }
}
template<class T> Value as_list(const Tensor& t) {
  std::vector<Value> values; const auto n=count(t.shape); values.reserve(static_cast<size_t>(n));
  for (uint64_t i=0; i<n; ++i) {
    uint64_t index=i, off=t.offset;
    for (size_t d=t.shape.size(); d-- >0;) { off+=(index%t.shape[d])*t.strides[d]; index/=t.shape[d]; }
    T x; std::memcpy(&x,static_cast<unsigned char*>(t.storage->data)+off,sizeof(T));
    if constexpr (std::is_integral_v<T>) values.push_back(Value::int64(x));
    else values.push_back(Value::number(x));
  }
  return Value::list(std::move(values));
}
bool factory_call(Runtime& rt,const Value* args,uint32_t argc,const NativeKeywordArg* kw,uint32_t nkw,Value& out,std::string& error,void* data) {
  return guarded(error,[&] {
    if (argc!=1 || !value_as_string(args[0])) throw std::runtime_error("tensor operator factory expects an operation name");
    auto op=std::make_unique<Operator>(); op->registration=*static_cast<std::shared_ptr<Registration>*>(data);
    op->name=value_to_string(args[0]); if (op->name.empty()) throw std::runtime_error("empty tensor operation name");
    op->attributes=snapshot(kwargs_value(kw,nkw)); out=wrap_operator(rt,std::move(op));
  });
}
bool factory_plain(Runtime& rt,const Value* args,uint32_t argc,Value& out,std::string& error,void* data) {
  return factory_call(rt,args,argc,nullptr,0,out,error,data);
}
struct Fusion { Value function, attributes; };
bool fusion_call(Runtime& rt,const Value* args,uint32_t argc,const NativeKeywordArg* kw,uint32_t nkw,Value& out,std::string& error,void* data) {
  return guarded(error,[&] {
    auto& fusion=*static_cast<Fusion*>(data);
    const bool root=current_regions(rt).tag==ValueTag::None;
    regions.push_back({&rt,fusion.attributes});
    struct Pop { ~Pop(){ regions.pop_back(); } } pop;
    std::vector<X3Value> cargs; std::vector<X3KeywordArg> ckw;
    for (uint32_t i=0;i<argc;++i) cargs.push_back(to_c_value(args[i]));
    for (uint32_t i=0;i<nkw;++i) ckw.push_back({kw[i].name,to_c_value(*kw[i].value)});
    auto function=to_c_value(fusion.function); X3Value result=x3_value_invalid();
    auto status=x3_call_kw(reinterpret_cast<X3Runtime*>(&rt),function,cargs.data(),argc,ckw.data(),nkw,&result);
    x3_value_release(function); for (auto v:cargs) x3_value_release(v); for (auto v:ckw) x3_value_release(v.value);
    if (status!=X3_STATUS_OK) { x3_value_release(result); throw std::runtime_error(rt.last_error()); }
    out=from_c_value(result,error); x3_value_release(result);
    if (!error.empty()) throw std::runtime_error(error);
    if (root) out=build_graph(rt,out);
  });
}
bool fusion_plain(Runtime& rt,const Value* args,uint32_t argc,Value& out,std::string& error,void* data) {
  return fusion_call(rt,args,argc,nullptr,0,out,error,data);
}
bool decorate(Runtime& rt,const Value* args,uint32_t argc,Value& out,std::string& error,void* data) {
  return guarded(error,[&] {
    if (argc!=1) throw std::runtime_error("fusion decorator expects one function");
    auto f=std::make_unique<Fusion>(); f->function=args[0]; f->attributes=*static_cast<Value*>(data);
    out=rt.make_native_function("tensor.fused",fusion_plain,f.get(),[](void* p){delete static_cast<Fusion*>(p);},nullptr,false,fusion_call,false);
    f.release();
  });
}
bool dispatch(Runtime& rt,const Value* args,uint32_t argc,const NativeKeywordArg* kw,uint32_t nkw,Value& out,std::string& error,void* data) {
  return guarded(error,[&] {
    std::string action=static_cast<const char*>(data); auto attrs=kwargs_value(kw,nkw);
    if (action=="tensor") {
      if (argc>1) throw std::runtime_error("tensor expects data and keyword options");
      auto dt=attr(attrs,"dtype",Value::int64(X3_TENSOR_FLOAT32));
      if (dt.tag!=ValueTag::Int64) throw std::runtime_error("invalid tensor dtype");
      std::vector<Value> values; std::vector<int64_t> shape;
      if (argc) flatten(args[0],shape,values);
      auto explicit_shape=attr(attrs,"shape");
      if (explicit_shape.tag!=ValueTag::Invalid) {
        auto dims=dimensions(explicit_shape);
        if (argc && count(dims)!=values.size()) throw std::runtime_error("tensor shape/data mismatch");
        shape=std::move(dims);
      }
      auto t=allocate(rt,static_cast<X3TensorDType>(dt.as.i64),shape);
      switch (t->dtype) {
        case X3_TENSOR_FLOAT32: fill<float>(*t,values); break;
        case X3_TENSOR_FLOAT64: fill<double>(*t,values); break;
        case X3_TENSOR_INT32: fill<int32_t>(*t,values); break;
        case X3_TENSOR_INT64: fill<int64_t>(*t,values); break;
      }
      out=wrap_tensor(rt,std::move(t)); return;
    }
    if (action=="input") {
      if (argc!=1 || !value_as_string(args[0])) throw std::runtime_error("input expects a name and shape keyword");
      auto t=std::make_unique<Tensor>(); t->name=value_to_string(args[0]);
      if (t->name.empty()) throw std::runtime_error("input name is empty");
      auto dt=attr(attrs,"dtype",Value::int64(X3_TENSOR_FLOAT32));
      if (dt.tag!=ValueTag::Int64) throw std::runtime_error("invalid input dtype");
      t->dtype=static_cast<X3TensorDType>(dt.as.i64); t->shape=dimensions(attr(attrs,"shape"));
      t->strides=contiguous_strides(t->shape,item_size(t->dtype)); out=wrap_tensor(rt,std::move(t)); return;
    }
    if (action=="graph") { if (argc!=1) throw std::runtime_error("graph expects outputs"); out=build_graph(rt,args[0]); return; }
    if (action=="fusion") {
      if (argc) throw std::runtime_error("fusion accepts keyword annotations");
      auto p=std::make_unique<Value>(snapshot(attrs));
      out=rt.make_native_function("tensor.decorate",decorate,p.get(),[](void* q){delete static_cast<Value*>(q);},nullptr,false,nullptr,false);
      p.release(); return;
    }
    if (action=="reject") throw std::runtime_error("use tensor factory functions to construct this object");
    if (argc<1) throw std::runtime_error("tensor method requires self");
    auto* t=get(args[0]); auto* graph=get_graph(args[0]);
    if (action=="run" || action=="eval") {
      if (argc>2) throw std::runtime_error("run expects one bindings dictionary");
      auto g=graph ? args[0] : build_graph(rt,args[0]);
      out=run(rt,*get_graph(g),argc==2 ? args[1] : Value::dict({})); return;
    }
    if (action=="inspect") { if (!graph || argc!=1) throw std::runtime_error("inspect requires graph"); out=inspect(*graph); return; }
    if (action=="replay") { if (!graph || argc!=1) throw std::runtime_error("replay requires graph"); replay(rt,*graph,nullptr,nullptr); out=Value::none(); return; }
    if (action=="bool") throw std::runtime_error("tensor truth value is ambiguous; symbolic data-dependent branches are not supported");
    if (action=="shape" || action=="dtype" || action=="id" || action=="tolist" || action=="strides") {
      if (!t || argc!=1) throw std::runtime_error("expected a tensor");
      if (action=="shape") { out=t->registration ? Value::none() : numbers(t->shape); return; }
      if (action=="strides") { out=t->registration ? Value::none() : numbers(t->strides); return; }
      if (action=="dtype") { out=t->registration ? Value::none() : Value::int64(t->dtype); return; }
      if (action=="id") { out=Value::int64(t->id); return; }
      if (!t->storage || t->storage->device) throw std::runtime_error("tolist requires CPU data; evaluate the graph first");
      validate_layout(*t);
      switch(t->dtype) {
        case X3_TENSOR_FLOAT32: out=as_list<float>(*t); break;
        case X3_TENSOR_FLOAT64: out=as_list<double>(*t); break;
        case X3_TENSOR_INT32: out=as_list<int32_t>(*t); break;
        case X3_TENSOR_INT64: out=as_list<int64_t>(*t); break;
      }
      return;
    }
    if (action=="neg") {
      if (argc!=1) throw std::runtime_error("neg expects one tensor");
      Operator op; op.registration=std::make_shared<Registration>(); op.registration->provider="cpu"; op.registration->arity=1;
      op.name="neg"; op.attributes=Value::dict({}); out=operation(rt,op,{args[0]}); return;
    }
    if (argc!=2) throw std::runtime_error("tensor binary operation expects two operands");
    bool reverse=action[0]=='r'; if(reverse) action=action.substr(1);
    out=apply(rt,args[reverse ? 1 : 0],args[reverse ? 0 : 1],action);
  });
}
bool dispatch_plain(Runtime& rt,const Value* args,uint32_t argc,Value& out,std::string& error,void* data) {
  return dispatch(rt,args,argc,nullptr,0,out,error,data);
}
}
Value current_regions(Runtime& rt) {
  std::vector<Value> values; for(const auto& r:regions) if(r.runtime==&rt) values.push_back(r.attributes);
  return values.empty() ? Value::none() : Value::list(std::move(values));
}
Value make_factory(Runtime& rt,std::shared_ptr<Registration> registration) {
  registration->runtime = &rt;
  auto p=std::make_unique<std::shared_ptr<Registration>>(std::move(registration));
  auto v=rt.make_native_function("tensor.operator_factory",factory_plain,p.get(),[](void* q){delete static_cast<std::shared_ptr<Registration>*>(q);},nullptr,false,factory_call,false);
  p.release(); return v;
}
void register_module(Runtime& rt) {
  auto function=[&](const char* name,const char* action) { return rt.make_native_function(std::string("tensor.")+name,dispatch_plain,
    const_cast<char*>(action),nullptr,nullptr,false,dispatch); };
  auto property=[&](const char* name) { return Value::property(function(name,name),Value::none(),Value::none(),Value::none()); };
  auto klass=Value::class_object("Tensor",{{"__module__",Value::string("tensor")},
    {"__init__",function("init","reject")},{"__add__",function("add","add")},{"__radd__",function("radd","radd")},
    {"__sub__",function("sub","sub")},{"__rsub__",function("rsub","rsub")},
    {"__mul__",function("mul","mul")},{"__rmul__",function("rmul","rmul")},
    {"__truediv__",function("div","div")},{"__rtruediv__",function("rdiv","rdiv")},
    {"__matmul__",function("matmul","matmul")},{"__neg__",function("neg","neg")},{"__bool__",function("bool","bool")},
    {"eval",function("eval","eval")},{"tolist",function("tolist","tolist")},
    {"shape",property("shape")},{"strides",property("strides")},{"dtype",property("dtype")},{"id",property("id")}});
  auto op=Value::class_object("Operator",{{"__module__",Value::string("tensor")},{"__init__",function("init","reject")},
    {"__mul__",function("operator_mul","mul")},{"__bool__",function("bool","bool")}});
  auto graph=Value::class_object("Graph",{{"__module__",Value::string("tensor")},{"__init__",function("init","reject")},
    {"run",function("run","run")},{"inspect",function("inspect","inspect")},{"replay",function("replay","replay")}});
  auto module=Value::module("tensor"); std::string error;
  auto put=[&](const char* name,Value value) { if(!module_set_attr(module,name,std::move(value),error)) throw std::runtime_error(error); };
  put("Tensor",klass); put("Operator",op); put("Graph",graph);
  for (auto name:{"tensor","input","graph","fusion"}) put(name,function(name,name));
  put("float32",Value::int64(X3_TENSOR_FLOAT32)); put("float64",Value::int64(X3_TENSOR_FLOAT64));
  put("int32",Value::int64(X3_TENSOR_INT32)); put("int64",Value::int64(X3_TENSOR_INT64));
  for (uint32_t arity:{1u,2u}) {
    auto reg=std::make_shared<Registration>(); reg->provider="cpu"; reg->arity=arity; reg->name=arity==1 ? "unary_op" : "binary_op";
    put(reg->name.c_str(),make_factory(rt,reg));
  }
  rt.register_module("tensor",module);
}
}
