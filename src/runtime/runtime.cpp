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
#include "xlang3/runtime.h"

#include "xlang3/builtins.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/ir.h"
#if !defined(XLANG3_EMBEDDED)
#include "xlang3/import_loader.h"
#include "xlang3/native_package_loader.h"
#endif
#include "xlang3/module_object.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#if !defined(XLANG3_EMBEDDED)
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif
#endif

namespace xlang3 {

void xlang_thread_join_runtime_threads(Runtime* runtime);

namespace {

struct RuntimeCurrentFrameState {
  const std::shared_ptr<const ir::Module>* module_owner = nullptr;
  const Value* globals_module = nullptr;
  Value current_globals_module;
  uint32_t function_id = 0;
  uint32_t instruction_index = 0;
  const RuntimeFrameView* frame_stack = nullptr;
  size_t frame_stack_count = 0;
  const std::vector<std::string>* local_names = nullptr;
  const Value* local_values = nullptr;
  size_t local_count = 0;
  Value trace_function;
  bool trace_dispatch_active = false;
  Value profile_function;
  bool profile_dispatch_active = false;
};

thread_local std::unordered_map<const Runtime*, RuntimeCurrentFrameState> g_runtime_current_frames;
thread_local std::unordered_map<const Runtime*, Value> g_runtime_active_exceptions;

std::mutex g_runtime_frame_registry_mutex;
std::unordered_map<const Runtime*, std::unordered_map<int64_t, RuntimeCurrentFrameState>> g_runtime_frame_registry;
std::mutex g_runtime_exception_registry_mutex;
std::unordered_map<const Runtime*, std::unordered_map<int64_t, Value>> g_runtime_exception_registry;

int64_t runtime_current_thread_ident() {
  return static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0x7fffffffffffffffll);
}

RuntimeCurrentFrameState& current_frame_state(const Runtime& runtime) {
  return g_runtime_current_frames[&runtime];
}

void publish_current_frame_state(const Runtime& runtime) {
  std::lock_guard<std::mutex> lock(g_runtime_frame_registry_mutex);
  g_runtime_frame_registry[&runtime][runtime_current_thread_ident()] = current_frame_state(runtime);
}

void clear_current_frame_state(const Runtime& runtime) {
  std::lock_guard<std::mutex> lock(g_runtime_frame_registry_mutex);
  auto runtime_it = g_runtime_frame_registry.find(&runtime);
  if (runtime_it == g_runtime_frame_registry.end()) {
    return;
  }
  runtime_it->second.erase(runtime_current_thread_ident());
  if (runtime_it->second.empty()) {
    g_runtime_frame_registry.erase(runtime_it);
  }
}

void clear_runtime_frame_states(const Runtime& runtime) {
  std::lock_guard<std::mutex> lock(g_runtime_frame_registry_mutex);
  g_runtime_frame_registry.erase(&runtime);
}

} // namespace

Value& runtime_current_exception_state(const Runtime& runtime) {
  return g_runtime_active_exceptions[&runtime];
}

void runtime_publish_current_exception_state(const Runtime& runtime) {
  std::lock_guard<std::mutex> lock(g_runtime_exception_registry_mutex);
  g_runtime_exception_registry[&runtime][runtime_current_thread_ident()] = runtime_current_exception_state(runtime);
}

void runtime_clear_exception_states(const Runtime& runtime) {
  std::lock_guard<std::mutex> lock(g_runtime_exception_registry_mutex);
  g_runtime_exception_registry.erase(&runtime);
}

namespace {

#if !defined(XLANG3_EMBEDDED)
void ostream_output_write(void* context, const char* data, std::size_t size) {
  auto* out = static_cast<std::ostream*>(context);
  out->write(data, static_cast<std::streamsize>(size));
}

void runtime_module_anchor() {}

std::filesystem::path runtime_library_dir() {
#if defined(_WIN32)
  HMODULE module = nullptr;
  if (GetModuleHandleExA(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCSTR>(&runtime_module_anchor),
          &module) != 0) {
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
      return std::filesystem::path(path).parent_path();
    }
  }
#elif defined(__APPLE__) || defined(__linux__)
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&runtime_module_anchor), &info) != 0 && info.dli_fname != nullptr) {
    return std::filesystem::path(info.dli_fname).parent_path();
  }
#endif
  return {};
}

void add_default_import_layout(Runtime& runtime, const std::filesystem::path& base) {
  if (base.empty()) {
    return;
  }
  runtime.add_import_root(base / "lib");
  runtime.add_import_root(base / "modules");
  runtime.add_import_root(base / "site-packages");
  runtime.add_import_root(base);
}

void add_import_root_if_dir(Runtime& runtime, const std::filesystem::path& root) {
  if (root.empty()) {
    return;
  }
  std::error_code ec;
  if (std::filesystem::is_directory(root, ec)) {
    runtime.add_import_root(root);
  }
}

