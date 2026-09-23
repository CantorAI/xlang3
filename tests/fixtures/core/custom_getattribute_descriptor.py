class Calls(tuple):
    @property
    def args(self):
        return self[0]

    def __getattribute__(self, name):
        if name in tuple.__dict__:
            raise AttributeError(name)
        return tuple.__getattribute__(self, name)


print(Calls(((1, 2), {})).args)
