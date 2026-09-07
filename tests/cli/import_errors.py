import importlib

try:
    import xlang3_missing_module_regression
except ModuleNotFoundError:
    pass
else:
    raise AssertionError("missing module imported")

try:
    from xlang3_missing_module_regression import member
except ModuleNotFoundError:
    pass
else:
    raise AssertionError("missing source module imported")

for importer in [__import__, importlib.import_module]:
    try:
        importer("xlang3_missing_module_regression")
    except ModuleNotFoundError:
        pass
    else:
        raise AssertionError("missing module imported through callable")

try:
    import xlang3_missing_module_regression.child
except ModuleNotFoundError:
    pass
else:
    raise AssertionError("missing parent module imported")

try:
    from sys import xlang3_missing_member_regression
except ModuleNotFoundError:
    raise AssertionError("missing attribute is not a missing module")
except ImportError:
    pass
else:
    raise AssertionError("missing attribute imported")

print("import-errors-passed")
