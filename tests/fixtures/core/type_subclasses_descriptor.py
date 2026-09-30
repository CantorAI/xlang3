print(type in type.__subclasses__(object))
print(type.__subclasses__(object) == object.__subclasses__())
for call in (lambda: type.__subclasses__(),
             lambda: type.__subclasses__(object, type)):
    try:
        call()
    except TypeError as error:
        print(str(error))
