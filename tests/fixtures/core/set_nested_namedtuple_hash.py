from collections import namedtuple


Row = namedtuple("Row", "value")
first = (0, Row(1))
equal_but_distinct = (0, Row(1))
visited = set()
visited.add(first)
visited.add(equal_but_distinct)

print(
    first == equal_but_distinct,
    hash(first) == hash(equal_but_distinct),
    len(visited),
    first in visited,
    equal_but_distinct in visited,
    visited.__contains__(equal_but_distinct),
)
