class DefaultRepr:
    pass


class CustomRepr:
    def __repr__(self):
        return "custom-repr"


default_text = f"{DefaultRepr()!r}"
print(
    "default-object-repr",
    default_text.startswith("<__main__.DefaultRepr object at 0x"),
    default_text.endswith(">"),
    f"{CustomRepr()!r}" == "custom-repr",
)
