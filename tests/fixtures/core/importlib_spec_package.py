import importlib.machinery
import importlib.util
import os


path = os.path.join(os.path.dirname(__file__), "spec_demo", "__init__.py")
loader = importlib.machinery.SourceFileLoader("spec_demo", path)
directory = os.path.dirname(path)

inferred = importlib.util.spec_from_file_location("spec_demo", path, loader=loader)
print(inferred.parent, inferred.submodule_search_locations == [directory])

empty = importlib.util.spec_from_file_location(
    "spec_demo", path, loader=loader, submodule_search_locations=[]
)
print(empty.parent, empty.submodule_search_locations == [directory])

custom = importlib.util.spec_from_file_location(
    "spec_demo", path, loader=loader, submodule_search_locations=["custom"]
)
print(custom.parent, custom.submodule_search_locations)
print(importlib.util.module_from_spec(custom).__path__)

not_package = importlib.util.spec_from_file_location(
    "spec_demo", path, loader=loader, submodule_search_locations=None
)
print(repr(not_package.parent), not_package.submodule_search_locations)
