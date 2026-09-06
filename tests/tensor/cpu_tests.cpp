#include "xlang3/xlang3.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>
#include <string>

static void Require(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }

static void OwnershipAndValidation(X::Runtime& runtime) {
  auto* host=runtime.host();
  auto* rt=host->runtime;
  int cleanups=0;
  float storage[]={1,2,3,4,5,6};
  int64_t shape[]={2,3};
  X3TensorInfo descriptor{};
  descriptor.size=sizeof(descriptor); descriptor.dtype=X3_TENSOR_FLOAT32;
  descriptor.rank=2; descriptor.shape=shape; descriptor.data=storage;
  descriptor.byte_size=sizeof(storage); descriptor.readonly=1;
  auto cleanup=[](void* p){ ++*static_cast<int*>(p); };
  X3Value raw=x3_value_invalid();
  auto bad=descriptor; bad.byte_size=4;
  Require(x3_tensor_wrap(rt,&bad,&cleanups,cleanup,&raw)==X3_STATUS_ERROR,"reject undersized storage");
  Require(cleanups==0,"failed wrap must not take ownership");
  {
    Require(x3_tensor_wrap(rt,&descriptor,&cleanups,cleanup,&raw)==X3_STATUS_OK,"wrap external storage");
    X::Tensor owner(X::Value(host,raw,false));
    auto view=owner.View({2},{12},4);
    Require(view.Info().data==storage+1 && view.Info().byte_size==20,"view span/offset");
    Require(view.Info().readonly==1,"view readonly propagation");
    X::TensorGraph graph(view+view);
    owner=X::Tensor(); view=X::Tensor();
    Require(cleanups==0,"graph retains external storage");
    X::Tensor output(graph.Run());
    auto* data=static_cast<float*>(output.Info().data);
    Require(data[0]==4 && data[1]==10,"external strided execution");
  }
  Require(cleanups==1,"external storage cleanup exactly once");
  Require(x3_tensor_create(rt,static_cast<X3TensorDType>(999),shape,2,nullptr,0,&raw)==X3_STATUS_ERROR,"invalid dtype");
  Require(x3_tensor_create(rt,X3_TENSOR_FLOAT32,nullptr,1,nullptr,0,&raw)==X3_STATUS_ERROR,"null shape");
  Require(x3_tensor_create(rt,X3_TENSOR_FLOAT32,shape,33,nullptr,0,&raw)==X3_STATUS_ERROR,"invalid rank");
  Require(x3_tensor_create(rt,X3_TENSOR_FLOAT32,shape,2,storage,4,&raw)==X3_STATUS_ERROR,"wrong input byte count");
  auto tensor=X::Tensor::Create(host,X3_TENSOR_FLOAT32,{2,3});
  int64_t stride=-4, dim=2;
  Require(x3_tensor_view(rt,tensor.raw(),&dim,&stride,1,0,&raw)==X3_STATUS_ERROR,"negative strides rejected");
  stride=100;
  Require(x3_tensor_view(rt,tensor.raw(),&dim,&stride,1,0,&raw)==X3_STATUS_ERROR,"out of bounds view");
  auto symbolic=X::Tensor::Input(host,"symbolic",X3_TENSOR_FLOAT32,{});
  Require(symbolic.Info().rank==0 && symbolic.Info().symbolic,"known scalar input");
  auto expression=X::Tensor(symbolic+symbolic);
  Require(expression.Info().rank==UINT32_MAX && expression.Info().dtype==0,"unknown expression metadata");
  X::Runtime other;
  X3TensorInfo info{}; info.size=sizeof(info);
  Require(x3_tensor_info(other.host()->runtime,tensor.raw(),&info)==X3_STATUS_ERROR,"foreign tensor rejected");
  Require(x3_tensor_apply(other.host()->runtime,tensor.raw(),tensor.raw(),"add",&raw)==X3_STATUS_ERROR,"foreign operands rejected");
}