void add_default_python_lib_roots(Runtime& runtime) {
  if (const char* explicit_lib = std::getenv("XLANG3_PYTHON_LIB")) {
    add_import_root_if_dir(runtime, explicit_lib);
  }
  if (const char* explicit_debugpy = std::getenv("XLANG3_DEBUGPY_ROOT")) {
    add_import_root_if_dir(runtime, explicit_debugpy);
    add_import_root_if_dir(runtime, std::filesystem::path(explicit_debugpy) / "_vendored" / "pydevd");
  }
#if defined(_WIN32)
  add_import_root_if_dir(runtime, "C:/Python/Python314/Lib");
#else
  add_import_root_if_dir(runtime, "/usr/local/lib/python3.14");
  add_import_root_if_dir(runtime, "/usr/lib/python3.14");
#endif
}

std::filesystem::path normalize_import_root(std::filesystem::path root) {
  if (root.empty()) {
    root = std::filesystem::current_path();
  }
  std::error_code ec;
  auto absolute = std::filesystem::absolute(root, ec);
  if (!ec) {
    root = std::move(absolute);
  }
  auto normalized = root.lexically_normal();
  return normalized.empty() ? root : normalized;
}

bool has_import_root(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& root) {
  return std::find(roots.begin(), roots.end(), root) != roots.end();
}

std::filesystem::path module_relative_source_path(const std::string& name) {
  std::filesystem::path path;
  size_t start = 0;
  for (;;) {
    const size_t dot = name.find('.', start);
    const auto part = name.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!part.empty()) {
      path /= part;
    }
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }
  path += ".py";
  return path;
}

std::string module_source_file_from_roots(const Runtime& runtime, const std::string& name) {
  const auto relative_file = module_relative_source_path(name);
  for (const auto& root : runtime.import_roots()) {
    std::error_code ec;
    const auto candidate = root / relative_file;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      return candidate.generic_string();
    }
  }
  return {};
}
#endif

bool module_has_real_attr(const Value& module, const std::string& name) {
  uint32_t slot = 0;
  std::string ignored;
  return module_find_attr_slot(module, name, slot, ignored);
}

Value make_import_metadata_class(const std::string& class_name, const std::string& module_name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string(module_name)});
  attrs.push_back({"__name__", Value::string(class_name)});
  return Value::class_object(class_name, std::move(attrs));
}

bool is_frozen_import_metadata_module(const std::string& name) {
  return name == "abc" || name == "_frozen_importlib" || name == "_frozen_importlib_external" ||
         name == "importlib._bootstrap" || name == "importlib._bootstrap_external";
}

Value make_runtime_loader(const std::string& class_name, const std::string& module_name) {
  if (class_name == "BuiltinImporter" || class_name == "FrozenImporter") {
    return make_import_metadata_class(class_name, "_frozen_importlib");
  }
  auto loader = Value::instance(make_import_metadata_class(class_name, "_frozen_importlib"));
  std::string ignored;
  object_set_attr(loader, "name", Value::string(module_name), ignored);
  return loader;
}

Value make_runtime_module_spec(
    const std::string& name,
    const Value& loader,
    const Value& origin,
    const Value& submodule_search_locations) {
  auto spec = Value::instance(make_import_metadata_class("ModuleSpec", "_frozen_importlib"));
  std::string ignored;
  object_set_attr(spec, "name", Value::string(name), ignored);
  object_set_attr(spec, "loader", loader, ignored);
  object_set_attr(spec, "origin", origin, ignored);
  object_set_attr(spec, "cached", Value::none(), ignored);
  const auto dot = name.rfind('.');
  object_set_attr(spec, "parent", Value::string(dot == std::string::npos ? "" : name.substr(0, dot)), ignored);
  const std::string origin_text = origin.tag == ValueTag::None ? "" : value_to_string(origin);
  object_set_attr(
      spec,
      "has_location",
      Value::boolean(origin.tag != ValueTag::None && origin_text != "built-in" && origin_text != "frozen"),
      ignored);
  object_set_attr(spec, "submodule_search_locations", submodule_search_locations, ignored);
  return spec;
}

void ensure_module_import_metadata(Value& module, const std::string& name) {
  if (value_as_module(module) == nullptr) {
    return;
  }

  std::string ignored;
  if (!module_has_real_attr(module, "__package__")) {
    const auto dot = name.rfind('.');
    module_set_attr(module, "__package__", Value::string(dot == std::string::npos ? "" : name.substr(0, dot)), ignored);
  }

  Value path;
  const bool has_path = module_get_attr(module, "__path__", path, ignored) && path.tag != ValueTag::Invalid;
  Value file;
  const bool has_file = module_get_attr(module, "__file__", file, ignored) && file.tag != ValueTag::Invalid && file.tag != ValueTag::None;
  const bool is_frozen = is_frozen_import_metadata_module(name);
  Value loader = is_frozen ? make_runtime_loader("FrozenImporter", name)
                           : (has_path && !has_file ? make_runtime_loader("NamespaceLoader", name)
                                                    : make_runtime_loader(has_file ? "SourceFileLoader" : "BuiltinImporter", name));
  if (!module_has_real_attr(module, "__loader__")) {
    module_set_attr(module, "__loader__", loader, ignored);
  } else {
    module_get_attr(module, "__loader__", loader, ignored);
  }

  if (!module_has_real_attr(module, "__spec__")) {
    Value origin = is_frozen ? Value::string("frozen") : (has_file ? file : (has_path ? Value::none() : Value::string("built-in")));
    module_set_attr(module, "__spec__", make_runtime_module_spec(name, loader, origin, has_path ? path : Value::none()), ignored);
  }
}

