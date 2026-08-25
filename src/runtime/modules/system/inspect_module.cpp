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
#include "xlang3/builtins.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/generator.h"
#include "xlang3/ir.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace xlang3 {

namespace {

Value g_parameter_class;
Value g_signature_class;
Value g_bound_arguments_class;

Value inspect_class_or_fallback(const Value& klass, const char* name) {
  if (klass.tag == ValueTag::Object && value_as_class(klass) != nullptr) {
    return klass;
  }
  return Value::class_object(name, {{"__module__", Value::string("inspect")}});
}

bool inspect_return_bool(bool value, Value& out) {
  value_set_bool(out, value);
  return true;
}

bool inspect_arity_one(const char* name, uint32_t argc, std::string& error) {
  if (argc == 1) {
    return true;
  }
  error = std::string("inspect.") + name + "() expected one argument";
  return false;
}

bool inspect_get_attr_any(const Value& object, const std::string& name, Value& out) {
  std::string ignored;
  if (value_as_module(object) != nullptr) {
    return module_get_attr(object, name, out, ignored);
  }
  return object_get_attr(object, name, out, ignored);
}

std::string inspect_attr_string(const Value& object, const std::string& name) {
  Value attr;
  if (!inspect_get_attr_any(object, name, attr)) {
    return "";
  }
  if (auto* text = value_as_string(attr)) {
    return string_object_to_string(*text);
  }
  return "";
}

std::string inspect_function_filename(const FunctionObject* function) {
  if (function != nullptr && function->module != nullptr) {
    return function->module->source_file.empty() ? "<xlang3>" : function->module->source_file;
  }
  return "<xlang3>";
}

uint32_t inspect_function_first_line(const FunctionObject* function) {
  if (function != nullptr && function->module != nullptr && function->function_id < function->module->functions.size()) {
    return function->module->functions[function->function_id].first_line;
  }
  return 0;
}

std::string inspect_trim_doc(std::string text) {
  std::vector<std::string> lines;
  std::string current;
  for (char ch : text) {
    if (ch == '\n') {
      lines.push_back(current);
      current.clear();
    } else if (ch != '\r') {
      current.push_back(ch);
    }
  }
  lines.push_back(current);
  while (!lines.empty()) {
    const auto& front = lines.front();
    if (std::all_of(front.begin(), front.end(), [](unsigned char ch) { return std::isspace(ch) != 0; })) {
      lines.erase(lines.begin());
    } else {
      break;
    }
  }
  while (!lines.empty()) {
    const auto& back = lines.back();
    if (std::all_of(back.begin(), back.end(), [](unsigned char ch) { return std::isspace(ch) != 0; })) {
      lines.pop_back();
    } else {
      break;
    }
  }
  size_t indent = std::string::npos;
  for (size_t i = 1; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
      ++pos;
    }
    if (pos < line.size()) {
      indent = std::min(indent, pos);
    }
  }
  if (indent != std::string::npos) {
    for (size_t i = 1; i < lines.size(); ++i) {
      if (lines[i].size() >= indent) {
        lines[i].erase(0, indent);
      }
    }
  }
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) {
      out.push_back('\n');
    }
    out += lines[i];
  }
  return out;
}

bool inspect_ismodule(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("ismodule", argc, error) && inspect_return_bool(value_as_module(args[0]) != nullptr, out);
}

bool inspect_isclass(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isclass", argc, error) && inspect_return_bool(value_as_class(args[0]) != nullptr, out);
}

bool inspect_isfunction(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isfunction", argc, error) && inspect_return_bool(value_as_function(args[0]) != nullptr, out);
}

bool inspect_isbuiltin(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isbuiltin", argc, error) && inspect_return_bool(value_as_native_function(args[0]) != nullptr, out);
}

bool inspect_ismethod(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("ismethod", argc, error) && inspect_return_bool(value_as_bound_method(args[0]) != nullptr, out);
}

bool inspect_ismethoddescriptor(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("ismethoddescriptor", argc, error)) {
    return false;
  }
  const bool descriptor = value_as_slot_descriptor(args[0]) != nullptr ||
                          object_value_is_descriptor(args[0]) ||
                          value_as_static_method(args[0]) != nullptr ||
                          value_as_class_method(args[0]) != nullptr;
  return inspect_return_bool(descriptor && value_as_function(args[0]) == nullptr && value_as_bound_method(args[0]) == nullptr, out);
}

