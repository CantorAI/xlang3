class Sample:
    def run(self, value, enabled=False):
        return "class", value, enabled

    @staticmethod
    def static(value, enabled=False):
        return "static", value, enabled

    @classmethod
    def class_run(cls, value, enabled=False):
        return cls.__name__, value, enabled


sample = Sample()
print(sample.run("local", enabled=True))
print(sample.static("local", enabled=True))
print(sample.class_run("local", enabled=True))
sample.run = lambda value, enabled=False: ("instance", value, enabled)
print(sample.run("local", enabled=True))


ordered = Sample()


def replace_method():
    ordered.run = lambda value, enabled=False: ("replacement", value, enabled)
    return "argument"


print(ordered.run(value=replace_method(), enabled=True))
