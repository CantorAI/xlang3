import importlib

from import_name_collision_pkg import specified_rules

print(type(specified_rules).__name__, specified_rules)
importlib.import_module("import_name_collision_pkg.specified_rules")
from import_name_collision_pkg import specified_rules as reimported

print(type(reimported).__name__, reimported)