bool inspect_isdatadescriptor(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isdatadescriptor", argc, error) && inspect_return_bool(object_value_is_data_descriptor(args[0]), out);
}

bool inspect_ismemberdescriptor(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("ismemberdescriptor", argc, error) && inspect_return_bool(value_as_slot_descriptor(args[0]) != nullptr, out);
}

bool inspect_isroutine(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("isroutine", argc, error)) {
    return false;
  }
  const bool is_routine = value_as_function(args[0]) != nullptr || value_as_native_function(args[0]) != nullptr ||
                          value_as_bound_method(args[0]) != nullptr;
  return inspect_return_bool(is_routine, out);
}

bool inspect_isgenerator(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isgenerator", argc, error) && inspect_return_bool(value_as_generator(args[0]) != nullptr, out);
}

bool inspect_isgeneratorfunction(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("isgeneratorfunction", argc, error)) {
    return false;
  }
  const FunctionObject* function = value_as_function(args[0]);
  if (auto* bound = value_as_bound_method(args[0])) {
    function = value_as_function(bound->function);
  }
  const bool result = function != nullptr &&
                      function->module != nullptr &&
                      function->function_id < function->module->functions.size() &&
                      function->module->functions[function->function_id].is_generator &&
                      !function->module->functions[function->function_id].is_coroutine;
  return inspect_return_bool(result, out);
}

bool inspect_iscoroutinefunction(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("iscoroutinefunction", argc, error)) {
    return false;
  }
  const FunctionObject* function = value_as_function(args[0]);
  if (auto* bound = value_as_bound_method(args[0])) {
    function = value_as_function(bound->function);
  }
  const bool result = function != nullptr &&
                      function->module != nullptr &&
                      function->function_id < function->module->functions.size() &&
                      function->module->functions[function->function_id].is_coroutine;
  return inspect_return_bool(result, out);
}

bool inspect_isasyncgenfunction(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("isasyncgenfunction", argc, error)) {
    return false;
  }
  const FunctionObject* function = value_as_function(args[0]);
  if (auto* bound = value_as_bound_method(args[0])) {
    function = value_as_function(bound->function);
  }
  const bool result = function != nullptr &&
                      function->module != nullptr &&
                      function->function_id < function->module->functions.size() &&
                      function->module->functions[function->function_id].is_async &&
                      function->module->functions[function->function_id].is_generator;
  return inspect_return_bool(result, out);
}

bool inspect_iscoroutine(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("iscoroutine", argc, error)) {
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  return inspect_return_bool(generator != nullptr && generator->is_coroutine, out);
}

bool inspect_isasyncgen(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("isasyncgen", argc, error)) {
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  return inspect_return_bool(generator != nullptr && generator->is_async && !generator->is_coroutine, out);
}

bool inspect_isawaitable(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("isawaitable", argc, error)) {
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  return inspect_return_bool((generator != nullptr && generator->is_coroutine) ||
                                 value_as_async_generator_awaitable(args[0]) != nullptr,
                             out);
}

bool inspect_iscode(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("iscode", argc, error) && inspect_return_bool(value_as_code(args[0]) != nullptr, out);
}

bool inspect_isframe(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("isframe", argc, error) && inspect_return_bool(value_as_frame(args[0]) != nullptr, out);
}

bool inspect_istraceback(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return inspect_arity_one("istraceback", argc, error) && inspect_return_bool(value_as_traceback(args[0]) != nullptr, out);
}

bool inspect_currentframe(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "inspect.currentframe() expected no arguments";
    return false;
  }
  out = runtime.current_frame_snapshot();
  return true;
}

bool inspect_stack(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "inspect.stack() expected optional context";
    return false;
  }
  out = Value::list({});
  return true;
}

