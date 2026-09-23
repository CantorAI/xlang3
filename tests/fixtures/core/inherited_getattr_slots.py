class LazyBase:
    __slots__ = ()

    def __getattr__(self, name):
        if name == "cached":
            value = "resolved"
            self.cached = value
            return value
        raise AttributeError(name)


class SlottedChild(LazyBase):
    __slots__ = ("cached",)


item = SlottedChild()
print(item.cached)
print(item.cached)
print(getattr(SlottedChild(), "cached"))
print(getattr(item, "missing", "default"))
