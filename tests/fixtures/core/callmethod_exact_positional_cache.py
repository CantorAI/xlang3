class Target:
    def method(self, value):
        return self.offset + value

    def optional(self, value=7):
        return self.offset + value


target = Target()
target.offset = 3
print(target.method(5))
print(target.optional())
Target.optional.__defaults__ = (8,)
print(target.optional())

# A class mutation must invalidate the warmed method target and select the
# replacement function before the exact-argument frame path is used again.
Target.method = lambda self, value: self.offset + value + 1
print(target.method(5))

# An instance attribute still takes precedence over the cached class method.
target.method = lambda value: value * 2
print(target.method(5))
