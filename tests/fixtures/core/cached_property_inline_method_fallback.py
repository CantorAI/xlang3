import functools


class Metadata:
    @functools.cached_property
    def entry_points(self):
        return ["console_scripts", "pydantic"]


class Installer:
    def __init__(self, distribution):
        self.distribution = distribution

    def iter_entry_points(self):
        return self.distribution.entry_points


entries = Installer(Metadata()).iter_entry_points()
assert entries == ["console_scripts", "pydantic"]
print("cached property inline method fallback ok")
