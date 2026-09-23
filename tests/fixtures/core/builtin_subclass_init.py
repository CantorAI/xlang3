class Token(tuple):
    def __init__(self, values=()):
        self.subtypes = set()
        self.size_at_init = len(self)


token = Token(("Name", "Class"))
token.subtypes.add("child")
print(tuple(token), token.size_at_init, token.subtypes, token.__dict__)


class Text(str):
    def __init__(self, value):
        self.original = value


text = Text("hello")
print(text, text.original)


events = []


class Redirect(tuple):
    def __new__(cls):
        return ()

    def __init__(self):
        events.append("init")


print(Redirect(), events)
