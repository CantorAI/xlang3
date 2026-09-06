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

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct PropertyState { int64_t value = 7; unsigned reads = 0; unsigned writes = 0; };

X3Status property_get(X3CallContext*, X3Runtime*, void* data,
    const X3Value*, uint32_t argc, X3Value* result) {
  if (argc != 1) return X3_STATUS_ERROR;
  auto& state = *static_cast<PropertyState*>(data);
  ++state.reads;
  *result = x3_value_int64(state.value);
  return X3_STATUS_OK;
}

X3Status property_set(X3CallContext*, X3Runtime*, void* data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  if (argc != 2 || args[1].tag != X3_TAG_INT64) return X3_STATUS_ERROR;
  auto& state = *static_cast<PropertyState*>(data);
  state.value = args[1].as.i64;
  ++state.writes;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status register_properties(void* opaque, X3Value, void* data) {
  auto* host = static_cast<X3PackageHost*>(opaque);
  X3Module* module = nullptr;
  if (host->add_module(host, "c_properties", &module) != X3_STATUS_OK) return X3_STATUS_ERROR;
  if (host->module_add_property(module, "number", property_get, property_set, data) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  return host->module_add_property(module, "readonly", property_get, nullptr, data);
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: xlang1_compat_package_tests <old-style-package-dir>\n";
    return 2;
  }

  try {
    PropertyState state;
    X::Runtime runtime;
    runtime.AddImportRoot(argv[1]);

    X::Module package(runtime, "xlang1_compat_sample");
    X3Value c_module = x3_value_invalid();
    require(x3_runtime_register_package(runtime.get(), "c_properties", register_properties, &state, &c_module)
        == X3_STATUS_OK, "C ABI property registration failed");
    X::Value c_properties(runtime.host(), c_module, false);
    require(c_properties["number"].ToInt64() == 7 && state.reads == 1, "C ABI getter failed");
    require(c_properties.SetAttr("number", X::Value(12)), "C ABI setter failed");
    require(state.value == 12 && state.writes == 1, "C ABI setter did not update storage");
    state.value = 19;
    require(c_properties["readonly"].ToInt64() == 19, "C ABI getter returned snapshot");
    require(!c_properties.SetAttr("readonly", X::Value(20)), "C ABI readonly property accepted write");
    require(package["property_count"].ToInt64() == 3, "initial module field failed");
    require(package.SetAttr("property_count", X::Value(40)), "module field write failed");
    require(package["increment_count"]().ToInt64() == 41, "module field write was not immediate");
    auto current = package["current_count"];
    if (!current.IsValid() || current.ToInt64() != 41)
      throw std::runtime_error("module getter did not read native mutation: " + current.ToString() +
          " (" + x3_runtime_last_error(runtime.get()) + ")");
    {
      X::Runtime second;
      second.AddImportRoot(argv[1]);
      X::Module other(second, "xlang1_compat_sample");
      require(other["property_count"].ToInt64() == 3, "module property state leaked between runtimes");
      require(other.SetAttr("property_count", X::Value(8)), "second runtime property write failed");
      require(package["property_count"].ToInt64() == 41, "second runtime changed first runtime property");
    }
    require(package["current_count"].ToInt64() == 41, "second runtime teardown invalidated first runtime property");

    require(package["add"](20, 22).ToInt64() == 42, "old AddFunc<int,int> call failed");
    require(package["make_list"]()[1].ToString() == "two", "old List return failed");
    require(package["make_dict"]().GetItem("answer").ToInt64() == 42, "old Dict return failed");
    require(package["operator_style"]().ToString() == "ok", "old X::Value operators failed");
    X::Value reference_dict = runtime.Dict();
    X::Value reference_list = runtime.List();
    X::Value returned_dict = package["set_item"](reference_dict, reference_list);
    require(returned_dict == reference_dict, "module Value reference lost dictionary identity");
    require(reference_dict["item"] == reference_list, "module const Value reference lost list identity");
    require(package["read_value"](42).ToInt64() == 42, "const module method reference binding failed");
    X::Value counter = package["Counter"]();
    X::Value variable;
    require(package["Variable"].Call({X::Value(1), X::Value(2)},
        {{"option", reference_dict}}, variable), "variable constructor failed");
    auto snapshot = variable["snapshot"]();
    require(snapshot.Size() == 3 && snapshot[0].ToInt64() == 1 &&
        snapshot[1].ToInt64() == 2 && snapshot[2] == reference_dict,
        "variable constructor lost argument order or keyword object identity");
    require(package["Variable"]()["snapshot"]().Size() == 0,
        "empty variable constructor failed");
    require(package["Variable"](42)["snapshot"]()[0].ToInt64() == 42,
        "positional variable constructor failed");
    require(!package["Variable"].Call({}, {{"unknown", X::Value(1)}}, variable),
        "constructor exception was not propagated");
    require(package["Variable"](7)["snapshot"]()[0].ToInt64() == 7,
        "failed constructor damaged subsequent construction");
    require(counter["add"](7).ToInt64() == 7, "counter construction failed");
    require(counter.SetAttr("payload", reference_dict) && counter["payload"] == reference_dict,
        "native class field property lost identity");
    require(counter.SetAttr("count", X::Value(41)) && counter["add"](1).ToInt64() == 42,
        "native class field property did not update the actual C++ field");
    auto independent = package["Counter"]();
    require(independent["count"].ToInt64() == 0, "native class field property shared instance state");
    require(counter.SetAttr("count", X::Value(7)), "native class field property reset failed");
    X::Value returned_list = counter["append_total"](reference_list);
    require(returned_list == reference_list, "class Value reference lost list identity");
    require(reference_list.Size() == 1 && reference_dict["item"][0].ToInt64() == 7,
            "class Value reference mutation did not reach caller");
    require(counter["read_value"](42).ToInt64() == 42, "const class method reference binding failed");
    X::Value list = runtime.List();
    list += X::Value(runtime, 1);
    list += X::Value(runtime, 2);
    require(list[0] == X::Value(runtime, 1), "runtime wrapper list += or == failed");
    require((X::Value(runtime, 20) + X::Value(runtime, 22)).ToInt64() == 42, "runtime wrapper operator+ failed");
    require(package["bytes_size"]("abcdef").ToInt64() == -1, "string must not be treated as binary");
    require(package["value_bytes_roundtrip"]().ToString() == "ok", "X::Value ToBytes/FromBytes roundtrip failed");
    require(package["stream_roundtrip"]().ToString() == "ok", "X::Value stream roundtrip failed");
    const std::string name = package["name"].ToString();
    if (name != "compat") {
      throw std::runtime_error("old property getter failed: got '" + name + "'");
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
  return 0;
}
