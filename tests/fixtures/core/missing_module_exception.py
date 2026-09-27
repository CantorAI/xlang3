import importlib


for name in ("foobar", "os.missing"):
    for load in (__import__, importlib.import_module):
        try:
            load(name)
        except ModuleNotFoundError as error:
            print(name, load.__name__, str(error), error.name)