bool module_spec_origin_is(const Value& module, const char* expected) {
  Value spec;
  std::string ignored;
  if (!module_get_attr(module, "__spec__", spec, ignored)) {
    return false;
  }
  Value origin;
  if (!object_get_attr(spec, "origin", origin, ignored)) {
    return false;
  }
  auto* text = value_as_string(origin);
  return text != nullptr && string_object_to_string(*text) == expected;
}

void canonicalize_module_loader_from_bootstrap(std::unordered_map<std::string, Value>& modules, Value& module) {
  if (value_as_module(module) == nullptr) {
    return;
  }
  const bool builtin_origin = module_spec_origin_is(module, "built-in");
  const bool frozen_origin = module_spec_origin_is(module, "frozen");
  if (!builtin_origin && !frozen_origin) {
    return;
  }
  auto bootstrap_it = modules.find("_frozen_importlib");
  if (bootstrap_it == modules.end()) {
    return;
  }
  Value loader;
  std::string ignored;
  const char* loader_name = builtin_origin ? "BuiltinImporter" : "FrozenImporter";
  if (!module_get_attr(bootstrap_it->second, loader_name, loader, ignored)) {
    return;
  }
  module_set_attr(module, "__loader__", loader, ignored);
  Value spec;
  if (module_get_attr(module, "__spec__", spec, ignored)) {
    object_set_attr(spec, "loader", loader, ignored);
  }
}

} // namespace

void Runtime::initialize() {
  modules_dict_ = Value::dict({});
  register_core_builtins(*this);
#if !defined(XLANG3_EMBEDDED)
  add_default_import_layout(*this, runtime_library_dir());
  add_default_python_lib_roots(*this);
  for (auto& entry : modules_) {
    Value existing;
    std::string ignored;
    if (module_get_attr(entry.second, "__file__", existing, ignored) && existing.tag != ValueTag::Invalid) {
      continue;
    }
    ignored.clear();
    const auto source_file = module_source_file_from_roots(*this, entry.first);
    if (!source_file.empty()) {
      module_set_attr(entry.second, "__file__", Value::string(source_file), ignored);
    }
  }
#endif
}

#if !defined(XLANG3_EMBEDDED)
Runtime::Runtime(std::ostream& out)
    : output_{&out, ostream_output_write},
      vfs_(std::make_unique<Vfs>()) {
  initialize();
}
#endif

Runtime::Runtime(OutputSink output)
    : output_(output),
      vfs_(std::make_unique<Vfs>()) {
  initialize();
}

Runtime::~Runtime() {
  std::string ignored;
  run_exit_functions(ignored);
  xlang_thread_join_runtime_threads(this);
  value_set_invalid(pending_exception_);
  value_set_invalid(active_exception_);
  value_set_invalid(runtime_current_exception_state(*this));
  value_set_invalid(current_globals_module_);
  value_set_invalid(trace_function_);
  value_set_invalid(thread_trace_function_);
  value_set_invalid(profile_function_);
  value_set_invalid(thread_profile_function_);
  value_set_invalid(debug_hook_);
  clear_current_frame();
  clear_runtime_frame_states(*this);
  runtime_clear_exception_states(*this);
  for (auto it = native_package_cleanups_.rbegin(); it != native_package_cleanups_.rend(); ++it) {
    if (it->second != nullptr) {
      it->second(it->first);
    }
  }
}

void Runtime::write_output(const char* data, std::size_t size) {
  if (data == nullptr || size == 0 || output_.write == nullptr) {
    return;
  }
  output_.write(output_.context, data, size);
}

void Runtime::write_output(const char* text) {
  if (text == nullptr) {
    return;
  }
  write_output(text, std::strlen(text));
}

void Runtime::register_builtin(std::string name, Value value) {
  builtins_[std::move(name)] = std::move(value);
}

void Runtime::register_native_builtin(
    std::string name,
    NativeFunctionCallback callback,
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    NativeKeywordFunctionCallback keyword_callback) {
  auto function_value = make_native_function(
      name,
      callback,
      nullptr,
      nullptr,
      fast_callback,
      fast_releases_vm_lock,
      keyword_callback);
  register_builtin(std::move(name), std::move(function_value));
}

const Value* Runtime::find_builtin(const std::string& name) const {
  auto it = builtins_.find(name);
  if (it == builtins_.end()) {
    return nullptr;
  }
  return &it->second;
}

