from concurrent.interpreters import create, create_queue, get_current, get_main


class classonly:
    def __init__(self, value):
        self.getter = classmethod(value).__get__

    def __get__(self, obj, cls):
        return self.getter(None, cls)


class DescriptorProbe:
    @classonly
    def direct(cls, value):
        return value + 1


def add(left, right):
    return left + right


print(DescriptorProbe.direct(2))
print(get_current().id == get_main().id)

interpreter = create()
print(interpreter.whence)
interpreter.prepare_main({"base": 40})
interpreter.exec("answer = base + 2")
print(interpreter.call(add, 20, 22))
print(interpreter.is_running())

queue = create_queue(2)
queue.put({"answer": [40, 2]})
print(queue.qsize(), queue.get())

interpreter.close()
print("subinterpreters-ok")
