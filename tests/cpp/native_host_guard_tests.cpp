/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "xlang3/xlang3.h"
#include "runtime/modules/thread/runtime_lock.h"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
struct Service { BEGIN_PACKAGE(Service) END_PACKAGE };
struct Call { X3Status status; X3Value value=x3_value_invalid(); };
struct Observation {std::string name;bool blocked;X3Status status;};
template<class F> Observation observe(const char* name,F invoke){
    std::promise<void> held,release,attempting;auto held_signal=held.get_future();auto release_signal=release.get_future().share();auto attempt_signal=attempting.get_future();
    // The holder does not call any other SDK operation while locked. Both
    // callers use the same runtime mutex through static test linkage.
    std::jthread holder([&]{auto& mutex=xlang3::xlang_runtime_execution_lock();mutex.lock();held.set_value();release_signal.wait();mutex.unlock();});
    held_signal.wait();auto call=std::async(std::launch::async,[&]{attempting.set_value();return invoke();});attempt_signal.wait();
    const bool blocked=call.wait_for(200ms)==std::future_status::timeout;
    release.set_value();holder.join();auto result=call.get();
    if(result.value.tag!=X3_TAG_INVALID)x3_value_release(result.value);
    std::cout<<name<<" blocked="<<(blocked?1:0)<<" status="<<static_cast<int>(result.status)<<std::endl;
    return {name,blocked,result.status};
}
int main(){try{
    Service service;X::Runtime runtime;auto package=runtime.RegisterPackage("native_guard_probe",service);auto* host=service.Host();
    if(!host||!host->create_class||!host->class_add_value||!host->value_instance)throw std::runtime_error("Missing package host capabilities");
    X3Value klass=x3_value_invalid();if(host->create_class(host,"GuardProbe",nullptr,0,&klass)!=X3_STATUS_OK)throw std::runtime_error("Class setup failed");
    X3Module* module=nullptr;if(host->add_module(host,"guard_module",&module)!=X3_STATUS_OK)throw std::runtime_error("Module setup failed");
    const auto integer=x3_value_int64(17);std::vector<Observation> observed;
    observed.push_back(observe("c_api_set_attr",[&]{return Call{x3_set_attr(runtime.get(),klass,"control",integer)};}));
    observed.push_back(observe("host_create_class",[&]{X3Value value=x3_value_invalid();const auto status=host->create_class(host,"CreatedProbe",nullptr,0,&value);return Call{status,value};}));
    observed.push_back(observe("host_class_add_value",[&]{return Call{host->class_add_value(klass,"native_field",integer)};}));
    observed.push_back(observe("host_value_instance",[&]{auto value=host->value_instance(runtime.get(),klass);return Call{value.tag==X3_TAG_INVALID?X3_STATUS_ERROR:X3_STATUS_OK,value};}));
    observed.push_back(observe("host_module_add_value",[&]{return Call{host->module_add_value(module,"native_value",integer)};}));
    observed.push_back(observe("runtime_create",[]{auto* created=x3_runtime_create();if(!created)throw std::runtime_error("Runtime creation failed");x3_runtime_destroy(created);return Call{X3_STATUS_OK};}));
    auto* retiring=std::async(std::launch::async,[]{return x3_runtime_create();}).get();
    if(!retiring)throw std::runtime_error("Runtime retirement setup failed");
    observed.push_back(observe("runtime_destroy",[&]{x3_runtime_destroy(retiring);return Call{X3_STATUS_OK};}));
    x3_value_release(klass);
    if(!observed.front().blocked||observed.front().status!=X3_STATUS_OK)throw std::runtime_error("Guarded C API control did not serialize");
    bool good=true;for(const auto& r:observed){if(r.status!=X3_STATUS_OK)throw std::runtime_error("Package operation failed");good=good&&r.blocked;}
    return good?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}}