void Runtime::set_current_globals_module(const Value& globals_module) {
  Value next;
  value_assign_fast(next, globals_module);
  auto& state = current_frame_state(*this);
  value_set_invalid(state.current_globals_module);
  state.current_globals_module = next;
}

const Value& Runtime::current_globals_module() const {
  const auto& state = current_frame_state(*this);
  return state.current_globals_module.tag == ValueTag::Invalid ? current_globals_module_ : state.current_globals_module;
}

bool Runtime::set_sys_argv(const std::vector<std::string>& argv, std::string& error) {
  Value sys;
  if (!import_module("sys", sys, error)) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(argv.size());
  for (const auto& item : argv) {
    values.push_back(Value::string(item));
  }
  return module_set_attr(sys, "argv", Value::list(std::move(values)), error);
}

#if !defined(XLANG3_EMBEDDED)
bool Runtime::publish_sys_path(std::string& error) {
  Value sys;
  if (!import_module("sys", sys, error)) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(import_roots_.size());
  for (const auto& root : import_roots_) {
    values.push_back(Value::string(root.string()));
  }
  return module_set_attr(sys, "path", Value::list(std::move(values)), error);
}
#endif

void Runtime::set_trace_function(Value trace_function) {
  Value next;
  value_assign_fast(next, trace_function);
  auto& state = current_frame_state(*this);
  value_set_invalid(state.trace_function);
  state.trace_function = next;
}

const Value& Runtime::trace_function() const {
  const auto& state = current_frame_state(*this);
  return state.trace_function.tag == ValueTag::Invalid ? trace_function_ : state.trace_function;
}

bool Runtime::trace_dispatch_active() const {
  return current_frame_state(*this).trace_dispatch_active;
}

void Runtime::set_trace_dispatch_active(bool active) {
  current_frame_state(*this).trace_dispatch_active = active;
}

void Runtime::set_thread_trace_function(Value trace_function) {
  Value next;
  value_assign_fast(next, trace_function);
  value_set_invalid(thread_trace_function_);
  thread_trace_function_ = next;
}

void Runtime::set_profile_function(Value profile_function) {
  Value next;
  value_assign_fast(next, profile_function);
  auto& state = current_frame_state(*this);
  value_set_invalid(state.profile_function);
  state.profile_function = next;
}

const Value& Runtime::profile_function() const {
  const auto& state = current_frame_state(*this);
  return state.profile_function.tag == ValueTag::Invalid ? profile_function_ : state.profile_function;
}

bool Runtime::profile_dispatch_active() const {
  return current_frame_state(*this).profile_dispatch_active;
}

void Runtime::set_profile_dispatch_active(bool active) {
  current_frame_state(*this).profile_dispatch_active = active;
}

bool Runtime::emit_profile_event(const char* event_name, const Value& arg, std::string& error) {
  return emit_profile_event_for_frame(current_frame_snapshot(), event_name, arg, error);
}

bool Runtime::emit_profile_event_for_frame(const Value& frame, const char* event_name, const Value& arg, std::string& error) {
  const Value& hook = profile_function();
  if (hook.tag == ValueTag::Invalid || hook.tag == ValueTag::None || profile_dispatch_active()) {
    return true;
  }
  if (frame.tag == ValueTag::None) {
    return true;
  }
  Value profile_args[3] = {
      frame,
      Value::string(event_name == nullptr ? "" : event_name),
      arg,
  };
  set_profile_dispatch_active(true);
  struct ProfileDispatchGuard {
    Runtime& runtime;
    ~ProfileDispatchGuard() { runtime.set_profile_dispatch_active(false); }
  } guard{*this};
  Value ignored;
  return runtime_call_callable(*this, hook, profile_args, 3, ignored, error);
}

void Runtime::set_thread_profile_function(Value profile_function) {
  Value next;
  value_assign_fast(next, profile_function);
  value_set_invalid(thread_profile_function_);
  thread_profile_function_ = next;
}

void Runtime::refresh_debug_poll_needed() {
  const bool debug_session_active = debug_enabled_ || value_as_function(debug_hook_) != nullptr;
  debug_poll_needed_ = debug_session_active && !debug_dispatch_active_ &&
                       (debug_pause_requested_ ||
                        debug_step_mode_ != RuntimeDebugStepMode::Continue ||
                        !debug_breakpoints_.empty());
}

void Runtime::set_debug_hook(Value hook) {
  Value next;
  value_assign_fast(next, hook);
  value_set_invalid(debug_hook_);
  debug_hook_ = next;
  refresh_debug_poll_needed();
}

void Runtime::set_debug_dispatch_active(bool active) {
  debug_dispatch_active_ = active;
  refresh_debug_poll_needed();
}

void Runtime::set_debug_enabled(bool enabled) {
  debug_enabled_ = enabled;
  refresh_debug_poll_needed();
}

void Runtime::set_debug_pause_on_hit(bool enabled) {
  debug_pause_on_hit_ = enabled;
}

