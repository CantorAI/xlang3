"""Global deletion retires obsolete VM owners while preserving Python aliases."""
events = []


class Victim:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        events.append(self.name)


victim = Victim('alias')
keeper = victim
name = victim.name
del victim
assert events == []
assert keeper.name == 'alias'
del keeper
assert events == ['alias'], events
print('global aliases keep their real owner until deletion: OK')

events.clear()
for number in (1, 2, 3):
    victim = Victim(number)
    name = victim.name
    del victim
    assert events == list(range(1, number + 1)), events
print('fused global attribute receiver does not survive loop deletion: OK')

events.clear()
winner = max([Victim('winner')], key=lambda item: 1)
name = winner.name
del winner
assert events == ['winner'], events
print('dead native-call container temporaries release returned items: OK')

events.clear()


class Reentrant:
    def __del__(self):
        events.append('absent' if 'reentrant' not in globals() else 'present')


reentrant = Reentrant()
del reentrant
assert events == ['absent'], events
print('global binding is absent before finalizer reentry: OK')
