class Left:
    def __init__(self):
        self.left_first = 1
        self.left_second = 2


class Right:
    def __init__(self):
        self.right_first = []
        self.right_second = -1


class Combined(Left, Right):
    def __init__(self):
        Left.__init__(self)
        Right.__init__(self)


value = Combined()
print(value.left_first, value.left_second)
print(value.right_first, value.right_second)
print(value.__dict__)