void Runtime::debug_request_pause() {
  debug_pause_requested_ = true;
  refresh_debug_poll_needed();
}

void Runtime::debug_add_breakpoint(std::string file, uint32_t line) {
  if (file.empty() || line == 0) {
    return;
  }
  for (const auto& breakpoint : debug_breakpoints_) {
    if (breakpoint.line == line && breakpoint.file == file) {
      return;
    }
  }
  debug_breakpoints_.push_back(RuntimeDebugBreakpoint{std::move(file), line});
  refresh_debug_poll_needed();
}

void Runtime::debug_clear_breakpoints() {
  debug_breakpoints_.clear();
  refresh_debug_poll_needed();
}

void Runtime::debug_step_into(size_t frame_count, uint32_t line) {
  debug_step_mode_ = RuntimeDebugStepMode::StepInto;
  debug_step_frame_count_ = frame_count;
  debug_step_line_ = line;
  refresh_debug_poll_needed();
}

void Runtime::debug_step_over(size_t frame_count, uint32_t line) {
  debug_step_mode_ = RuntimeDebugStepMode::StepOver;
  debug_step_frame_count_ = frame_count;
  debug_step_line_ = line;
  refresh_debug_poll_needed();
}

void Runtime::debug_step_out(size_t frame_count) {
  debug_step_mode_ = RuntimeDebugStepMode::StepOut;
  debug_step_frame_count_ = frame_count;
  debug_step_line_ = 0;
  refresh_debug_poll_needed();
}

void Runtime::debug_continue() {
  debug_step_mode_ = RuntimeDebugStepMode::Continue;
  debug_step_frame_count_ = 0;
  debug_step_line_ = 0;
  debug_pause_requested_ = false;
  refresh_debug_poll_needed();
}

RuntimePauseReason Runtime::debug_step_pause_reason(size_t frame_count, uint32_t line) const {
  if (debug_pause_requested_) {
    return RuntimePauseReason::PauseRequest;
  }
  if (line == 0) {
    return RuntimePauseReason::None;
  }
  if (debug_step_mode_ == RuntimeDebugStepMode::StepInto) {
    if (frame_count > debug_step_frame_count_ || line != debug_step_line_) {
      return RuntimePauseReason::Step;
    }
    return RuntimePauseReason::None;
  }
  if (debug_step_mode_ == RuntimeDebugStepMode::StepOver &&
      frame_count <= debug_step_frame_count_ &&
      line != debug_step_line_) {
    return RuntimePauseReason::StepOver;
  }
  if (debug_step_mode_ == RuntimeDebugStepMode::StepOut && frame_count < debug_step_frame_count_) {
    return RuntimePauseReason::StepOut;
  }
  return RuntimePauseReason::None;
}

bool Runtime::debug_skip_breakpoint_at_step_origin(size_t frame_count, uint32_t line) const {
  if (debug_step_mode_ == RuntimeDebugStepMode::Continue || debug_step_line_ == 0) {
    return false;
  }
  return frame_count == debug_step_frame_count_ && line == debug_step_line_;
}

bool Runtime::debug_breakpoint_matches(std::string_view file, uint32_t line) const {
  if (line == 0) {
    return false;
  }
  for (const auto& breakpoint : debug_breakpoints_) {
    if (breakpoint.line != line) {
      continue;
    }
    if (file == breakpoint.file) {
      return true;
    }
    if (file.size() > breakpoint.file.size() &&
        file.substr(file.size() - breakpoint.file.size()) == breakpoint.file) {
      return true;
    }
  }
  return false;
}

void Runtime::set_current_frame(
    const std::shared_ptr<const ir::Module>* module_owner,
    uint32_t function_id,
    const Value* globals_module,
    uint32_t instruction_index) {
  auto& state = current_frame_state(*this);
  state.module_owner = module_owner;
  state.function_id = function_id;
  state.globals_module = globals_module;
  state.instruction_index = instruction_index;
  publish_current_frame_state(*this);
}

void Runtime::set_current_frame_stack(const RuntimeFrameView* frames, size_t count) {
  auto& state = current_frame_state(*this);
  state.frame_stack = frames;
  state.frame_stack_count = count;
  publish_current_frame_state(*this);
}

void Runtime::clear_current_frame() {
  auto& state = current_frame_state(*this);
  state.module_owner = nullptr;
  state.globals_module = nullptr;
  state.function_id = 0;
  state.instruction_index = 0;
  state.frame_stack = nullptr;
  state.frame_stack_count = 0;
  clear_current_frame_locals();
  clear_current_frame_state(*this);
}

namespace {

Value locals_snapshot_from_view(const RuntimeFrameView& view) {
  if (view.local_names == nullptr || view.local_values == nullptr) {
    return Value::dict({});
  }
  const size_t count = view.local_count < view.local_names->size() ? view.local_count : view.local_names->size();
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const auto& name = (*view.local_names)[i];
    if (name.empty() || name[0] == '#') {
      continue;
    }
    if (view.local_values[i].tag == ValueTag::Invalid) {
      continue;
    }
    entries.push_back({Value::string(name), view.local_values[i]});
  }
  return Value::dict(std::move(entries));
}

