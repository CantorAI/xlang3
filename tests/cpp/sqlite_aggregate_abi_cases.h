#pragma once
#include "test_harness.h"
#include "xlang3/xlang3.h"
#include <filesystem>
#include "xlang3/c_api_bridge.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/runtime.h"
#include "xlang3/value.h"
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace xlang3::test::sqlite_aggregate_probe {

static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// The exception extension must preserve every byte of the former ABI-24 host.
static_assert(X3_ABI_VERSION == 24u);
static_assert(offsetof(X3PackageHost, runtime_take_exception) ==
    offsetof(X3PackageHost, buffer_release) + sizeof(X3PackageHost::buffer_release));
static_assert(offsetof(X3PackageHost, runtime_restore_exception) ==
    offsetof(X3PackageHost, runtime_take_exception) + sizeof(X3PackageHost::runtime_take_exception));

struct ProbeState { X3PackageHost* host = nullptr; };
static X3Status capture_host(void* opaque, X3Value, void* data) {
  auto& state = *static_cast<ProbeState*>(data);
  state.host = static_cast<X3PackageHost*>(opaque);
  X3Module* module = nullptr;
  return state.host->add_module(state.host, "sqlite_exception_probe", &module);
}

static void transport_tests(X::Runtime& sdk, X3PackageHost& host) {
  auto& runtime = *reinterpret_cast<xlang3::Runtime*>(sdk.get());
  require(host.size >= offsetof(X3PackageHost, runtime_restore_exception) +
      sizeof(host.runtime_restore_exception), "real host extension is truncated");
  require(host.runtime_take_exception && host.runtime_restore_exception,
      "real package host lacks exception transport");
  runtime.set_last_error("unchanged transport diagnostic");
  auto outer = runtime.make_exception("ValueError", "outer identity");
  auto inner = runtime.make_exception("TypeError", "inner identity");
  auto active = runtime.make_exception("RuntimeError", "handled identity");
  const auto outer_refs = outer.as.obj->refcnt.load();
  const auto inner_refs = inner.as.obj->refcnt.load();
  runtime.set_active_exception(active);

  X3Value empty = x3_value_none();
  require(host.runtime_take_exception(sdk.get(), &empty) == X3_STATUS_OK &&
      empty.tag == X3_TAG_INVALID, "absent exception did not export INVALID");
  runtime.set_pending_exception(outer);
  require(outer.as.obj->refcnt.load() == outer_refs + 1, "pending state did not own exception");
  X3Value saved = x3_value_invalid();
  require(host.runtime_take_exception(sdk.get(), &saved) == X3_STATUS_OK &&
      saved.tag == X3_TAG_OBJECT && saved.as.obj == reinterpret_cast<X3Object*>(outer.as.obj),
      "take lost exception identity");
  require(outer.as.obj->refcnt.load() == outer_refs + 1, "take did not transfer one owned reference");
  xlang3::Value absent;
  require(!runtime.take_pending_exception(absent), "take left pending state populated");
  runtime.set_pending_exception(inner);
  X3Value discarded = x3_value_invalid();
  require(host.runtime_take_exception(sdk.get(), &discarded) == X3_STATUS_OK,
      "cannot detach callback exception");
  host.value_release(discarded);
  require(inner.as.obj->refcnt.load() == inner_refs, "discarded callback exception leaked");
  require(host.runtime_restore_exception(sdk.get(), saved) == X3_STATUS_OK,
      "cannot restore prior exception");
  require(outer.as.obj->refcnt.load() == outer_refs + 2, "restore failed to retain caller's transport");
  host.value_release(saved);
  require(outer.as.obj->refcnt.load() == outer_refs + 1, "caller release consumed restored exception");
  require(runtime.active_exception().as.obj == active.as.obj, "transport changed handled exception");
  require(host.runtime_restore_exception(nullptr, x3_value_invalid()) == X3_STATUS_ERROR &&
      host.runtime_take_exception(nullptr, &empty) == X3_STATUS_ERROR &&
      host.runtime_take_exception(sdk.get(), nullptr) == X3_STATUS_ERROR,
      "null runtime/output accepted");
  require(host.runtime_restore_exception(sdk.get(), x3_value_none()) == X3_STATUS_ERROR,
      "non-instance exception accepted");
  X3Value recovered = x3_value_invalid();
  require(host.runtime_take_exception(sdk.get(), &recovered) == X3_STATUS_OK &&
      recovered.as.obj == reinterpret_cast<X3Object*>(outer.as.obj),
      "failed transport changed prior pending state");
  require(host.runtime_restore_exception(sdk.get(), recovered) == X3_STATUS_OK,
      "cannot restore recovered exception");
  host.value_release(recovered);
  require(host.runtime_restore_exception(sdk.get(), x3_value_invalid()) == X3_STATUS_OK,
      "INVALID did not clear pending state");
  require(outer.as.obj->refcnt.load() == outer_refs, "clear leaked pending exception");
  require(sdk.LastError() == "unchanged transport diagnostic", "transport rewrote last-error text");
  runtime.clear_active_exception();
}

struct FactoryCounters {
  int destroyed = 0;
  int called = 0;
};
struct FactoryState {
  xlang3::Value aggregate_class;
  // Pending Python exceptions/tracebacks can keep a connection and factory
  // alive after the test helper has unwound. The factory owns its observation
  // storage for exactly that lifetime; never point into the helper's stack.
  std::shared_ptr<FactoryCounters> counters;
};
static bool make_group(xlang3::Runtime& runtime, const xlang3::Value*, uint32_t argc,
    xlang3::Value& out, std::string& error, void* data) {
  if (argc != 0) { error = "aggregate factory received arguments"; return false; }
  auto& state = *static_cast<FactoryState*>(data);
  ++state.counters->called;
  return xlang3::runtime_call_callable(runtime, state.aggregate_class, nullptr, 0, out, error);
}
static void destroy_factory(void* data) {
  auto* state = static_cast<FactoryState*>(data);
  ++state->counters->destroyed;
  delete state;
}
static X::Value factory(X::Runtime& sdk, const X::Value& klass,
    const std::shared_ptr<FactoryCounters>& counters) {
  std::string error;
  auto payload = xlang3::from_c_value(klass.raw(), error);
  require(error.empty(), "cannot retain Python aggregate class");
  auto& runtime = *reinterpret_cast<xlang3::Runtime*>(sdk.get());
  auto callable = runtime.make_native_function("aggregate_probe_factory", make_group,
      new FactoryState{std::move(payload), counters}, destroy_factory,
      nullptr, false, nullptr, false);
  return X::Value(sdk.host(), xlang3::to_c_value(callable), false);
}

static void aggregate_lifetime_tests(X::Runtime& runtime, const char* fixture_dir,
    const char* native_dir) {
  auto counters_a = std::make_shared<FactoryCounters>();
  auto counters_b = std::make_shared<FactoryCounters>();
  auto counters_failed = std::make_shared<FactoryCounters>();
  runtime.AddImportRoot(fixture_dir);
  runtime.AddImportRoot(native_dir);
  X::Module module(runtime, "sqlite_aggregate_sdk_probe");
  auto klass = module["Sum"];
  X::Value connection;
  require(module["connect"].Call({}, connection), "cannot open own native SQLite connection");
  auto a = factory(runtime, klass, counters_a);
  auto b = factory(runtime, klass, counters_b);
  auto failed = factory(runtime, klass, counters_failed);
  auto register_aggregate = connection["create_aggregate"];
  X::Value ignored;
  require(register_aggregate.Call({X::Value(runtime, "sdk_sum"), X::Value(1), a}, ignored),
      "native factory registration failed");
  a = X::Value();
  require(counters_a->destroyed == 0, "registered factory lost connection ownership");
  X::Value answer;
  require(module["query"].Call({connection}, answer) && answer.ToInt64() == 6 && counters_a->called == 1,
      "Python per-group aggregate did not run through native factory");
  require(module["empty"].Call({connection}, answer) && answer.IsNone() && counters_a->called == 1,
      "empty group invoked factory");
  require(register_aggregate.Call({X::Value(runtime, "sdk_sum"), X::Value(1), b}, ignored),
      "factory replacement failed");
  require(counters_a->destroyed == 1, "replacement did not release old factory exactly once");
  b = X::Value();
  require(counters_b->destroyed == 0, "replacement factory was not retained");
  require(module["query"].Call({connection}, answer) && answer.ToInt64() == 6 && counters_b->called == 1,
      "replacement retained stale callback");
  // SQL names longer than 255 UTF-8 bytes fail inside sqlite3_create_function_v2,
  // after callback context allocation: SQLite itself owns the failure xDestroy.
  require(!register_aggregate.Call({X::Value(runtime, std::string(256, 'a')), X::Value(1), failed}, ignored),
      "SQLite accepted an oversized function name");
  require(counters_failed->destroyed == 0, "registration failure consumed caller's factory reference");
  failed = X::Value();
  require(counters_failed->destroyed == 1 && counters_failed->called == 0,
      "failed registration leaked or double-destroyed native factory");
  require(connection["close"].Call({}, ignored), "connection close failed");
  require(counters_b->destroyed == 1, "close did not release active factory exactly once");
  require(connection["close"].Call({}, ignored) && counters_b->destroyed == 1,
      "repeated close released factory twice");
}

// Exercise the exact exceptional lifetime: a pending Python error owns the
// connection while C++ locals unwind, and the connection owns a native factory.
static void aggregate_failure_unwind_test(X::Runtime& runtime, X3PackageHost& host) {
  struct InjectedUnwind {};
  auto counters = std::make_shared<FactoryCounters>();
  std::weak_ptr<FactoryCounters> lifetime = counters;
  bool unwound = false;
  try {
    X::Module module(runtime, "sqlite_aggregate_sdk_probe");
    X::Value connection;
    require(module["connect"].Call({}, connection), "cannot create unwind-test connection");
    auto callback = factory(runtime, module["Sum"], counters);
    X::Value ignored;
    require(connection["create_aggregate"].Call(
        {X::Value(runtime, "sdk_sum"), X::Value(1), callback}, ignored),
        "cannot register unwind-test factory");
    callback = X::Value();
    require(!module["retain_and_fail"].Call({connection}, ignored),
        "injected Python error did not fail");
    throw InjectedUnwind{};
  } catch (const InjectedUnwind&) {
    unwound = true;
  }
  require(unwound && counters->destroyed == 0 && counters->called == 0,
      "pending error did not retain unused registered factory through C++ unwind");
  X3Value pending = x3_value_invalid();
  const X3Status status = host.runtime_take_exception(runtime.get(), &pending);
  X::Value pending_owner(&host, pending, false);
  require(status == X3_STATUS_OK &&
      pending.tag == X3_TAG_OBJECT, "unwind test lost the pending Python exception");
  // At this point the helper's observer lets go: only native FactoryState owns
  // the counters. A weak observer proves their continued ownership before the
  // pending exception is released and observes one eventual callback cleanup.
  counters.reset();
  auto survivor = lifetime.lock();
  require(survivor != nullptr && survivor->destroyed == 0,
      "factory did not own counter storage after helper observer release");
  pending_owner = X::Value();
  require(survivor->destroyed == 1 && survivor->called == 0,
      "pending exception release leaked or double-destroyed unwind-test factory");
  survivor.reset();
  require(lifetime.expired(), "unwind-test counter storage leaked after callback cleanup");
}

} // namespace xlang3::test::sqlite_aggregate_probe

