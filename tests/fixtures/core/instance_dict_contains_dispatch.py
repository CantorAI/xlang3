class Container:
    def __init__(self):
        self.name = "attribute"

    def __contains__(self, value):
        return value == "member"


container = Container()
print(container.__dict__)
for candidate in ("member", "name", "other"):
    print(
        candidate,
        container.__contains__(candidate),
        candidate in container,
        candidate not in container,
    )
