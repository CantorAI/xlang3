class Metadata:
    def __init__(self, name):
        self.name = name

    def _build_entry_points(self):
        return ["console_scripts", self.name]

    @property
    def entry_points(self):
        return self._build_entry_points()


class Installer:
    def __init__(self, distribution):
        self.distribution = distribution

    def get_entry_points(self):
        return self.distribution.entry_points


print(Installer(Metadata("pytest")).get_entry_points())