Value module_attrs_snapshot(const Value& module_value) {
  const auto* module = value_as_module(module_value);
  if (module == nullptr) {
    return Value::dict({});
  }
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(module->name_to_slot.size());
  for (const auto& entry : module->name_to_slot) {
    if (entry.second < module->slots.size() && module->slots[entry.second].tag != ValueTag::Invalid) {
      entries.push_back({Value::string(entry.first), module->slots[entry.second]});
    }
  }
  return Value::dict(std::move(entries));
}

Value materialize_frame_from_stack(const RuntimeFrameView* frames, size_t index, const Value& builtins) {
  const auto& view = frames[index];
  if (view.module_owner == nullptr || view.module_owner->get() == nullptr || view.globals_module == nullptr ||
      view.instruction_index == nullptr) {
    return Value::none();
  }
  Value back = Value::none();
  if (index != 0) {
    back = materialize_frame_from_stack(frames, index - 1, builtins);
  }
  return Value::frame(
      *view.module_owner,
      view.function_id,
      *view.globals_module,
      static_cast<uint32_t>(*view.instruction_index),
      locals_snapshot_from_view(view),
      std::move(back),
      builtins);
}

} // namespace

Value Runtime::current_frame_snapshot() const {
  Value builtins = Value::dict({});
  auto builtins_it = modules_.find("builtins");
  if (builtins_it != modules_.end()) {
    builtins = module_attrs_snapshot(builtins_it->second);
  }
  const auto& state = current_frame_state(*this);
  if (state.frame_stack != nullptr && state.frame_stack_count != 0) {
    return materialize_frame_from_stack(state.frame_stack, state.frame_stack_count - 1, builtins);
  }
  if (state.module_owner == nullptr || state.globals_module == nullptr ||
      state.module_owner->get() == nullptr) {
    return Value::none();
  }
  return Value::frame(
      *state.module_owner,
      state.function_id,
      *state.globals_module,
      state.instruction_index,
      current_locals_snapshot(),
      Value::invalid(),
      builtins);
}

void Runtime::set_current_frame_locals(const std::vector<std::string>* names, const Value* values, size_t count) {
  auto& state = current_frame_state(*this);
  state.local_names = names;
  state.local_values = values;
  state.local_count = count;
  publish_current_frame_state(*this);
}

void Runtime::clear_current_frame_locals() {
  auto& state = current_frame_state(*this);
  state.local_names = nullptr;
  state.local_values = nullptr;
  state.local_count = 0;
  publish_current_frame_state(*this);
}

Value Runtime::current_locals_snapshot() const {
  const auto& state = current_frame_state(*this);
  if (state.module_owner != nullptr && state.module_owner->get() != nullptr &&
      state.globals_module != nullptr &&
      state.function_id == state.module_owner->get()->entry) {
    return module_attrs_snapshot(*state.globals_module);
  }
  if (state.local_names == nullptr || state.local_values == nullptr) {
    return Value::dict({});
  }
  const size_t count =
      state.local_count < state.local_names->size() ? state.local_count : state.local_names->size();
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const auto& name = (*state.local_names)[i];
    if (name.empty() || name[0] == '#') {
      continue;
    }
    if (state.local_values[i].tag == ValueTag::Invalid) {
      continue;
    }
    entries.push_back({Value::string(name), state.local_values[i]});
  }
  return Value::dict(std::move(entries));
}

namespace {

Value frame_snapshot_from_state(const RuntimeCurrentFrameState& state, const Value& builtins) {
  if (state.frame_stack != nullptr && state.frame_stack_count != 0) {
    return materialize_frame_from_stack(state.frame_stack, state.frame_stack_count - 1, builtins);
  }
  if (state.module_owner == nullptr || state.globals_module == nullptr ||
      state.module_owner->get() == nullptr) {
    return Value::none();
  }
  return Value::frame(
      *state.module_owner,
      state.function_id,
      *state.globals_module,
      state.instruction_index,
      state.function_id == state.module_owner->get()->entry
          ? module_attrs_snapshot(*state.globals_module)
          : locals_snapshot_from_view(RuntimeFrameView{
                state.module_owner,
                state.globals_module,
                state.local_names,
                state.local_values,
                nullptr,
                state.local_count,
                state.function_id,
            }),
      Value::invalid(),
      builtins);
}

} // namespace