bool inspect_getmodule(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "inspect.getmodule() expected object";
    return false;
  }
  if (value_as_module(args[0]) != nullptr) {
    value_assign_fast(out, args[0]);
    return true;
  }
  if (auto* function = value_as_function(args[0])) {
    if (function->globals_module.tag != ValueTag::Invalid) {
      value_assign_fast(out, function->globals_module);
      return true;
    }
  }
  if (auto* bound = value_as_bound_method(args[0])) {
    if (auto* function = value_as_function(bound->function)) {
      if (function->globals_module.tag != ValueTag::Invalid) {
        value_assign_fast(out, function->globals_module);
        return true;
      }
    }
  }
  Value module_name;
  if (inspect_get_attr_any(args[0], "__module__", module_name)) {
    if (auto* text = value_as_string(module_name)) {
      out = Value::string(string_object_to_string(*text));
      return true;
    }
  }
  value_set_none(out);
  return true;
}

bool inspect_getfile(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "inspect.getfile() expected object";
    return false;
  }
  if (auto* module = value_as_module(args[0])) {
    Value file;
    if (module_get_attr(args[0], "__file__", file, error)) {
      value_assign_fast(out, file);
      return true;
    }
    out = Value::string(module->name);
    return true;
  }
  if (auto* function = value_as_function(args[0])) {
    out = Value::string(inspect_function_filename(function));
    return true;
  }
  if (auto* bound = value_as_bound_method(args[0])) {
    if (auto* function = value_as_function(bound->function)) {
      out = Value::string(inspect_function_filename(function));
      return true;
    }
  }
  if (auto* code = value_as_code(args[0])) {
    if (!code->filename_override.empty()) {
      out = Value::string(code->filename_override);
    } else if (code->module != nullptr && !code->module->source_file.empty()) {
      out = Value::string(code->module->source_file);
    } else {
      out = Value::string("<xlang3>");
    }
    return true;
  }
  if (auto* frame = value_as_frame(args[0])) {
    if (frame->module != nullptr && !frame->module->source_file.empty()) {
      out = Value::string(frame->module->source_file);
    } else {
      out = Value::string("<xlang3>");
    }
    return true;
  }
  Value file;
  std::string ignored;
  if (object_get_attr(args[0], "__file__", file, ignored)) {
    value_assign_fast(out, file);
    return true;
  }
  out = Value::string("<xlang3>");
  return true;
}

bool inspect_getabsfile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_getfile(runtime, args, argc, out, error, nullptr)) {
    return false;
  }
  if (auto* text = value_as_string(out)) {
    std::error_code ec;
    auto path = std::filesystem::absolute(string_object_to_string(*text), ec);
    if (!ec) {
      out = Value::string(path.string());
    }
  }
  return true;
}

bool inspect_getmodulename(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "inspect.getmodulename() expected path";
    return false;
  }
  auto* text = value_as_string(args[0]);
  if (text == nullptr) {
    error = "inspect.getmodulename() path must be str";
    return false;
  }
  std::filesystem::path path(string_object_to_string(*text));
  const std::string ext = path.extension().string();
  if (ext == ".py" || ext == ".pyc" || ext == ".pyd" || ext == ".so") {
    out = Value::string(path.stem().string());
  } else {
    value_set_none(out);
  }
  return true;
}

bool inspect_getmro(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("getmro", argc, error)) {
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "inspect.getmro() expected class";
    return false;
  }
  if (!object_get_attr(args[0], "__mro__", out, error)) {
    return false;
  }
  return true;
}

bool inspect_getdoc(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("getdoc", argc, error)) {
    return false;
  }
  Value doc;
  if (inspect_get_attr_any(args[0], "__doc__", doc)) {
    if (auto* text = value_as_string(doc)) {
      out = Value::string(inspect_trim_doc(string_object_to_string(*text)));
      return true;
    }
  }
  value_set_none(out);
  return true;
}

bool inspect_cleandoc(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("cleandoc", argc, error)) {
    return false;
  }
  auto* text = value_as_string(args[0]);
  if (text == nullptr) {
    error = "inspect.cleandoc() expected str";
    return false;
  }
  out = Value::string(inspect_trim_doc(string_object_to_string(*text)));
  return true;
}