struct ReplayState { std::vector<std::string> names; uint64_t previous=0; int cleanups=0; bool fail=false, throw_error=false; };
static X3Status Visit(X3Runtime*,void* context,const X3TensorOperation* op) {
  auto& state=*static_cast<ReplayState*>(context);
  if (state.throw_error) throw std::runtime_error("test callback exception");
  if (state.fail || !op || op->size!=sizeof(*op) || op->id<=state.previous)
    return X3_STATUS_ERROR;
  state.previous=op->id; state.names.emplace_back(op->name);
  if (std::strcmp(op->provider,"cpu_test")!=0 || !(op->flags&X3_TENSOR_ORDERED) ||
      op->operand_count==0 || op->input_count<op->operand_count) return X3_STATUS_ERROR;
  if (std::strcmp(op->name,"rms_norm")==0 && (op->operand_count!=1 || op->input_count!=2))
    return X3_STATUS_ERROR;
  if (std::strcmp(op->name,"cache_update")==0 && op->operand_count!=2) return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
static void RegisteredReplay(X::Runtime& runtime) {
  auto* host=runtime.host(); auto* rt=host->runtime;
  ReplayState state;
  {
    X3TensorOperatorDef def{}; def.size=sizeof(def); def.provider="cpu_test";
    def.name="unary_op"; def.arity=1; def.flags=X3_TENSOR_ORDERED;
    def.replay=Visit; def.context=&state;
    def.cleanup=[](void* p){ ++static_cast<ReplayState*>(p)->cleanups; };
    X3Value raw=x3_value_invalid();
    Require(x3_tensor_register_operator(rt,&def,&raw)==X3_STATUS_OK,"register unary operator");
    X::Value factory(host,raw,false);
    auto input=X::Tensor::Input(host,"x",X3_TENSOR_FLOAT32,{4});
    auto first=input*factory("bind_cache");
    X::Value norm;
    Require(factory.Call({X::Value::String(host,"rms_norm")},{{"weight",input}},norm),"tensor-valued operator attributes");
    auto second=X::Tensor(first)*norm;
    def.name="binary_op"; def.arity=2;
    Require(x3_tensor_register_operator(rt,&def,&raw)==X3_STATUS_OK,"register binary operator");
    X::Value binary(host,raw,false);
    auto partial=X::Tensor(second)*binary("cache_update");
    Require(x3_tensor_apply(rt,partial.raw(),input.raw(),"mul",&raw)==X3_STATUS_OK,"complete binary composition");
    X::Value final(host,raw,false);
    X::TensorGraph graph(final);
    factory=X::Value(); binary=X::Value(); partial=X::Value(); final=X::Value(); norm=X::Value();
    first=X::Value(); second=X::Value();
    Require(state.cleanups==0,"graph retains registration");
    graph.Replay();
    Require(state.names==std::vector<std::string>{"bind_cache","rms_norm","cache_update"},"ordered registered replay");
    state.names.clear(); state.previous=0;
    graph.Replay();
    Require(state.names.size()==3,"replay is reusable");
    state.fail=true;
    Require(x3_tensor_graph_replay(rt,graph.raw(),nullptr,nullptr)==X3_STATUS_ERROR,"callback failure propagates");
    state.fail=false;
    state.throw_error=true;
    Require(x3_tensor_graph_replay(rt,graph.raw(),nullptr,nullptr)==X3_STATUS_ERROR,"C++ callback exception is contained");
    state.throw_error=false;
    auto bindings=runtime.Dict(); bindings.Set("x",X::Tensor::Create(host,X3_TENSOR_FLOAT32,{4}));
    Require(x3_tensor_graph_run(rt,graph.raw(),bindings.raw(),&raw)==X3_STATUS_ERROR,"CPU rejects unimplemented custom kernel");
  }
  Require(state.cleanups==2,"each registration cleanup exactly once");
}

static void LargeAndThreaded(X::Runtime& runtime) {
  const size_t count=1024*1024;
  std::vector<float> data(count);
  for(size_t i=0;i<count;++i) data[i]=static_cast<float>(i%257);
  auto a=X::Tensor::Create(runtime.host(),X3_TENSOR_FLOAT32,{4,8,32,1024},data.data(),data.size()*sizeof(float));
  X::TensorGraph graph(a+a);
  X::Tensor output(graph.Run());
  auto* result=static_cast<float*>(output.Info().data);
  for(size_t i=0;i<count;++i) Require(result[i]==data[i]*2,"large high dimensional result");
  std::atomic<int> failures{0};
  std::vector<std::thread> workers;
  for(int worker=0;worker<4;++worker) workers.emplace_back([&] {
    try {
      for(int run=0;run<5;++run) {
        X::Tensor out(graph.Run());
        auto* values=static_cast<float*>(out.Info().data);
        for(size_t i=0;i<count;i+=997) if(values[i]!=data[i]*2) ++failures;
      }
    } catch(...) { ++failures; }
  });
  for(auto& worker:workers) worker.join();
  Require(failures==0,"graph calls from multiple host threads");
  Require(std::memcmp(a.Info().data,data.data(),data.size()*sizeof(float))==0,"execution preserves inputs");
}
int main() {
  try {
    X::Runtime runtime;
    float data[]={1,2,3,4,5,6};
    auto a=X::Tensor::Create(runtime.host(),X3_TENSOR_FLOAT32,{2,3},data,sizeof(data));
    auto b=X::Tensor::Input(runtime.host(),"b",X3_TENSOR_FLOAT32,{2,3});
    X::TensorGraph graph(a+b);
    auto bindings=runtime.Dict(); bindings.Set("b",a);
    auto output=X::Tensor(graph.Run(bindings)); auto info=output.Info();
    Require(info.byte_size==sizeof(data),"output bytes");
    auto* values=static_cast<float*>(info.data);
    for(int i=0;i<6;++i) Require(values[i]==data[i]*2,"CPU addition");
    auto transposed=a.View({3,2},{4,12});
    X::TensorGraph viewGraph(transposed+transposed);
    auto viewOutput=X::Tensor(viewGraph.Run());
    float expected[]={2,8,4,10,6,12};
    Require(std::memcmp(viewOutput.Info().data,expected,sizeof(expected))==0,"strided CPU addition");
    OwnershipAndValidation(runtime);
    RegisteredReplay(runtime);
    LargeAndThreaded(runtime);
    std::cout<<"tensor-cpu-passed\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
