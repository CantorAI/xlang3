#include "xlang3/xlang3.h"
#include <iostream>
#include <stdexcept>

struct Counter {
  int count = 0;
  BEGIN_PACKAGE(Counter)
    APISET().AddFunc<0>("next", &Counter::Next);
    APISET().AddEvent("changed");
  END_PACKAGE
  int Next() { return ++count; }
};
static Counter shared_counter;

struct NamedPair {
  std::string name;
  int count;
  NamedPair(std::string value, int number) : name(std::move(value)), count(number) {}
  std::string Describe() { return name + ":" + std::to_string(count); }
  BEGIN_PACKAGE(NamedPair)
    APISET().AddFunc<0>("describe", &NamedPair::Describe);
    APISET().AddConst("KIND", 7);
  END_PACKAGE
};
struct Nested {
  BEGIN_PACKAGE(Nested)
    APISET().AddClass<2, NamedPair>("Pair");
  END_PACKAGE
};

struct Payload : std::enable_shared_from_this<Payload> {
  static inline int live = 0;
  Payload() { ++live; }
  ~Payload() { --live; }
  int number = 0;
  BEGIN_PACKAGE(Payload)
    APISET().AddEvent("changed");
    APISET().AddPropL("number", [](Payload* self, X::Value value) { self->number = static_cast<int>(value.ToInt64()); },
        [](Payload* self) { return self->number; });
    APISET().AddSerializer("xlang.tests.CppPayload", 1, &Payload::Encode, &Payload::Decode);
  END_PACKAGE
  X::Value Encode() { return X::Value(number); }
  void Decode(const X::Value& state) {
    if (!state.IsInt64()) throw X::Error("invalid payload state");
    number = static_cast<int>(state.ToInt64());
  }
};
struct Objects {
  bool fail_creation = false;
  void OnPackageCreated(X::Package<Objects>*) {
    if (fail_creation) throw std::runtime_error("test serializer registration rollback");
  }
  std::shared_ptr<Payload> retained;
  X::Value MakeShared() {
    retained = std::make_shared<Payload>();
    retained->number = 73;
    return __xlang3_package_->CreateInstance("Payload", retained);
  }
  bool FailedCreationCleansUp() {
    const auto before = Payload::live;
    auto missing = __xlang3_package_->CreateInstance("Missing", new Payload());
    return !missing.IsValid() && Payload::live == before;
  }
  BEGIN_PACKAGE(Objects)
    APISET().AddClass<0, Nested>("Nested");
    APISET().AddClass<0, Counter>("Counter", &shared_counter);
    APISET().AddClass<0, Payload>("Payload");
    APISET().AddFunc<0>("shared", &Objects::MakeShared);
    APISET().AddFunc<0>("failed_creation_cleans_up", &Objects::FailedCreationCleansUp);
  END_PACKAGE
};

struct Service {
  int calls = 0;
  bool fail_creation = false;
  BEGIN_PACKAGE(Service)
    APISET().AddFunc<1>("add", &Service::Add);
    APISET().AddEvent("changed");
    APISET().AddVarFunc("echo", &Service::Echo);
    APISET().AddFunc<0>("caller", &Service::Caller);
  END_PACKAGE
  int Add(int value) { ++calls; return value + 2; }
  X::Value Caller() {
    X::Runtime runtime(Host()->runtime);
    X::Module sys(runtime, "sys");
    auto frame = sys["_getframe"]();
    auto result = X::Value::List(Host());
    result.Append(frame["f_code"]["co_filename"]);
    result.Append(frame["f_lineno"]);
    return result;
  }
  X::Value Echo(const X::ARGS& args, const X::KWARGS& kwargs) {
    if (args.size() == 1 && kwargs.empty()) return args[0];
    if (args.empty() && kwargs.size() == 1 && kwargs[0].first == "value") return kwargs[0].second;
    throw X::Error("echo requires one value");
  }
  void OnPackageCreated(X::Package<Service>*) {
    if (fail_creation) throw std::runtime_error("test registration failure");
  }
};

