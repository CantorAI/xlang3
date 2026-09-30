namespace = {"bar": "baz"}
exec("print('__name__' in globals(), globals().get('__name__'))", namespace)
print("__name__" in namespace, namespace.get("__name__"))

with_locals = {"bar": "baz"}
locals_dict = {}
exec("seen = ('__name__' in globals(), globals().get('__name__'))", with_locals, locals_dict)
print(locals_dict["seen"], "__name__" in with_locals)

named = {"__name__": "given"}
exec("print(__name__, globals()['__name__'])", named)
print(named["__name__"])
