class Handler:
    def run(self):
        return 1


def invoke(handler):
    return handler.run()


first = Handler()
print(invoke(first))
first.run = lambda: 2
print(invoke(first))
del first.run
print(invoke(first))

second = Handler()
second.run = lambda: 3
print(invoke(second))

third = Handler()
print(invoke(third))
third.__dict__["run"] = lambda: 4
print(invoke(third))
