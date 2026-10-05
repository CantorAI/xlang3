"""Minimal LooseVersion shim for an old SymPy benchmark dependency.

Python 3.14 removed distutils. Keeping this tiny compatibility class in the
shared pyperformance overlay lets CPython and XLang3 load the same dependency
without adding code to either runtime.
"""

import re


class LooseVersion:
    _component_re = re.compile(r"(\d+|[a-z]+|\.)", re.IGNORECASE)

    def __init__(self, value):
        self.vstring = value
        self.version = []
        for component in self._component_re.split(value):
            if not component or component == ".":
                continue
            try:
                component = int(component)
            except ValueError:
                pass
            self.version.append(component)

    def __str__(self):
        return self.vstring

    def __repr__(self):
        return f"LooseVersion('{self.vstring}')"

    def __eq__(self, other):
        if isinstance(other, str):
            other = LooseVersion(other)
        return isinstance(other, LooseVersion) and self.version == other.version

    def __lt__(self, other):
        if isinstance(other, str):
            other = LooseVersion(other)
        if not isinstance(other, LooseVersion):
            return NotImplemented
        return self.version < other.version

    def __le__(self, other):
        return self == other or self < other

    def __gt__(self, other):
        return not self <= other

    def __ge__(self, other):
        return not self < other


__all__ = ["LooseVersion"]
