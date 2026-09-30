from math import sin, cos, sqrt


class Point(object):
    __slots__ = ('x', 'y', 'z')

    def __init__(self, i):
        self.x = x = sin(i)
        self.y = cos(i) * 3
        self.z = (x * x) / 2

    def normalize(self):
        x = self.x
        y = self.y
        z = self.z
        norm = sqrt(x * x + y * y + z * z)
        self.x /= norm
        self.y /= norm
        self.z /= norm

    def maximize(self, other):
        self.x = self.x if self.x > other.x else other.x
        self.y = self.y if self.y > other.y else other.y
        self.z = self.z if self.z > other.z else other.z
        return self


p = Point(1)
print(round(p.x, 6), round(p.y, 6), round(p.z, 6))
p.normalize()
print(round(p.x, 6), round(p.y, 6), round(p.z, 6))
q = Point(2)
print(p.maximize(q) is p, round(p.x, 6), round(p.y, 6), round(p.z, 6))
