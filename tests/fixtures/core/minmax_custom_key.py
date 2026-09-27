class Version:
    def __init__(self, major, minor):
        self.major = major
        self.minor = minor

    def __eq__(self, other):
        return isinstance(other, Version) and (self.major, self.minor) == (other.major, other.minor)

    def __lt__(self, other):
        return (self.major, self.minor) < (other.major, other.minor)

    def __gt__(self, other):
        return (self.major, self.minor) > (other.major, other.minor)


versions = [Version(1, 2), Version(1, 3), Version(2, 0)]
key = lambda item: (0, item, ())
print('max', max(versions, key=key).major, max(versions, key=key).minor)
print('min', min(versions, key=key).major, min(versions, key=key).minor)
