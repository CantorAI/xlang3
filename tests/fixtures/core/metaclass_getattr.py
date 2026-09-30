class Meta(type):
    def __getattr__(cls, name):
        if name == 'default':
            return 17
        raise AttributeError(name)


class Config(metaclass=Meta):
    local = 4


print('direct', Config.default)
print('builtin', getattr(Config, 'default'))
print('existing', Config.local)
print('missing', hasattr(Config, 'absent'))
