try:
    class Invalid(flag=True):
        pass
except TypeError as error:
    print(str(error))


class Accepting:
    def __init_subclass__(cls, **kwargs):
        cls.flag = kwargs['flag']


class Accepted(Accepting, flag=True):
    pass


print(Accepted.flag)
