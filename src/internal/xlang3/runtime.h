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
#pragma once

#include "xlang3/value.h"
#include "xlang3/vfs.h"

#if !defined(XLANG3_EMBEDDED)
#include <filesystem>
#include <ostream>
#endif
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xlang3 {

struct OutputSink {
  void* context = nullptr;
  void (*write)(void* context, const char* data, std::size_t size) = nullptr;
};

struct RuntimeFrameView {
  const std::shared_ptr<const ir::Module>* module_owner = nullptr;
  const Value* globals_module = nullptr;
  const std::vector<std::string>* local_names = nullptr;
  const Value* local_values = nullptr;
  const size_t* instruction_index = nullptr;
  size_t local_count = 0;
  uint32_t function_id = 0;
};

enum class RuntimeDebugStepMode : uint8_t {
  Continue,
  StepInto,
  StepOver,
  StepOut,
};

enum class RuntimePauseReason : uint8_t {
  None,
  Breakpoint,
  Step,
  StepOver,
  StepOut,
  PauseRequest,
};

struct RuntimeDebugBreakpoint {
  std::string file;
  uint32_t line = 0;
};

struct RawBlockContext {
  std::function<bool(const std::string& name, Value& out, std::string& error)> get_var;
  std::function<bool(const std::string& name, const Value& value, std::string& error)> set_var;
};

struct ExitFunction {
  Value callable;
  std::vector<Value> args;
  std::vector<std::pair<std::string, Value>> kwargs;
};

class Runtime {
public:
  using RawBlockHandler = bool (*)(
      Runtime& runtime,
      RawBlockContext& context,
      const std::string& language,
      const std::string& provider,
      const std::string& body,
      std::string& error);

#if !defined(XLANG3_EMBEDDED)
  explicit Runtime(std::ostream& out);
#endif
  explicit Runtime(OutputSink output);
  ~Runtime();

  void write_output(const char* data, std::size_t size);
  void write_output(const std::string& text) { write_output(text.data(), text.size()); }
  void write_output(const char* text);
  void write_output(char ch) { write_output(&ch, 1); }

  void register_builtin(std::string name, Value value);
  void register_native_builtin(
      std::string name,
      NativeFunctionCallback callback,
      NativeFastCallCallback fast_callback = nullptr,
      bool fast_releases_vm_lock = false,
      NativeKeywordFunctionCallback keyword_callback = nullptr);
  const Value* find_builtin(const std::string& name) const;
  Value make_exception(std::string class_name, std::string message);
  Value make_exception_from_class(Value klass, std::string message);
  Value exception_type(const Value& exception);
  bool raise_class_error(std::string class_name, std::string message);
  void set_pending_exception(Value exception);
  bool take_pending_exception(Value& out);
  void set_active_exception(Value exception);
  void clear_active_exception();
  const Value& active_exception() const;
  Value make_native_function(
      std::string name,
      NativeFunctionCallback callback,
      void* user_data = nullptr,
      void (*user_data_cleanup)(void*) = nullptr,
      NativeFastCallCallback fast_callback = nullptr,
      bool fast_releases_vm_lock = false,
      NativeKeywordFunctionCallback keyword_callback = nullptr);
  void register_module(std::string name, Value module);
  void unregister_module(const std::string& name);
  const Value& module_registry_dict() const { return modules_dict_; }
  void register_native_package_cleanup(void* data, void (*cleanup)(void*));
  void register_raw_block_handler(std::string language, std::string provider, RawBlockHandler handler);
  bool execute_raw_block(
      RawBlockContext& context,
      const std::string& language,
      const std::string& provider,
      const std::string& body,
      std::string& error);
  bool import_module(const std::string& name, Value& out, std::string& error);
  bool has_registered_module(const std::string& name) const;
  bool import_from(const std::string& module_name, const std::string& attr_name, Value& out, std::string& error);
  bool import_star(const std::string& module_name, Value& target_module, std::string& error);
  Vfs& vfs() { return *vfs_; }
  const Vfs& vfs() const { return *vfs_; }
#if !defined(XLANG3_EMBEDDED)
  void add_import_root(std::filesystem::path root);
  void prepend_import_root(std::filesystem::path root);
  const std::vector<std::filesystem::path>& import_roots() const { return import_roots_; }
  bool publish_sys_path(std::string& error);
#endif
  void set_last_error(std::string error) { last_error_ = std::move(error); }
  const std::string& last_error() const { return last_error_; }
  void set_current_globals_module(const Value& globals_module);
  const Value& current_globals_module() const { return current_globals_module_; }
  bool set_sys_argv(const std::vector<std::string>& argv, std::string& error);
  void set_trace_function(Value trace_function);
  const Value& trace_function() const;
  bool trace_dispatch_active() const;
  void set_trace_dispatch_active(bool active);
  void set_thread_trace_function(Value trace_function);
  const Value& thread_trace_function() const { return thread_trace_function_; }
  void set_profile_function(Value profile_function);
  const Value& profile_function() const;
  bool profile_dispatch_active() const;
  void set_profile_dispatch_active(bool active);
  void set_thread_profile_function(Value profile_function);
  const Value& thread_profile_function() const { return thread_profile_function_; }
  void set_current_frame(
      const std::shared_ptr<const ir::Module>* module_owner,
      uint32_t function_id,
      const Value* globals_module,
      uint32_t instruction_index);
  void set_current_frame_stack(const RuntimeFrameView* frames, size_t count);
  void clear_current_frame();
  Value current_frame_snapshot() const;
  uint32_t current_frame_function_id() const;
  const std::shared_ptr<const ir::Module>* current_frame_module_owner() const;
  void set_current_frame_locals(const std::vector<std::string>* names, const Value* values, size_t count);
  void clear_current_frame_locals();
  Value current_locals_snapshot() const;
  Value current_frame_snapshots(const std::vector<int64_t>& live_thread_ids) const;
  Value current_exception_snapshots(const std::vector<int64_t>& live_thread_ids) const;
  void set_debug_hook(Value hook);
  const Value& debug_hook() const { return debug_hook_; }
  bool debug_dispatch_active() const { return debug_dispatch_active_; }
  void set_debug_dispatch_active(bool active);
  bool debug_poll_needed() const { return debug_poll_needed_; }
  bool debug_step_active() const { return debug_step_mode_ != RuntimeDebugStepMode::Continue; }
  bool debug_pause_on_hit() const { return debug_pause_on_hit_; }
  void set_debug_enabled(bool enabled);
  void set_debug_pause_on_hit(bool enabled);
  void debug_request_pause();
  void debug_add_breakpoint(std::string file, uint32_t line);
  void debug_clear_breakpoints();
  void debug_step_into(size_t frame_count, uint32_t line);
  void debug_step_over(size_t frame_count, uint32_t line);
  void debug_step_out(size_t frame_count);
  void debug_continue();
  RuntimePauseReason debug_step_pause_reason(size_t frame_count, uint32_t line) const;
  bool debug_skip_breakpoint_at_step_origin(size_t frame_count, uint32_t line) const;
  bool debug_breakpoint_matches(std::string_view file, uint32_t line) const;
  void register_exit_function(Value callable, std::vector<Value> args, std::vector<std::pair<std::string, Value>> kwargs = {});
  void unregister_exit_function(const Value& callable);
  bool run_exit_functions(std::string& error);

private:
  void initialize();
  void refresh_debug_poll_needed();

  OutputSink output_;
  std::unique_ptr<Vfs> vfs_;
  std::string last_error_;
  Value pending_exception_;
  Value active_exception_;
  Value current_globals_module_;
  Value trace_function_;
  Value thread_trace_function_;
  Value profile_function_;
  Value thread_profile_function_;
  Value debug_hook_;
  bool debug_dispatch_active_ = false;
  bool debug_poll_needed_ = false;
  bool debug_enabled_ = false;
  bool debug_pause_on_hit_ = false;
  bool debug_pause_requested_ = false;
  RuntimeDebugStepMode debug_step_mode_ = RuntimeDebugStepMode::Continue;
  size_t debug_step_frame_count_ = 0;
  uint32_t debug_step_line_ = 0;
  std::vector<RuntimeDebugBreakpoint> debug_breakpoints_;
  const std::shared_ptr<const ir::Module>* current_frame_module_owner_ = nullptr;
  const Value* current_frame_globals_module_ = nullptr;
  uint32_t current_frame_function_id_ = 0;
  uint32_t current_frame_instruction_index_ = 0;
  const RuntimeFrameView* current_frame_stack_ = nullptr;
  size_t current_frame_stack_count_ = 0;
  const std::vector<std::string>* current_local_names_ = nullptr;
  const Value* current_local_values_ = nullptr;
  size_t current_local_count_ = 0;
  uint32_t next_native_id_ = 1;
  std::unordered_map<std::string, Value> builtins_;
  std::unordered_map<std::string, Value> modules_;
  Value modules_dict_;
  std::vector<std::pair<void*, void (*)(void*)>> native_package_cleanups_;
  std::unordered_map<std::string, RawBlockHandler> raw_block_handlers_;
  std::vector<ExitFunction> exit_functions_;
  bool exit_functions_running_ = false;
#if !defined(XLANG3_EMBEDDED)
  std::vector<std::filesystem::path> import_roots_;
#endif
};

} // namespace xlang3
