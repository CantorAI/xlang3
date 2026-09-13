import sys

module = sys.modules[__name__]
global_map = globals()
print(type(global_map).__name__)
print(global_map is globals(), global_map is locals(), global_map is vars())
print(global_map is module.__dict__, vars(module) is global_map)
print(sys._getframe().f_globals is global_map, sys._getframe().f_locals is global_map)

assigned_by_code = 41
print(global_map["assigned_by_code"])
global_map["assigned_by_mapping"] = 42
print(assigned_by_mapping)
global_map[7] = "non-string"
print(global_map[7])
del global_map["assigned_by_mapping"]
try:
    assigned_by_mapping
except NameError:
    print("mapping-delete-visible")

def function_locals(value):
    first = locals()
    second = locals()
    print(type(first).__name__, type(second).__name__, first is second)
    first["value"] = 99
    print(value, locals()["value"])
    frame = sys._getframe()
    print(frame.f_globals is global_map, frame.f_locals["value"])

function_locals(5)

def function_globals():
    pass

print(type(function_globals.__globals__).__name__, function_globals.__globals__ is global_map)


