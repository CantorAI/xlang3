static = staticmethod(1)
klass = classmethod(2)

print(static.__func__, static.__wrapped__)
print(klass.__func__, klass.__wrapped__)
print(static.__get__(None, object))
