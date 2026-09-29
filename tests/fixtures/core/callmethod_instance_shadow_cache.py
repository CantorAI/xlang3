class Target:
    def method(self):
        return "class"


def replacement():
    return "instance"


target = Target()
index = 0
while index < 3:
    print(target.method())
    if index == 0:
        target.method = replacement
    elif index == 1:
        del target.method
    index += 1
