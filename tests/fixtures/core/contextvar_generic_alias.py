from contextvars import ContextVar, Token


for cls in (ContextVar, Token):
    alias = cls[int]
    print(str(alias), alias.__origin__ is cls, alias.__args__ == (int,))
    pair = cls[int, str]
    print(str(pair), pair.__origin__ is cls, pair.__args__ == (int, str))

variable = ContextVar[int]("number", default=7)
print(variable.get())