bool inspect_unwrap(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "inspect.unwrap() expected object and optional stop";
    return false;
  }
  Value current = args[0];
  for (uint32_t depth = 0; depth < 100; ++depth) {
    if (argc == 2 && args[1].tag != ValueTag::None) {
      Value stop_result;
      if (!runtime_call_callable(runtime, args[1], &current, 1, stop_result, error)) {
        return false;
      }
      if (value_truthy(stop_result)) {
        value_assign_fast(out, current);
        return true;
      }
    }
    Value wrapped;
    if (!inspect_get_attr_any(current, "__wrapped__", wrapped)) {
      value_assign_fast(out, current);
      return true;
    }
    current = wrapped;
  }
  error = "wrapper loop when unwrapping";
  return false;
}

bool inspect_getsource(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("getsource", argc, error)) {
    return false;
  }
  Value file;
  if (!inspect_getfile(runtime, args, 1, file, error, nullptr)) {
    return false;
  }
  auto* text = value_as_string(file);
  if (text == nullptr) {
    error = "source file is not a string";
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(string_object_to_string(*text), bytes, error)) {
    return false;
  }
  out = Value::string(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  return true;
}

bool inspect_getsourcelines(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value source;
  if (!inspect_getsource(runtime, args, argc, source, error, nullptr)) {
    return false;
  }
  auto* text = value_as_string(source);
  std::vector<Value> lines;
  if (text != nullptr) {
    std::istringstream stream(string_object_to_string(*text));
    std::string line;
    while (std::getline(stream, line)) {
      lines.push_back(Value::string(line + "\n"));
    }
  }
  int64_t first_line = 1;
  if (auto* function = value_as_function(args[0])) {
    first_line = inspect_function_first_line(function);
  } else if (auto* bound = value_as_bound_method(args[0])) {
    first_line = inspect_function_first_line(value_as_function(bound->function));
  }
  out = Value::tuple({Value::list(std::move(lines)), Value::int64(first_line <= 0 ? 1 : first_line)});
  return true;
}

void append_member(std::vector<std::pair<std::string, Value>>& members, const std::string& name, const Value& value) {
  if (!name.empty() && name[0] == '#') {
    return;
  }
  members.push_back({name, value});
}

bool collect_members(const Value& object, std::vector<std::pair<std::string, Value>>& members) {
  if (auto* module = value_as_module(object)) {
    for (const auto& entry : module->name_to_slot) {
      if (entry.second < module->slots.size()) {
        append_member(members, entry.first, module->slots[entry.second]);
      }
    }
    return true;
  }
  if (auto* klass = value_as_class(object)) {
    for (const auto& entry : klass->attrs) {
      append_member(members, entry.first, entry.second);
    }
    return true;
  }
  if (auto* instance = value_as_instance(object)) {
    for (const auto& entry : instance->attrs) {
      append_member(members, entry.first, entry.second);
    }
    if (auto* klass = value_as_class(instance->klass)) {
      for (const auto& entry : klass->attrs) {
        append_member(members, entry.first, entry.second);
      }
    }
    return true;
  }
  return false;
}

bool member_matches_predicate(Runtime& runtime, const Value& predicate, const Value& member, std::string& error) {
  Value predicate_result;
  if (!runtime_call_callable(runtime, predicate, &member, 1, predicate_result, error)) {
    return false;
  }
  return value_truthy(predicate_result);
}

bool inspect_getmembers(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "inspect.getmembers() expected object and optional predicate";
    return false;
  }
  std::vector<std::pair<std::string, Value>> members;
  collect_members(args[0], members);
  std::sort(
      members.begin(),
      members.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
      });
  std::vector<Value> result;
  result.reserve(members.size());
  for (const auto& member : members) {
    if (argc == 2 && !member_matches_predicate(runtime, args[1], member.second, error)) {
      if (!error.empty()) {
        return false;
      }
      continue;
    }
    result.push_back(Value::tuple({Value::string(member.first), member.second}));
  }
  out = Value::list(std::move(result));
  return true;
}

