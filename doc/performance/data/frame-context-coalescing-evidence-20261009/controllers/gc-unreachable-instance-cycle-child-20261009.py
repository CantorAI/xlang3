"""Untimed GC contract: discover an unreachable cycle without weakref seeds."""
import gc


class Node:
    pass


live = Node()
live.link = live


def abandon():
    node = Node()
    node.link = node
    return id(node)


gc.collect()
dead_id = abandon()
collected = gc.collect()
dead_present = any(type(obj) is Node and id(obj) == dead_id for obj in gc.get_objects())
live_same = live.link is live
print('GC_RESULT', collected, dead_present, live_same)
assert live_same, 'reachable cycle changed'
assert collected >= 1, 'unreachable instance cycle not collected'
assert not dead_present, 'unreachable instance cycle still tracked'
print('PASS unreachable cycle reclaimed; reachable cycle preserved')
