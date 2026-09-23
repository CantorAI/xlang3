# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0


class Manager:
    def __init__(self, value):
        self.value = value

    def __enter__(self):
        return self.value

    def __exit__(self, exc_type, exc_value, traceback):
        return False


class Holder:
    pass


holder = Holder()
items = [None]

with Manager(7) as holder.value:
    pass

with Manager(9) as items[0]:
    pass

with Manager(("left", "right")) as (first, second):
    pass

with Manager("a") as a, Manager("b") as b:
    pass

print(holder.value, items[0], first, second, a, b)
