import gc
import threading


class Node:
    pass


class Slotted:
    __slots__ = ('link', 'value')


marker = 17777999


def still_target(obj, identities):
    if id(obj) not in identities:
        return False
    if type(obj) is Node or type(obj) is Slotted:
        return getattr(obj, 'value', None) is marker
    if type(obj) is dict:
        return obj.get('gc_marker') is marker
    if type(obj) in (list, tuple, set):
        return any(item is marker for item in obj)
    return False


def assert_reclaimed(make):
    gc.collect()
    identities = make()
    assert gc.collect() >= len(identities)
    # get_objects() allocates a fresh list that can reuse a reclaimed list ID.
    # Require the fixture marker as well, rather than mistaking address reuse
    # for a retained cycle. Markers do not hold a reference back to the graph.
    assert not any(still_target(obj, identities) for obj in gc.get_objects())


def instance_cycle():
    value = Node()
    value.link = value
    value.value = marker
    return {id(value)}


def list_cycle():
    value = [marker]
    value.append(value)
    return {id(value)}


def dict_cycle():
    value = {'gc_marker': marker}
    value['self'] = value
    value['number'] = 17
    return {id(value)}


def duplicate_edges():
    first = [marker]
    second = [marker, first]
    first.extend([second, second, second])
    return {id(first), id(second)}


def mixed_cycle():
    value = Node()
    value.value = marker
    items = [marker]
    mapping = {'node': value, 'gc_marker': marker}
    members = {value, marker}
    sequence = (marker, mapping, members)
    value.link = items
    items.append(sequence)
    return {id(value), id(items), id(mapping), id(members), id(sequence)}


def slot_cycle():
    value = Slotted()
    value.link = value
    value.value = marker
    return {id(value)}


for make in (instance_cycle, list_cycle, dict_cycle, duplicate_edges, mixed_cycle, slot_cycle):
    assert_reclaimed(make)
print('PASS ordinary-instance-container-slot-cycles')


root = Node()
root.link = root
tail = []
tail.append(tail)
root.tail = tail
del tail
gc.collect()
assert root.link is root and root.tail[0] is root.tail
assert root.__dict__['tail'] is root.tail
print('PASS reachable-cycle-and-tail')


events = []


class Finalizable:
    def __del__(self):
        events.append('finalized')


protected = Finalizable()
protected.link = protected
gc.collect()
assert protected.link is protected and events == []
protected.link = None
del protected
assert events == ['finalized']
print('PASS reachable-finalizer-boundary')


ready = threading.Event()
release = threading.Event()
errors = []


def hold_in_thread():
    value = Node()
    value.link = value
    ready.set()
    release.wait()
    if value.link is not value:
        errors.append('cleared live thread root')
    value.link = None


worker = threading.Thread(target=hold_in_thread)
worker.start()
ready.wait()
gc.collect()
release.set()
worker.join()
assert errors == []
print('PASS other-thread-frame-root')


outer = LookupError('outer')
try:
    raise outer
except LookupError:
    assert gc.collect() >= 0
    import sys
    assert sys.exception() is outer
print('PASS handled-exception-preserved')
