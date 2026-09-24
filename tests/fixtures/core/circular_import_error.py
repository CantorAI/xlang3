import importlib


try:
    importlib.import_module("circular_import_case.first")
except ImportError as exc:
    message = str(exc)
    print("cannot import name 'missing' from partially initialized module 'circular_import_case.first'" in message)
    print("most likely due to a circular import" in message)
else:
    print(False)
    print(False)

module = importlib.import_module("circular_import_case.complete")
print(module.__spec__._initializing)
