"""An overwritten value finalizer must see the newly published dict value."""
events = []


class Previous:
    def __init__(self, owner):
        self.owner = owner

    def __del__(self):
        events.append(self.owner[(b'replace',)])
        self.owner.clear()
        self.owner[(b'callback',)] = 7


def install(owner):
    owner[(b'replace',)] = Previous(owner)


owner = {}
install(owner)
owner[(b'replace',)] = 9
assert events == [9], events
assert list(owner.items()) == [((b'callback',), 7)]
print('replacement published before finalizer; callback writes preserved: OK')