bool inspect_getfullargspec(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "inspect.getfullargspec() expected callable";
    return false;
  }
  std::vector<Value> arg_names;
  Value defaults = Value::none();
  if (auto* function = value_as_function(args[0])) {
    if (function->module != nullptr && function->function_id < function->module->functions.size()) {
      const auto& fn = function->module->functions[function->function_id];
      arg_names.reserve(fn.params.size());
      for (const auto& param : fn.params) {
        arg_names.push_back(Value::string(param));
      }
    }
    if (!function->defaults.empty()) {
      defaults = Value::tuple(function->defaults);
    }
  } else if (auto* bound = value_as_bound_method(args[0])) {
    if (auto* function = value_as_function(bound->function)) {
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        uint32_t start = fn.params.empty() ? 0 : 1;
        for (uint32_t i = start; i < fn.params.size(); ++i) {
          arg_names.push_back(Value::string(fn.params[i]));
        }
      }
      if (!function->defaults.empty()) {
        defaults = Value::tuple(function->defaults);
      }
    }
  } else if (value_as_native_function(args[0]) == nullptr) {
    error = "inspect.getfullargspec() expected callable";
    return false;
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"args", Value::list(std::move(arg_names))});
  attrs.push_back({"varargs", Value::none()});
  attrs.push_back({"varkw", Value::none()});
  attrs.push_back({"defaults", std::move(defaults)});
  attrs.push_back({"kwonlyargs", Value::list({})});
  attrs.push_back({"kwonlydefaults", Value::none()});
  attrs.push_back({"annotations", Value::dict({})});
  Value klass = Value::class_object("FullArgSpec", {{"__module__", Value::string("inspect")}});
  out = Value::instance(klass);
  for (const auto& attr : attrs) {
    if (!object_set_attr(out, attr.first, attr.second, error)) {
      return false;
    }
  }
  return true;
}

Value make_parameter(Runtime& runtime, const std::string& name, int64_t kind, const Value& default_value) {
  Value klass = inspect_class_or_fallback(g_parameter_class, "Parameter");
  Value parameter = Value::instance(klass);
  std::string ignored;
  object_set_attr(parameter, "name", Value::string(name), ignored);
  object_set_attr(parameter, "kind", Value::int64(kind), ignored);
  object_set_attr(parameter, "default", default_value, ignored);
  object_set_attr(parameter, "annotation", Value::none(), ignored);
  return parameter;
}

bool parameter_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 5) {
    error = "Parameter() expected name and optional kind/default/annotation";
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    error = "Parameter name must be str";
    return false;
  }
  std::string ignored;
  Value self = args[0];
  object_set_attr(self, "name", Value::string(string_object_to_string(*name)), ignored);
  object_set_attr(self, "kind", argc >= 3 ? args[2] : Value::int64(1), ignored);
  object_set_attr(self, "default", argc >= 4 ? args[3] : Value::none(), ignored);
  object_set_attr(self, "annotation", argc >= 5 ? args[4] : Value::none(), ignored);
  value_set_none(out);
  return true;
}

bool parameter_replace(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 1) {
    error = "Parameter.replace() keyword form pending";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

Value make_signature_object(Runtime& runtime, const Value& callable) {
  std::vector<std::pair<Value, Value>> parameters;
  if (auto* function = value_as_function(callable)) {
    if (function->module != nullptr && function->function_id < function->module->functions.size()) {
      const auto& fn = function->module->functions[function->function_id];
      const size_t default_start =
          function->defaults.size() > fn.params.size() ? 0 : fn.params.size() - function->defaults.size();
      for (size_t i = 0; i < fn.params.size(); ++i) {
        Value default_value = Value::none();
        if (i >= default_start && !function->defaults.empty()) {
          default_value = function->defaults[i - default_start];
        }
        parameters.push_back({Value::string(fn.params[i]), make_parameter(runtime, fn.params[i], 1, default_value)});
      }
    }
  } else if (auto* bound = value_as_bound_method(callable)) {
    if (auto* function = value_as_function(bound->function)) {
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        const size_t default_start =
            function->defaults.size() > fn.params.size() ? 0 : fn.params.size() - function->defaults.size();
        for (size_t i = fn.params.empty() ? 0 : 1; i < fn.params.size(); ++i) {
          Value default_value = Value::none();
          if (i >= default_start && !function->defaults.empty()) {
            default_value = function->defaults[i - default_start];
          }
          parameters.push_back({Value::string(fn.params[i]), make_parameter(runtime, fn.params[i], 1, default_value)});
        }
      }
    }
  }

  Value klass = inspect_class_or_fallback(g_signature_class, "Signature");
  Value signature = Value::instance(klass);
  std::string ignored;
  object_set_attr(signature, "parameters", Value::dict(std::move(parameters)), ignored);
  object_set_attr(signature, "return_annotation", Value::none(), ignored);
  return signature;
}

