from abc import ABC, abstractmethod, update_abstractmethods
from functools import singledispatchmethod


class Base(ABC):
    @abstractmethod
    def negate(self, value):
        pass


class Concrete(Base):
    @singledispatchmethod
    def negate(self, value):
        return -value


descriptor = vars(Concrete)['negate']
print('marker', descriptor.__isabstractmethod__)
print('abstracts', sorted(Concrete.__abstractmethods__))
print('result', Concrete().negate(3))
update_abstractmethods(Concrete)
print('updated', sorted(Concrete.__abstractmethods__))


class Forwarded:
    def __init__(self, wrapped):
        self.wrapped = wrapped

    def __getattr__(self, name):
        return getattr(self.wrapped, name)


class AbstractProxy(ABC):
    action = Forwarded(property(lambda self: None, None, None, None))


class MarkedProxy(ABC):
    action = Forwarded(abstractmethod(lambda self: None))


print('forwarded-concrete', sorted(AbstractProxy.__abstractmethods__))
print('forwarded-abstract', sorted(MarkedProxy.__abstractmethods__))
