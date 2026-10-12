"""Strict CP-first finalizer context probe for a receiver-owned payload."""
import sys

events = []

class Marker:
    def __init__(self, label):
        self.label = label

    def __del__(self):
        events.append((self.label, sys._getframe(1).f_code.co_name))

class Components:
    def __init__(self, label, a, b, c):
        self.a, self.b, self.c = a, b, c
        self.payload = Marker(label)

    def ready(self):
        return self

    def combine(self, other):
        other.ready()
        return self.a * other.a + self.b * other.b + self.c * other.c

def invoke_temporary():
    result = Components('self', 1.0, 2.0, 3.0).combine(
        Components('other', 4.0, 5.0, 6.0))
    assert result == 32.0
    assert sorted(events) == [('other', 'invoke_temporary'), ('self', 'invoke_temporary')], events

invoke_temporary()
print('PASS receiver-owned payload finalizers preserve caller context')
