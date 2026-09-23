import math


class Truncatable:
    def __trunc__(self):
        return 17


print(math.trunc(4), math.trunc(-3.75), math.trunc(Truncatable()))