bool signature_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "Signature() expected optional parameters and return_annotation";
    return false;
  }
  std::string ignored;
  Value self = args[0];
  object_set_attr(self, "parameters", argc >= 2 ? args[1] : Value::dict({}), ignored);
  object_set_attr(self, "return_annotation", argc >= 3 ? args[2] : Value::none(), ignored);
  value_set_none(out);
  return true;
}

bool signature_bind_common(const Value* args, uint32_t argc, Value& out, std::string& error, bool partial) {
  if (argc < 1) {
    error = "Signature.bind() expected self";
    return false;
  }
  Value parameters;
  if (!object_get_attr(args[0], "parameters", parameters, error)) {
    return false;
  }
  auto* dict = value_as_dict(parameters);
  if (dict == nullptr) {
    error = "Signature.parameters is not a dict";
    return false;
  }
  std::vector<std::pair<Value, Value>> bound;
  uint32_t positional_index = 1;
  for (const auto& entry : dict->entries) {
    if (positional_index < argc) {
      bound.push_back({entry.first, args[positional_index++]});
      continue;
    }
    Value parameter = entry.second;
    Value default_value;
    std::string ignored;
    if (object_get_attr(parameter, "default", default_value, ignored) && default_value.tag != ValueTag::None) {
      continue;
    }
    if (!partial) {
      error = "missing a required argument";
      return false;
    }
  }
  Value klass = inspect_class_or_fallback(g_bound_arguments_class, "BoundArguments");
  out = Value::instance(klass);
  std::string ignored;
  object_set_attr(out, "arguments", Value::dict(std::move(bound)), ignored);
  object_set_attr(out, "signature", args[0], ignored);
  return true;
}

bool signature_bind(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return signature_bind_common(args, argc, out, error, false);
}

bool signature_bind_partial(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return signature_bind_common(args, argc, out, error, true);
}

bool signature_replace(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 1) {
    error = "Signature.replace() keyword form pending";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool inspect_signature(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "inspect.signature() expected callable";
    return false;
  }
  if (value_as_function(args[0]) == nullptr && value_as_bound_method(args[0]) == nullptr &&
      value_as_native_function(args[0]) == nullptr) {
    error = "inspect.signature() expected callable";
    return false;
  }
  out = make_signature_object(runtime, args[0]);
  return true;
}

bool inspect_getgeneratorstate(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("getgeneratorstate", argc, error)) {
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr) {
    error = "inspect.getgeneratorstate() expected generator";
    return false;
  }
  if (generator->done) {
    out = Value::string("GEN_CLOSED");
  } else if (!generator->started) {
    out = Value::string("GEN_CREATED");
  } else {
    out = Value::string("GEN_SUSPENDED");
  }
  return true;
}

bool inspect_getcoroutinestate(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("getcoroutinestate", argc, error)) {
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr || !generator->is_coroutine) {
    error = "inspect.getcoroutinestate() expected coroutine";
    return false;
  }
  if (generator->done) {
    out = Value::string("CORO_CLOSED");
  } else if (!generator->started) {
    out = Value::string("CORO_CREATED");
  } else {
    out = Value::string("CORO_SUSPENDED");
  }
  return true;
}

bool inspect_getgeneratorlocals(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!inspect_arity_one("getgeneratorlocals", argc, error)) {
    return false;
  }
  if (value_as_generator(args[0]) == nullptr) {
    error = "inspect.getgeneratorlocals() expected generator";
    return false;
  }
  out = Value::dict({});
  return true;
}

bool inspect_getcoroutinelocals(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  return inspect_getgeneratorlocals(runtime, args, argc, out, error, data);
}

} // namespace

