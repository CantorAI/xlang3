import operator
from typing import Callable, Union


alias = Callable[[int], None]
combined = operator.or_(dict[str, int], alias)
print('origin', getattr(combined, '__origin__', None) is Union)
print('args-count', len(combined.__args__))
print('display', str(combined))
print('int-or', operator.or_(3, 5))