Value Runtime::current_frame_snapshots(const std::vector<int64_t>& live_thread_ids) const {
  Value builtins = Value::dict({});
  auto builtins_it = modules_.find("builtins");
  if (builtins_it != modules_.end()) {
    builtins = module_attrs_snapshot(builtins_it->second);
  }
  std::vector<std::pair<Value, Value>> entries;
  std::lock_guard<std::mutex> lock(g_runtime_frame_registry_mutex);
  auto runtime_it = g_runtime_frame_registry.find(this);
  entries.reserve(live_thread_ids.size());
  for (const auto ident : live_thread_ids) {
    Value frame = Value::none();
    if (runtime_it != g_runtime_frame_registry.end()) {
      auto frame_it = runtime_it->second.find(ident);
      if (frame_it != runtime_it->second.end()) {
        frame = frame_snapshot_from_state(frame_it->second, builtins);
      }
    }
    entries.push_back({Value::int64(ident), std::move(frame)});
  }
  if (entries.empty()) {
    entries.push_back({Value::int64(runtime_current_thread_ident()), current_frame_snapshot()});
  }
  return Value::dict(std::move(entries));
}

Value Runtime::current_exception_snapshots(const std::vector<int64_t>& live_thread_ids) const {
  std::vector<std::pair<Value, Value>> entries;
  std::lock_guard<std::mutex> lock(g_runtime_exception_registry_mutex);
  auto runtime_it = g_runtime_exception_registry.find(this);
  entries.reserve(live_thread_ids.size());
  for (const auto ident : live_thread_ids) {
    Value exception = Value::none();
    if (runtime_it != g_runtime_exception_registry.end()) {
      auto exception_it = runtime_it->second.find(ident);
      if (exception_it != runtime_it->second.end() && exception_it->second.tag != ValueTag::Invalid) {
        exception = exception_it->second;
      }
    }
    entries.push_back({Value::int64(ident), std::move(exception)});
  }
  if (entries.empty()) {
    const auto& exception = runtime_current_exception_state(*this);
    entries.push_back({
        Value::int64(runtime_current_thread_ident()),
        exception.tag == ValueTag::Invalid ? Value::none() : exception,
    });
  }
  return Value::dict(std::move(entries));
}

uint32_t Runtime::current_frame_function_id() const {
  return current_frame_state(*this).function_id;
}

const std::shared_ptr<const ir::Module>* Runtime::current_frame_module_owner() const {
  return current_frame_state(*this).module_owner;
}

void Runtime::register_exit_function(
    Value callable,
    std::vector<Value> args,
    std::vector<std::pair<std::string, Value>> kwargs) {
  exit_functions_.push_back(ExitFunction{std::move(callable), std::move(args), std::move(kwargs)});
}

void Runtime::unregister_exit_function(const Value& callable) {
  exit_functions_.erase(
      std::remove_if(
          exit_functions_.begin(),
          exit_functions_.end(),
          [&](const ExitFunction& entry) {
            if (entry.callable.tag != callable.tag) {
              return false;
            }
            if (entry.callable.tag == ValueTag::Object) {
              return entry.callable.as.obj == callable.as.obj;
            }
            if (entry.callable.tag == ValueTag::Int64) {
              return entry.callable.as.i64 == callable.as.i64;
            }
            return false;
          }),
      exit_functions_.end());
}

Value Runtime::make_native_function(
    std::string name,
    NativeFunctionCallback callback,
    void* user_data,
    void (*user_data_cleanup)(void*),
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    NativeKeywordFunctionCallback keyword_callback,
    bool bind_as_descriptor) {
  const uint32_t native_id = next_native_id_++;
  return Value::native_function(
      native_id,
      std::move(name),
      callback,
      user_data,
      user_data_cleanup,
      fast_callback,
      fast_releases_vm_lock,
      keyword_callback,
      bind_as_descriptor);
}

void Runtime::register_module(std::string name, Value module) {
  std::string key = name;
  ensure_module_import_metadata(module, key);
  modules_[std::move(name)] = std::move(module);
  auto it = modules_.find(key);
  if (it != modules_.end() && modules_dict_.tag != ValueTag::Invalid) {
    canonicalize_module_loader_from_bootstrap(modules_, it->second);
    std::string ignored;
    mapping_set_item(modules_dict_, Value::string(key), it->second, ignored);
  }
}

void Runtime::unregister_module(const std::string& name) {
  modules_.erase(name);
  if (modules_dict_.tag != ValueTag::Invalid) {
    std::string ignored;
    mapping_delete_item(modules_dict_, Value::string(name), ignored);
  }
}

void Runtime::register_native_package_cleanup(void* data, void (*cleanup)(void*)) {
  if (cleanup == nullptr) {
    return;
  }
  native_package_cleanups_.push_back(std::make_pair(data, cleanup));
}

void Runtime::register_raw_block_handler(std::string language, std::string provider, RawBlockHandler handler) {
  raw_block_handlers_[std::move(language) + "\n" + std::move(provider)] = handler;
}

bool Runtime::execute_raw_block(
    RawBlockContext& context,
    const std::string& language,
    const std::string& provider,
    const std::string& body,
    std::string& error) {
  auto it = raw_block_handlers_.find(language + "\n" + provider);
  if (it == raw_block_handlers_.end()) {
    it = raw_block_handlers_.find(language + "\n");
  }
  if (it == raw_block_handlers_.end() || it->second == nullptr) {
    error = "no raw block provider registered for '" + language + " " + provider + "'";
    return false;
  }
  return it->second(*this, context, language, provider, body, error);
}