namespace xlang3::test {
inline void check_sqlite_aggregate_abi(CaseResult& result, const char* program) {
  try {
    using namespace sqlite_aggregate_probe;
    require(program != nullptr, "cannot locate existing test executable");
    const auto fixtures = std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path();
    const auto modules = std::filesystem::absolute(std::filesystem::path(program)).parent_path() / "modules";
    require(std::filesystem::is_regular_file(fixtures / "sqlite_aggregate_sdk_probe.py"),
        "cannot locate Python aggregate ownership fixture");
    require(std::filesystem::is_directory(modules), "missing existing sibling native modules directory");
    ProbeState state; // The embedded package borrows this until runtime teardown.
    X::Runtime runtime;
    X3Value module = x3_value_invalid();
    runtime.check(x3_runtime_register_package(runtime.get(), "sqlite_exception_probe",
        capture_host, &state, &module));
    x3_value_release(module);
    require(state.host != nullptr, "did not capture real native package host");
    transport_tests(runtime, *state.host);
    const auto fixture_path = fixtures.string();
    const auto module_path = modules.string();
    aggregate_lifetime_tests(runtime, fixture_path.c_str(), module_path.c_str());
    aggregate_failure_unwind_test(runtime, *state.host);
    expect_true(result, true, "SQLite exception ABI ownership and factory lifetime");
  } catch (const std::exception& error) {
    expect_true(result, false, error.what());
  }
}
} // namespace xlang3::test
