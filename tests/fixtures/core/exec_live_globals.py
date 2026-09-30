import sys
import types


namespace = {"__name__": "plain_exec"}
namespace["same"] = namespace
exec("seen = globals() is same\nvalue = 7\n", namespace)
print(namespace["seen"], namespace["value"])

module = types.ModuleType("live_exec_oracle")
sys.modules[module.__name__] = module
try:
    code = compile(
        "class Branch:\n    pass\n"
        "visible = sys.modules[__name__].__dict__.get('Branch') is Branch\n",
        "<live module globals>", "exec")
    module.__dict__["sys"] = sys
    exec(code, module.__dict__)
    print(module.visible, module.Branch is module.__dict__["Branch"])
finally:
    del sys.modules[module.__name__]

try:
    exec("written = 11\nraise ValueError('stop')\n", namespace)
except ValueError:
    pass
print(namespace["written"])