bool Runtime::import_module(const std::string& name, Value& out, std::string& error) {
  static const bool trace_imports = std::getenv("XLANG3_TRACE_IMPORTS") != nullptr;
  static const bool diag_missing_imports = std::getenv("XLANG3_DIAG_MISSING_IMPORTS") != nullptr;
  if (trace_imports) {
    std::cerr << "xlang3 import: " << name << "\n";
  }
  auto it = modules_.find(name);
  if (it == modules_.end()) {
    if (modules_dict_.tag != ValueTag::Invalid) {
      Value registry_module;
      std::string registry_error;
      if (mapping_get_item(modules_dict_, Value::string(name), registry_module, registry_error) &&
          value_as_module(registry_module) != nullptr) {
        modules_[name] = registry_module;
        value_assign_fast(out, registry_module);
        return true;
      }
    }
#if !defined(XLANG3_EMBEDDED)
    std::string python_error;
    if (import_python_module(*this, name, out, python_error)) {
      return true;
    }
    const bool python_source_not_found = python_error == "module '" + name + "' not found";
    std::string native_error;
    if (python_source_not_found &&
        import_native_package(*this, name, NativePackageLookupMode::ExactNameOnly, out, native_error)) {
      return true;
    }
    std::string prefixed_native_error;
    if (python_source_not_found &&
        import_native_package(*this, name, NativePackageLookupMode::IncludeXlangPrefixFallback, out, prefixed_native_error)) {
      return true;
    }
    if (!python_error.empty() && !native_error.empty()) {
      error = python_error + "; native package candidates tried:\n" + native_error + "\n" + prefixed_native_error;
    } else {
      error = native_error.empty() ? python_error : native_error;
    }
    if (diag_missing_imports) {
      std::cerr << "XLANG3_MISSING_IMPORT name=\"" << name << "\"";
      if (python_source_not_found) {
        std::cerr << " python_source=\"not_found\"";
      } else if (!python_error.empty()) {
        std::cerr << " python_source=\"error\"";
      }
      if (!native_error.empty()) {
        std::cerr << " native=\"tried\"";
      }
      std::cerr << "\n";
    }
#else
    error = "module '" + name + "' not found in embedded runtime";
#endif
    return false;
  }
  value_assign_fast(out, it->second);
  return true;
}

bool Runtime::has_registered_module(const std::string& name) const {
  return modules_.find(name) != modules_.end();
}

bool Runtime::import_from(const std::string& module_name, const std::string& attr_name, Value& out, std::string& error) {
  std::string resolved_module = module_name;
  while (!resolved_module.empty() && resolved_module.front() == '.') {
    resolved_module.erase(resolved_module.begin());
  }
  Value module;
  if (!import_module(resolved_module, module, error)) {
    return false;
  }
  if (module_get_attr(module, attr_name, out, error) && out.tag != ValueTag::Invalid) {
    return true;
  }
  if (resolved_module == "builtins" || resolved_module == "_builtins") {
    if (const auto* builtin = find_builtin(attr_name)) {
      value_assign_fast(out, *builtin);
      return true;
    }
  }

  std::string submodule_error;
  if (import_module(resolved_module.empty() ? attr_name : resolved_module + "." + attr_name, out, submodule_error)) {
    return true;
  }
  if (!submodule_error.empty()) {
    error = submodule_error;
  }
  return false;
}

bool Runtime::import_star(const std::string& module_name, Value& target_module, std::string& error) {
  std::string resolved_module = module_name;
  while (!resolved_module.empty() && resolved_module.front() == '.') {
    resolved_module.erase(resolved_module.begin());
  }
  Value module;
  if (!import_module(resolved_module, module, error)) {
    return false;
  }
  auto* source = value_as_module(module);
  if (source == nullptr) {
    error = "star import source is not a module";
    return false;
  }
  auto* target = value_as_module(target_module);
  if (target == nullptr) {
    error = "star import target is not a module";
    return false;
  }
  for (const auto& item : source->name_to_slot) {
    const std::string& name = item.first;
    const uint32_t slot = item.second;
    if (name.empty() || name[0] == '_' || slot >= source->slots.size()) {
      continue;
    }
    if (!module_set_attr(target_module, name, source->slots[slot], error)) {
      return false;
    }
  }
  return true;
}

#if !defined(XLANG3_EMBEDDED)
void Runtime::add_import_root(std::filesystem::path root) {
  root = normalize_import_root(std::move(root));
  if (has_import_root(import_roots_, root)) {
    return;
  }
  import_roots_.push_back(std::move(root));
}

void Runtime::prepend_import_root(std::filesystem::path root) {
  root = normalize_import_root(std::move(root));
  if (has_import_root(import_roots_, root)) {
    return;
  }
  import_roots_.insert(import_roots_.begin(), std::move(root));
}
#endif

} // namespace xlang3
