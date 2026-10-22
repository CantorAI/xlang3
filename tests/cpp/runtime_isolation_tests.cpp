#include "xlang3/cpp/runtime.h"
#include <atomic>
#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>

void source_only(X::Runtime& runtime){
    X::Module sys(runtime,"sys");
    if(!sys.SetAttr("dont_write_bytecode",X::Value(runtime,true)))
        throw std::runtime_error("Cannot disable test fixture bytecode writes");
}

void check(X::Module& module,X::Runtime& runtime,const char* phase="concurrent"){
    X::Value result;
    if(!module["verify"].Call(nullptr,0,result)||!result.IsInt64()||result.ToInt64()!=1)
        throw std::runtime_error(std::string(phase)+": ctypes array base mismatch; result="+result.ToString()+"; error="+runtime.LastError());
}

int main(){try{
    {
        X::Runtime first;source_only(first);first.AddImportRoot(XLANG3_RUNTIME_ISOLATION_FIXTURE);
        X::Module module(first,"runtime_ctypes_isolation");
        check(module,first,"before second runtime");
        X::Runtime second;
        check(module,first,"after second runtime");
    }
    std::atomic<unsigned> completed=0,failed=0;
    std::barrier start(4);
    std::vector<std::thread> threads;
    for(unsigned i=0;i<4;++i)threads.emplace_back([&]{for(unsigned round=0;round<20;++round){
        start.arrive_and_wait();
        try{X::Runtime runtime;source_only(runtime);runtime.AddImportRoot(XLANG3_RUNTIME_ISOLATION_FIXTURE);X::Module module(runtime,"runtime_ctypes_isolation");check(module,runtime);++completed;}
        catch(const std::exception& error){++failed;std::cerr<<error.what()<<'\n';}
        start.arrive_and_wait();
    }});
    for(auto& thread:threads)thread.join();
    if(completed!=80||failed)throw std::runtime_error("Concurrent runtime isolation failed");
    std::cout<<"Two-live-runtime ctypes inheritance and 80 concurrent runtime lifetimes passed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
