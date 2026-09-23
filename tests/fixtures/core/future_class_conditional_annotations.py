from __future__ import annotations

flag = False


class Conditional:
    if flag:
        hidden: int
    else:
        visible: str

    value: int
    if False:
        value: str


print(Conditional.__annotations__)