void register_inspect_module(Runtime& runtime) {
  Value parameter_class = Value::class_object(
      "Parameter",
      {
          {"__module__", Value::string("inspect")},
          {"__init__", runtime.make_native_function("inspect.Parameter.__init__", parameter_init)},
          {"replace", runtime.make_native_function("inspect.Parameter.replace", parameter_replace)},
          {"POSITIONAL_ONLY", Value::int64(0)},
          {"POSITIONAL_OR_KEYWORD", Value::int64(1)},
          {"VAR_POSITIONAL", Value::int64(2)},
          {"KEYWORD_ONLY", Value::int64(3)},
          {"VAR_KEYWORD", Value::int64(4)},
          {"empty", Value::none()},
      });
  Value signature_class = Value::class_object(
      "Signature",
      {
          {"__module__", Value::string("inspect")},
          {"__init__", runtime.make_native_function("inspect.Signature.__init__", signature_init)},
          {"bind", runtime.make_native_function("inspect.Signature.bind", signature_bind)},
          {"bind_partial", runtime.make_native_function("inspect.Signature.bind_partial", signature_bind_partial)},
          {"replace", runtime.make_native_function("inspect.Signature.replace", signature_replace)},
          {"empty", Value::none()},
      });
  Value bound_arguments_class = Value::class_object("BoundArguments", {{"__module__", Value::string("inspect")}});
  g_parameter_class = parameter_class;
  g_signature_class = signature_class;
  g_bound_arguments_class = bound_arguments_class;
  NativeModuleBuilder builder(runtime, "inspect");
  builder.function("ismodule", inspect_ismodule)
      .function("isclass", inspect_isclass)
      .function("isfunction", inspect_isfunction)
      .function("isbuiltin", inspect_isbuiltin)
      .function("ismethod", inspect_ismethod)
      .function("ismethoddescriptor", inspect_ismethoddescriptor)
      .function("isdatadescriptor", inspect_isdatadescriptor)
      .function("ismemberdescriptor", inspect_ismemberdescriptor)
      .function("isroutine", inspect_isroutine)
      .function("isgenerator", inspect_isgenerator)
      .function("isgeneratorfunction", inspect_isgeneratorfunction)
      .function("iscoroutinefunction", inspect_iscoroutinefunction)
      .function("isasyncgenfunction", inspect_isasyncgenfunction)
      .function("iscoroutine", inspect_iscoroutine)
      .function("isasyncgen", inspect_isasyncgen)
      .function("isawaitable", inspect_isawaitable)
      .function("iscode", inspect_iscode)
      .function("isframe", inspect_isframe)
      .function("istraceback", inspect_istraceback)
      .function("currentframe", inspect_currentframe)
      .function("stack", inspect_stack)
      .function("getmodule", inspect_getmodule)
      .function("getfile", inspect_getfile)
      .function("getsourcefile", inspect_getfile)
      .function("getabsfile", inspect_getabsfile)
      .function("getmodulename", inspect_getmodulename)
      .function("getmro", inspect_getmro)
      .function("getdoc", inspect_getdoc)
      .function("cleandoc", inspect_cleandoc)
      .function("unwrap", inspect_unwrap)
      .function("getsource", inspect_getsource)
      .function("getsourcelines", inspect_getsourcelines)
      .function("getfullargspec", inspect_getfullargspec)
      .function("getmembers", inspect_getmembers)
      .function("signature", inspect_signature)
      .function("getgeneratorstate", inspect_getgeneratorstate)
      .function("getgeneratorlocals", inspect_getgeneratorlocals)
      .function("getcoroutinestate", inspect_getcoroutinestate)
      .function("getcoroutinelocals", inspect_getcoroutinelocals)
      .value("Parameter", parameter_class)
      .value("Signature", signature_class)
      .value("BoundArguments", bound_arguments_class)
      .value("GEN_CREATED", Value::string("GEN_CREATED"))
      .value("GEN_RUNNING", Value::string("GEN_RUNNING"))
      .value("GEN_SUSPENDED", Value::string("GEN_SUSPENDED"))
      .value("GEN_CLOSED", Value::string("GEN_CLOSED"))
      .value("CORO_CREATED", Value::string("CORO_CREATED"))
      .value("CORO_RUNNING", Value::string("CORO_RUNNING"))
      .value("CORO_SUSPENDED", Value::string("CORO_SUSPENDED"))
      .value("CORO_CLOSED", Value::string("CORO_CLOSED"))
      .value("_empty", Value::none());
  runtime.register_module("inspect", builder.finish());
}

} // namespace xlang3