int main() {
  try {
    Service service;
    {
      Objects objects;
      X::Runtime runtime;
      objects.fail_creation = true;
      bool codec_failure_rejected = false;
      try { runtime.RegisterPackage("embedded_objects", objects); }
      catch (const std::exception&) { codec_failure_rejected = true; }
      if (!codec_failure_rejected || objects.Host()) throw std::runtime_error("serializer package failure not rolled back");
      objects.fail_creation = false;
      auto module = runtime.RegisterPackage("embedded_objects", objects);
      {
        auto nested = module["Nested"]();
        auto pair = nested["Pair"]("native", 42);
        if (!pair.NativeData<NamedPair>() || pair["describe"]().ToString() != "native:42" ||
            nested["Pair"]["KIND"].ToInt64() != 7 || module["Pair"].IsValid())
          throw std::runtime_error("nested native class construction failed");
        X::Value invalid;
        if (nested["Pair"].Call({X::Value(runtime, "bad"), X::Value(runtime, "not an integer")}, invalid))
          throw std::runtime_error("invalid native constructor argument accepted");
        if (X::Value(4294967295u).ToLongLong() != 4294967295LL)
          throw std::runtime_error("unsigned SDK integer narrowed");
      }
      auto first = module["Counter"]();
      auto second = module["Counter"]();
      if (first.NativeData<Counter>() != &shared_counter || second.NativeData<Counter>() != &shared_counter ||
          first.NativeData<Payload>() || first["next"]().ToInt64() != 1 || second["next"]().ToInt64() != 2)
        throw std::runtime_error("typed singleton binding failed");
      auto payload = module["Payload"]();
      {
        auto other = module["Payload"]();
        auto event = payload["changed"];
        auto separate = other["changed"];
        if (!event.IsEvent() || !separate.IsEvent() || event == separate ||
            !(event == payload.NativeData<Payload>()->GetEvent("changed")) ||
            !(first["changed"] == second["changed"]))
          throw std::runtime_error("native instance events are not isolated by native owner");
        std::vector<uint64_t> counts;
        if (!event.OnSubscribersChanged([&counts](uint64_t count) { counts.push_back(count); }))
          throw std::runtime_error("event observer registration failed");
        uint64_t cookie = 0;
        X::Value result;
        if (!event.Subscribe(first["next"], cookie) || !event.Fire({}, result) || result.ToInt64() != 3 ||
            !event.Unsubscribe(cookie) || !event.Unsubscribe(cookie) || counts != std::vector<uint64_t>{1, 0})
          throw std::runtime_error("native event subscribe/fire/unsubscribe failed");
        if (!event.OnSubscribersChanged([](uint64_t) {})) throw std::runtime_error("event observer replacement failed");
      }
      if (payload.NativeData<Payload>()->shared_from_this().get() != payload.NativeData<Payload>() ||
          module["failed_creation_cleans_up"]().ToLongLong() != 1)
        throw std::runtime_error("native owned instance lifetime failed");
      auto shared = module["shared"]();
      std::weak_ptr<Payload> weak = objects.retained;
      objects.retained.reset();
      if (weak.expired() || shared.NativeData<Payload>()->number != 73)
        throw std::runtime_error("native shared owner was released too early");
      shared = X::Value();
      if (!weak.expired()) throw std::runtime_error("native shared owner leaked");
      if (!payload.SetAttr("number", X::Value(42))) throw std::runtime_error("payload setup failed");
      X::Value bytes, restored;
      if (!payload.ToBytes(bytes) || !bytes.FromBytes(restored) || !restored.NativeData<Payload>() ||
          restored["number"].ToInt64() != 42 || restored.NativeData<Payload>() == payload.NativeData<Payload>())
        throw std::runtime_error("C++ native serializer failed");
      if (restored.NativeData<Payload>()->shared_from_this().get() != restored.NativeData<Payload>())
        throw std::runtime_error("decoded native shared ownership failed");
    }
    if (Payload::live) throw std::runtime_error("native payload leaked");
    if (shared_counter.Host() || shared_counter.count != 3) throw std::runtime_error("singleton lifetime failed");
    for (int pass = 0; pass < 2; ++pass) {
      {
        X::Runtime runtime;
        service.fail_creation = true;
        bool failure_rejected = false;
        try { runtime.RegisterPackage("embedded_service", service); }
        catch (const std::exception&) { failure_rejected = true; }
        if (!failure_rejected || service.Host()) throw std::runtime_error("failed registration retained a host");
        service.fail_creation = false;
        auto module = runtime.RegisterPackage("embedded_service", service);
        X::Module imported(runtime, "embedded_service");
        X::Module builtins(runtime, "builtins");
        auto globals = runtime.Dict();
        builtins["exec"]("import embedded_service\nlocation = embedded_service.caller()\n", globals);
        if (globals["location"][1].ToInt64() != 2 || globals["location"][0].ToString().empty())
          throw std::runtime_error("native caller source location was lost");
        auto list = runtime.List();
        X::Value echoed;
        if (!imported["echo"].Call({}, {{"value", list}}, echoed) || !echoed.Append(X::Value(42)) || list.Size() != 1)
          throw std::runtime_error("native keyword argument identity was lost");
        if (imported["echo"](7).ToInt64() != 7 || imported["echo"].Call({}, {{"bad", list}}, echoed))
          throw std::runtime_error("native keyword validation failed");
        if (imported["add"](40).ToInt64() != 42 || !module["changed"].IsEvent())
          throw std::runtime_error("embedded package was not callable");
        bool duplicate_rejected = false;
        try { runtime.RegisterPackage("embedded_service", service); }
        catch (const std::exception&) { duplicate_rejected = true; }
        if (!duplicate_rejected || imported["add"](1).ToInt64() != 3)
          throw std::runtime_error("duplicate registration changed original package");
      }
      if (service.Host() != nullptr || service.calls != (pass + 1) * 2)
        throw std::runtime_error("borrowed service lifetime was not preserved");
    }
    std::cout << "Embedded C++ package passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
