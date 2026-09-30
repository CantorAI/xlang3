"""Behavioral oracle for the native rpds dependency used by jsonschema.

Run against upstream rpds-py on CPython and the XLang3 native package.
"""

from rpds import HashTrieMap, HashTrieSet, List


class Collision:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        return 7

    def __eq__(self, other):
        return isinstance(other, Collision) and self.value == other.value


empty = HashTrieMap()
first = empty.insert("one", 1)
second = first.insert("two", 2)
print("map versions", len(empty), len(first), len(second))
print("map lookup", second["one"], second.get("missing", 9), "two" in first)
print("map iter", sorted(second), sorted(second.items()))
print("map remove", len(second.remove("one")), len(second.discard("missing")))
print("map convert", HashTrieMap.convert(first) is first)
print("map update", sorted(first.update({"three": 3}).items()))
collisions = HashTrieMap().insert(Collision(1), "a").insert(Collision(2), "b")
print("map collision", collisions[Collision(1)], collisions[Collision(2)])
try:
    second["missing"]
except KeyError:
    print("map missing", "KeyError")

empty_set = HashTrieSet()
one_set = empty_set.insert("one")
two_set = one_set.insert("two")
print("set versions", len(empty_set), len(one_set), len(two_set))
print("set contents", sorted(two_set), "two" in one_set)
print("set remove", sorted(two_set.remove("one")), len(two_set.discard("missing")))

empty_list = List()
one_list = empty_list.push_front(1)
two_list = one_list.push_front(2)
print("list versions", len(empty_list), list(one_list), list(two_list))
print("list tail", list(two_list.drop_first()))
try:
    empty_list.drop_first()
except IndexError:
    print("list empty", "IndexError")
