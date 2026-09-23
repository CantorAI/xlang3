import gc
import weakref


class Root:
    pass


class Tail:
    pass


root = Root()
tail = Tail()
tail.marker = "alive"
tail.self = tail
root.tail = tail
root_ref = weakref.ref(root)
tail_ref = weakref.ref(tail)
tail = None

gc.collect()
print(root.tail.marker)
print(root.tail is root.tail.self)
print(root_ref() is root)
print(tail_ref() is root.tail)
