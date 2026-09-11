#include "xlang3/builtins.h"
#include "xlang3/module_object.h"
namespace xlang3 { namespace { bool enabled = false;
bool set_enabled(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) { if (argc > 2) { error = "faulthandler.enable() expected at most 2 arguments"; return false; } enabled = true; value_set_none(out); return true; }
bool clear_enabled(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) { if (argc) { error = "faulthandler.disable() expected no arguments"; return false; } enabled = false; value_set_none(out); return true; }
bool get_enabled(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) { if (argc) { error = "faulthandler.is_enabled() expected no arguments"; return false; } value_set_bool(out, enabled); return true; }
bool cancel(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) { if (argc) { error = "faulthandler.cancel_dump_traceback_later() expected no arguments"; return false; } value_set_none(out); return true; }
} void register_faulthandler_module(Runtime& runtime) { NativeModuleBuilder b(runtime, "faulthandler"); b.function("enable", set_enabled).function("disable", clear_enabled).function("is_enabled", get_enabled).function("cancel_dump_traceback_later", cancel); runtime.register_module("faulthandler", b.finish()); } }
